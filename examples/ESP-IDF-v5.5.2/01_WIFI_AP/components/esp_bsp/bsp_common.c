#include "bsp_common.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

const char *const ws_week_names[7] = {"SUN", "Mon", "Tues", "Wed", "Thur", "Fri", "Sat"};

static SemaphoreHandle_t s_log_mutex;
static char *s_can_log[2];
static size_t s_can_log_len[2];

uint8_t ws_bcd_to_dec(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

uint8_t ws_dec_to_bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

bool ws_scan_marker(const char *text, const char *marker, const char *format, ...)
{
    const char *start = text ? strstr(text, marker) : NULL;
    if (!start) {
        return false;
    }

    va_list args;
    va_start(args, format);
    int ret = vsscanf(start, format, args);
    va_end(args);
    return ret == 1;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

uint8_t ws_hex_byte(char high, char low)
{
    int hi = hex_nibble(high);
    int lo = hex_nibble(low);
    if (hi < 0) {
        hi = 0;
    }
    if (lo < 0) {
        lo = 0;
    }
    return (uint8_t)((hi << 4) | lo);
}

void ws_datetime_to_str(char *out, size_t len, datetime_t t)
{
    snprintf(out, len, "%u.%u.%u  %u:%u:%u  %s",
             t.year, t.month, t.day, t.hour, t.minute, t.second,
             t.dotw < 7 ? ws_week_names[t.dotw] : "?");
}

datetime_t ws_datetime_add_seconds(datetime_t t, uint32_t add)
{
    t.second += add % 60;
    add /= 60;
    if (t.second >= 60) {
        t.second -= 60;
        add++;
    }
    t.minute += add % 60;
    add /= 60;
    if (t.minute >= 60) {
        t.minute -= 60;
        add++;
    }
    t.hour += add % 24;
    add /= 24;
    if (t.hour >= 24) {
        t.hour -= 24;
        add++;
    }
    if (add > 0) {
        t.day = (uint8_t)(((t.day - 1 + add) % 31) + 1);
        t.dotw = (uint8_t)((t.dotw + add) % 7);
    }
    return t;
}

void ws_url_decode(char *s)
{
    char *src = s;
    char *dst = s;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            *dst++ = (char)ws_hex_byte(src[1], src[2]);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

bool ws_get_query_arg(httpd_req_t *req, const char *name, char *out, size_t out_len)
{
    size_t len = httpd_req_get_url_query_len(req) + 1;
    if (len <= 1 || len > HTTP_BUF_MAX) {
        return false;
    }

    char *query = malloc(len);
    if (!query) {
        return false;
    }

    bool ok = httpd_req_get_url_query_str(req, query, len) == ESP_OK &&
              httpd_query_key_value(query, name, out, out_len) == ESP_OK;
    free(query);
    if (ok) {
        ws_url_decode(out);
    }
    return ok;
}

esp_err_t ws_logs_init(void)
{
    if (!s_log_mutex) {
        s_log_mutex = xSemaphoreCreateMutex();
    }
    for (size_t i = 0; i < 2; i++) {
        if (!s_can_log[i]) {
            s_can_log[i] = heap_caps_calloc(1, CAN_LOG_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_can_log[i]) {
                s_can_log[i] = calloc(1, CAN_LOG_MAX);
            }
        }
        if (!s_can_log[i]) {
            return ESP_ERR_NO_MEM;
        }
    }
    return s_log_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

void ws_log_clear(uint8_t channel)
{
    if (channel < 1 || channel > 2 || !s_can_log[channel - 1]) {
        return;
    }

    xSemaphoreTake(s_log_mutex, portMAX_DELAY);
    s_can_log[channel - 1][0] = '\0';
    s_can_log_len[channel - 1] = 0;
    xSemaphoreGive(s_log_mutex);
}

void ws_log_append(uint8_t channel, datetime_t now, uint32_t can_id, const uint8_t *data, uint8_t len, bool extd)
{
    if (channel < 1 || channel > 2 || !s_can_log[channel - 1]) {
        return;
    }

    char *log_buf = s_can_log[channel - 1];
    size_t *log_len = &s_can_log_len[channel - 1];
    const char *channel_name = channel == 1 ? "CAN CH1" : "CAN CH2";
    char ts[50];
    ws_datetime_to_str(ts, sizeof(ts), now);

    xSemaphoreTake(s_log_mutex, portMAX_DELAY);
    if (*log_len + 160 + len * 6 >= CAN_LOG_MAX) {
        *log_len = 0;
        log_buf[0] = '\0';
    }

    int wrote = snprintf(log_buf + *log_len, CAN_LOG_MAX - *log_len,
                         "%s :\n   %s ID:0x%" PRIx32 "   CAN Type:%s\n   ",
                         ts, channel_name, can_id, extd ? "Extended frames" : "Standard frames");
    if (wrote > 0) {
        *log_len += (size_t)wrote;
    }
    for (uint8_t i = 0; i < len && *log_len + 8 < CAN_LOG_MAX; i++) {
        wrote = snprintf(log_buf + *log_len, CAN_LOG_MAX - *log_len, "0x%02X ", data[i]);
        if (wrote > 0) {
            *log_len += (size_t)wrote;
        }
    }
    if (*log_len + 2 < CAN_LOG_MAX) {
        log_buf[(*log_len)++] = '\n';
        log_buf[*log_len] = '\0';
    }
    xSemaphoreGive(s_log_mutex);
}

esp_err_t ws_log_send_json_and_clear(httpd_req_t *req, uint8_t channel)
{
    if (channel < 1 || channel > 2) {
        return httpd_resp_sendstr(req, "[]");
    }

    char snapshot[CAN_LOG_MAX];
    char *log_buf = s_can_log[channel - 1];
    size_t *log_len = &s_can_log_len[channel - 1];
    xSemaphoreTake(s_log_mutex, portMAX_DELAY);
    if (!log_buf || *log_len == 0 || log_buf[0] == '\0') {
        xSemaphoreGive(s_log_mutex);
        return httpd_resp_sendstr(req, "[]");
    }
    snprintf(snapshot, sizeof(snapshot), "%s", log_buf);
    log_buf[0] = '\0';
    *log_len = 0;
    xSemaphoreGive(s_log_mutex);

    esp_err_t err = httpd_resp_sendstr_chunk(req, "[\"");
    if (err != ESP_OK) {
        return err;
    }
    for (const char *p = snapshot; *p; p++) {
        char esc[3] = {*p, 0, 0};
        if (*p == '\n') {
            err = httpd_resp_sendstr_chunk(req, "\\n");
        } else if (*p == '\r') {
            err = httpd_resp_sendstr_chunk(req, "\\r");
        } else if (*p == '"' || *p == '\\') {
            esc[0] = '\\';
            esc[1] = *p;
            err = httpd_resp_sendstr_chunk(req, esc);
        } else {
            err = httpd_resp_sendstr_chunk(req, esc);
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    err = httpd_resp_sendstr_chunk(req, "\"]");
    if (err != ESP_OK) {
        return err;
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}
