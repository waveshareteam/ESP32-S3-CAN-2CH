#include "bsp_web.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_can.h"
#include "bsp_common.h"
#include "bsp_rtc.h"
#include "bsp_xl2515.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

typedef struct {
    bool used;
    datetime_t time;
    uint8_t port;
    can_payload_t payload;
    repetition_event_t repeat;
    uint32_t repeat_ms;
    int64_t last_fire_us;
    char description[220];
} rtc_event_t;

extern const uint8_t root_html_start[] asm("_binary_root_html_start");
extern const uint8_t root_html_end[] asm("_binary_root_html_end");
extern const uint8_t rtc_html_start[] asm("_binary_rtc_html_start");
extern const uint8_t rtc_html_end[] asm("_binary_rtc_html_end");

static httpd_handle_t s_httpd;
static rtc_event_t s_events[EVENT_MAX];
static SemaphoreHandle_t s_events_mutex;
static TaskHandle_t s_event_task_handle;
static const char *TAG = "bsp_web";

static void set_no_cache(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
}

static esp_err_t send_text(httpd_req_t *req, const char *text)
{
    set_no_cache(req);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, text);
}

static bool parse_rate(const char *text, uint32_t *kbps)
{
    unsigned long rate = 0;
    if (!ws_scan_marker(text, "CAN Rate: ", "CAN Rate: %lu", &rate) || !bsp_can1_valid_rate((uint32_t)rate)) {
        return false;
    }
    *kbps = (uint32_t)rate;
    return true;
}

static bool parse_can_payload(const char *text, can_payload_t *payload)
{
    memset(payload, 0, sizeof(*payload));
    unsigned long id = 0;
    if (!ws_scan_marker(text, "CAN ID: ", "CAN ID: 0x%lx", &id)) {
        return false;
    }
    payload->can_id = (uint32_t)id;
    if (payload->can_id > 0x1FFFFFFF) {
        return false;
    }
    if (!ws_scan_marker(text, "CAN Extd: ", "CAN Extd: %hhu", &payload->extd)) {
        return false;
    }

    const char *marker = "CAN Data: ";
    const char *start = strstr(text, marker);
    if (!start) {
        marker = "Serial Data: ";
        start = strstr(text, marker);
    }
    const char *end = start ? strstr(start, "Web End") : NULL;
    if (!end && start) {
        end = strstr(start, "  Data Type: ");
    }
    if (!end && start) {
        end = strstr(start, "Data Type:");
    }
    if (!start || !end) {
        return false;
    }

    start += strlen(marker);
    size_t raw_len = (size_t)(end - start);
    while (raw_len > 0 && isspace((unsigned char)start[raw_len - 1])) {
        raw_len--;
    }

    unsigned int data_type = 1;
    bool serial_data = strcmp(marker, "Serial Data: ") == 0;
    if (serial_data) {
        ws_scan_marker(text, "Data Type: ", "Data Type: %u", &data_type);
    }
    if (serial_data && data_type == 0) {
        payload->len = raw_len;
        payload->data = malloc(payload->len ? payload->len : 1);
        if (!payload->data) {
            return false;
        }
        if (payload->len) {
            memcpy(payload->data, start, payload->len);
        }
        return true;
    }

    char *clean = malloc(raw_len + 2);
    if (!clean) {
        return false;
    }
    size_t n = 0;
    for (size_t i = 0; i < raw_len; i++) {
        if (isxdigit((unsigned char)start[i])) {
            clean[n++] = start[i];
        }
    }
    if (n & 1) {
        clean[n++] = '0';
    }

    payload->len = n / 2;
    payload->data = malloc(payload->len ? payload->len : 1);
    if (!payload->data) {
        free(clean);
        return false;
    }
    for (size_t i = 0; i < payload->len; i++) {
        payload->data[i] = ws_hex_byte(clean[i * 2], clean[i * 2 + 1]);
    }
    free(clean);
    return true;
}

static bool parse_rtc_config(const char *text, datetime_t *dt)
{
    int y, mo, d, w, h, mi, s;
    if (sscanf(text, "Date: %d/%d/%d", &y, &mo, &d) != 3) {
        return false;
    }
    if (!ws_scan_marker(text, "Week: ", "Week: %d", &w)) {
        return false;
    }
    const char *time_field = strstr(text, "Time: ");
    if (!time_field || sscanf(time_field, "Time: %d:%d:%d", &h, &mi, &s) != 3) {
        return false;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || w < 0 || w > 6 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) {
        return false;
    }
    *dt = (datetime_t){(uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)w, (uint8_t)h, (uint8_t)mi, (uint8_t)s};
    return true;
}

static bool parse_event_payload(const char *text, rtc_event_t *event)
{
    memset(event, 0, sizeof(*event));
    if (!parse_rtc_config(text, &event->time)) {
        return false;
    }
    if (!ws_scan_marker(text, "Serial Port: ", "Serial Port: %hhu", &event->port)) {
        return false;
    }
    if (!parse_can_payload(strstr(text, "CAN ID: "), &event->payload)) {
        return false;
    }
    uint8_t repeat = 0;
    if (!ws_scan_marker(text, "Cycle: ", "Cycle: %hhu", &repeat)) {
        repeat = 0;
    }
    event->repeat = (repetition_event_t)repeat;
    unsigned long duration = 0;
    if (ws_scan_marker(text, "Cycle Duration: ", "Cycle Duration: %lu", &duration)) {
        event->repeat_ms = (uint32_t)duration;
        if (event->repeat == REP_SECONDS) {
            event->repeat_ms *= 1000;
        }
        if (event->repeat == REP_MINUTES) {
            event->repeat_ms *= 60000;
        }
        if (event->repeat == REP_HOURS) {
            event->repeat_ms *= 3600000;
        }
    }
    ws_datetime_to_str(event->description, sizeof(event->description), event->time);
    size_t used = strlen(event->description);
    snprintf(event->description + used, sizeof(event->description) - used,
             "\\nCAN CH%u ID:0x%" PRIx32 " Len:%u", event->port == 0 || event->port == 2 ? 2 : 1,
             event->payload.can_id, (unsigned)event->payload.len);
    return true;
}

static void fire_event(rtc_event_t *event)
{
    bool ok;
    if (event->port == 0 || event->port == 2) {
        ok = bsp_xl2515_send(event->payload.can_id, event->payload.data, (uint8_t)event->payload.len, event->payload.extd);
    } else {
        ok = bsp_can1_send(event->payload.can_id, event->payload.data, (uint8_t)event->payload.len, event->payload.extd);
    }
    event->last_fire_us = esp_timer_get_time();
    uint8_t channel = event->port == 0 || event->port == 2 ? 2 : 1;
    if (ok) {
        ESP_LOGI(TAG, "RTC event fired: CAN CH%u id=0x%" PRIx32 " len=%u sent",
                 channel, event->payload.can_id, (unsigned)event->payload.len);
    } else {
        ESP_LOGW(TAG, "RTC event fired: CAN CH%u id=0x%" PRIx32 " len=%u send failed",
                 channel, event->payload.can_id, (unsigned)event->payload.len);
    }
}

static bool event_time_matches(const rtc_event_t *event)
{
    datetime_t now = bsp_rtc_now();
    switch (event->repeat) {
    case REP_EVERYDAY:
        return event->time.hour == now.hour && event->time.minute == now.minute && event->time.second == now.second;
    case REP_WEEKLY:
        return event->time.dotw == now.dotw && event->time.hour == now.hour &&
               event->time.minute == now.minute && event->time.second == now.second;
    case REP_MONTHLY:
        return event->time.day == now.day && event->time.hour == now.hour &&
               event->time.minute == now.minute && event->time.second == now.second;
    case REP_NONE:
        return event->time.year == now.year && event->time.month == now.month &&
               event->time.day == now.day && event->time.hour == now.hour &&
               event->time.minute == now.minute && event->time.second == now.second;
    default:
        return event->repeat_ms > 0 && (esp_timer_get_time() - event->last_fire_us) / 1000 >= event->repeat_ms;
    }
}

static void event_task(void *arg)
{
    (void)arg;

    uint8_t last_second = 255;
    while (1) {
        bool rtc_int_active = bsp_rtc_int_active();
        (void)bsp_rtc_refresh();
        datetime_t now = bsp_rtc_now();
        xSemaphoreTake(s_events_mutex, portMAX_DELAY);
        for (size_t i = 0; i < EVENT_MAX; i++) {
            if (!s_events[i].used) {
                continue;
            }
            bool interval_repeat = s_events[i].repeat >= REP_MILLISECONDS && s_events[i].repeat <= REP_HOURS;
            if (event_time_matches(&s_events[i]) && (interval_repeat || now.second != last_second)) {
                fire_event(&s_events[i]);
                if (s_events[i].repeat == REP_NONE) {
                    free(s_events[i].payload.data);
                    memset(&s_events[i], 0, sizeof(s_events[i]));
                }
            }
        }
        xSemaphoreGive(s_events_mutex);
        last_second = now.second;
        vTaskDelay(pdMS_TO_TICKS(rtc_int_active ? 20 : 100));
    }
}

static esp_err_t root_handler(httpd_req_t *req)
{
    set_no_cache(req);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)root_html_start, root_html_end - root_html_start);
}

static esp_err_t rtc_page_handler(httpd_req_t *req)
{
    set_no_cache(req);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)rtc_html_start, rtc_html_end - rtc_html_start);
}

static esp_err_t rate_config_handler(httpd_req_t *req)
{
    char json[80];
    snprintf(json, sizeof(json), "{\"can_rate\":%" PRIu32 ",\"can2_rate\":%" PRIu32 "}",
             bsp_can1_get_rate(), bsp_xl2515_get_rate());
    set_no_cache(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t log_handler(httpd_req_t *req, uint8_t channel)
{
    set_no_cache(req);
    httpd_resp_set_type(req, "application/json");
    return ws_log_send_json_and_clear(req, channel);
}

static esp_err_t can1_log_handler(httpd_req_t *req) { return log_handler(req, 1); }
static esp_err_t can2_log_handler(httpd_req_t *req) { return log_handler(req, 2); }
static esp_err_t clear_can1_handler(httpd_req_t *req) { ws_log_clear(1); return send_text(req, "OK"); }
static esp_err_t clear_can2_handler(httpd_req_t *req) { ws_log_clear(2); return send_text(req, "OK"); }

static esp_err_t can_send_handler(httpd_req_t *req, bool ch2)
{
    char text[HTTP_BUF_MAX] = {0};
    can_payload_t p;
    if (!ws_get_query_arg(req, "data", text, sizeof(text)) || !parse_can_payload(text, &p)) {
        return send_text(req, "ERROR");
    }
    if (p.len > UINT8_MAX) {
        free(p.data);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_text(req, "ERROR");
    }
    bool ok = ch2 ? bsp_xl2515_send(p.can_id, p.data, (uint8_t)p.len, p.extd) :
                    bsp_can1_send(p.can_id, p.data, (uint8_t)p.len, p.extd);
    free(p.data);
    httpd_resp_set_status(req, ok ? "200 OK" : "503 Service Unavailable");
    return send_text(req, ok ? "OK" : "BUSY");
}

static esp_err_t can1_send_handler(httpd_req_t *req) { return can_send_handler(req, false); }
static esp_err_t can2_send_handler(httpd_req_t *req) { return can_send_handler(req, true); }

static esp_err_t set_rate_handler(httpd_req_t *req, int channel)
{
    char text[HTTP_BUF_MAX] = {0};
    uint32_t kbps;
    if (!ws_get_query_arg(req, "data", text, sizeof(text)) || !parse_rate(text, &kbps)) {
        return send_text(req, "ERROR");
    }
    bool ok = true;
    if (channel == 1 || channel == 0) {
        ok = bsp_can1_set_rate(kbps) == ESP_OK && ok;
    }
    if (channel == 2 || channel == 0) {
        ok = bsp_xl2515_set_rate(kbps) && ok;
    }
    httpd_resp_set_status(req, ok ? "200 OK" : "503 Service Unavailable");
    return send_text(req, ok ? "OK" : "BUSY");
}

static esp_err_t set_can1_rate_handler(httpd_req_t *req) { return set_rate_handler(req, 1); }
static esp_err_t set_can2_rate_handler(httpd_req_t *req) { return set_rate_handler(req, 2); }
static esp_err_t set_all_rate_handler(httpd_req_t *req) { return set_rate_handler(req, 0); }

static esp_err_t set_rtc_handler(httpd_req_t *req)
{
    char text[HTTP_BUF_MAX] = {0};
    datetime_t t;
    if (!ws_get_query_arg(req, "data", text, sizeof(text)) || !parse_rtc_config(text, &t)) {
        return send_text(req, "ERROR");
    }
    bsp_rtc_set(t);
    return send_text(req, "OK");
}

static esp_err_t new_event_handler(httpd_req_t *req)
{
    char text[HTTP_BUF_MAX] = {0};
    rtc_event_t e;
    if (!ws_get_query_arg(req, "data", text, sizeof(text)) || !parse_event_payload(text, &e)) {
        return send_text(req, "ERROR");
    }
    if (e.payload.len > UINT8_MAX) {
        free(e.payload.data);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_text(req, "ERROR");
    }
    xSemaphoreTake(s_events_mutex, portMAX_DELAY);
    for (size_t i = 0; i < EVENT_MAX; i++) {
        if (!s_events[i].used) {
            e.used = true;
            s_events[i] = e;
            ESP_LOGI(TAG, "New RTC event added: slot=%u port=CAN CH%u id=0x%" PRIx32 " len=%u",
                     (unsigned)(i + 1),
                     s_events[i].port == 0 || s_events[i].port == 2 ? 2 : 1,
                     s_events[i].payload.can_id,
                     (unsigned)s_events[i].payload.len);
            xSemaphoreGive(s_events_mutex);
            return send_text(req, "OK");
        }
    }
    xSemaphoreGive(s_events_mutex);
    free(e.payload.data);
    httpd_resp_set_status(req, "503 Service Unavailable");
    return send_text(req, "BUSY");
}

static esp_err_t delete_event_handler(httpd_req_t *req)
{
    char value[16] = {0};
    if (!ws_get_query_arg(req, "id", value, sizeof(value))) {
        return send_text(req, "ERROR");
    }
    int id = atoi(value);
    xSemaphoreTake(s_events_mutex, portMAX_DELAY);
    if (id < 1 || id > EVENT_MAX || !s_events[id - 1].used) {
        xSemaphoreGive(s_events_mutex);
        httpd_resp_set_status(req, "404 Not Found");
        return send_text(req, "Event not found");
    }
    free(s_events[id - 1].payload.data);
    memset(&s_events[id - 1], 0, sizeof(s_events[id - 1]));
    xSemaphoreGive(s_events_mutex);
    return send_text(req, "OK");
}

static esp_err_t time_event_handler(httpd_req_t *req)
{
    datetime_t now = bsp_rtc_now();
    char time_str[80];
    char event_descriptions[EVENT_MAX][220];
    int count = 0;

    xSemaphoreTake(s_events_mutex, portMAX_DELAY);
    for (size_t i = 0; i < EVENT_MAX; i++) {
        if (s_events[i].used) {
            snprintf(event_descriptions[count], sizeof(event_descriptions[count]), "%s", s_events[i].description);
            count++;
        }
    }
    xSemaphoreGive(s_events_mutex);

    snprintf(time_str, sizeof(time_str), " %u/%u/%u  %s  %u:%u:%u",
             now.year, now.month, now.day,
             now.dotw < 7 ? ws_week_names[now.dotw] : "?",
             now.hour, now.minute, now.second);
    set_no_cache(req);
    httpd_resp_set_type(req, "application/json");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, "{\"time\":\""), TAG, "send time prefix failed");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, time_str), TAG, "send time failed");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, "\","), TAG, "send time suffix failed");
    for (int i = 0; i < count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "\"eventStr%d\":\"", i + 1);
        ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, key), TAG, "send event key failed");
        ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, event_descriptions[i]), TAG, "send event value failed");
        ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, "\","), TAG, "send event suffix failed");
    }
    char tail[32];
    snprintf(tail, sizeof(tail), "\"eventCount\":%d}", count);
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, tail), TAG, "send event count failed");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t register_uri(const char *uri, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t h = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = handler,
    };
    return httpd_register_uri_handler(s_httpd, &h);
}

static esp_err_t start_web(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 20;
    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &cfg), TAG, "http server start failed");
    ESP_RETURN_ON_ERROR(register_uri("/", root_handler), TAG, "register / failed");
    ESP_RETURN_ON_ERROR(register_uri("/getRateConfig", rate_config_handler), TAG, "register getRateConfig failed");
    ESP_RETURN_ON_ERROR(register_uri("/CANSetRate", set_can1_rate_handler), TAG, "register CANSetRate failed");
    ESP_RETURN_ON_ERROR(register_uri("/CAN2SetRate", set_can2_rate_handler), TAG, "register CAN2SetRate failed");
    ESP_RETURN_ON_ERROR(register_uri("/CANSetAllRate", set_all_rate_handler), TAG, "register CANSetAllRate failed");
    ESP_RETURN_ON_ERROR(register_uri("/CANSend", can1_send_handler), TAG, "register CANSend failed");
    ESP_RETURN_ON_ERROR(register_uri("/CAN2Send", can2_send_handler), TAG, "register CAN2Send failed");
    ESP_RETURN_ON_ERROR(register_uri("/getCANData", can1_log_handler), TAG, "register getCANData failed");
    ESP_RETURN_ON_ERROR(register_uri("/getCAN2Data", can2_log_handler), TAG, "register getCAN2Data failed");
    ESP_RETURN_ON_ERROR(register_uri("/clearCANData", clear_can1_handler), TAG, "register clearCANData failed");
    ESP_RETURN_ON_ERROR(register_uri("/clearCAN2Data", clear_can2_handler), TAG, "register clearCAN2Data failed");
    ESP_RETURN_ON_ERROR(register_uri("/RTC_Event", rtc_page_handler), TAG, "register RTC_Event failed");
    ESP_RETURN_ON_ERROR(register_uri("/SetRtcTime", set_rtc_handler), TAG, "register SetRtcTime failed");
    ESP_RETURN_ON_ERROR(register_uri("/NewEvent", new_event_handler), TAG, "register NewEvent failed");
    ESP_RETURN_ON_ERROR(register_uri("/DeleteEvent", delete_event_handler), TAG, "register DeleteEvent failed");
    ESP_RETURN_ON_ERROR(register_uri("/getTimeAndEvent", time_event_handler), TAG, "register getTimeAndEvent failed");
    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

static esp_err_t wifi_ap_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!ap) {
        return ESP_ERR_NO_MEM;
    }
    esp_netif_ip_info_t ip = {
        .ip.addr = ESP_IP4TOADDR(192, 168, 4, 1),
        .gw.addr = ESP_IP4TOADDR(192, 168, 4, 1),
        .netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0),
    };
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_stop(ap), TAG, "dhcp server stop failed");
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(ap, &ip), TAG, "set ap ip failed");
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_start(ap), TAG, "dhcp server start failed");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init failed");
    wifi_config_t cfg = {0};
    if (strlen(WS_AP_SSID) >= sizeof(cfg.ap.ssid) || strlen(WS_AP_PASS) >= sizeof(cfg.ap.password)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(cfg.ap.ssid, WS_AP_SSID, strlen(WS_AP_SSID) + 1);
    memcpy(cfg.ap.password, WS_AP_PASS, strlen(WS_AP_PASS) + 1);
    cfg.ap.ssid_len = strlen(WS_AP_SSID);
    cfg.ap.channel = 1;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (strlen(WS_AP_PASS) == 0) {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &cfg), TAG, "wifi config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    ESP_LOGI(TAG, "AP IP address: 192.168.4.1");
    return ESP_OK;
}

esp_err_t bsp_web_init(void)
{
    if (!s_events_mutex) {
        s_events_mutex = xSemaphoreCreateMutex();
        if (!s_events_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_RETURN_ON_ERROR(wifi_ap_init(), TAG, "wifi ap init failed");
    return start_web();
}

esp_err_t bsp_web_start_event_task(void)
{
    if (!s_event_task_handle) {
        BaseType_t ret = xTaskCreatePinnedToCore(event_task, "event_task", 4096, NULL, 3, &s_event_task_handle, 0);
        if (ret != pdPASS) {
            s_event_task_handle = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}
