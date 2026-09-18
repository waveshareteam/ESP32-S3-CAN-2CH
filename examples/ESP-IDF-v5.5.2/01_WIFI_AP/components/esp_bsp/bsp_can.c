#include "bsp_can.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bsp_rtc.h"
#include "driver/twai.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "bsp_can";

static SemaphoreHandle_t s_can1_mutex;
static TaskHandle_t s_can1_task_handle;
static bool s_can1_ready;
static uint32_t s_can1_bitrate_kbps = 250;

static void log_can1_frame(const char *direction, uint32_t can_id, const uint8_t *data, uint8_t len, bool extd, bool ok)
{
    char hex[3 * 8 + 1] = {0};
    size_t pos = 0;
    for (uint8_t i = 0; i < len && i < 8 && pos < sizeof(hex); i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, " %02X", data ? data[i] : 0);
    }
    ESP_LOGI(TAG, "CAN CH1 %s %s ID:0x%" PRIx32 " Type:%s Len:%u Data:%s",
             direction,
             ok ? "OK" : "FAILED",
             can_id,
             extd ? "Extended" : "Standard",
             len,
             hex);
}

static bool select_twai_timing(uint32_t kbps, twai_timing_config_t *cfg)
{
    switch (kbps) {
    case 25: *cfg = (twai_timing_config_t)TWAI_TIMING_CONFIG_25KBITS(); return true;
    case 50: *cfg = (twai_timing_config_t)TWAI_TIMING_CONFIG_50KBITS(); return true;
    case 100: *cfg = (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS(); return true;
    case 125: *cfg = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS(); return true;
    case 250: *cfg = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS(); return true;
    case 500: *cfg = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS(); return true;
    case 800: *cfg = (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS(); return true;
    case 1000: *cfg = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS(); return true;
    default: return false;
    }
}

bool bsp_can1_valid_rate(uint32_t kbps)
{
    twai_timing_config_t unused;
    return select_twai_timing(kbps, &unused);
}

uint32_t bsp_can1_get_rate(void)
{
    return s_can1_bitrate_kbps;
}

esp_err_t bsp_can1_init(uint32_t kbps)
{
    if (!s_can1_mutex) {
        s_can1_mutex = xSemaphoreCreateMutex();
        if (!s_can1_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    twai_timing_config_t timing;
    if (!select_twai_timing(kbps, &timing)) {
        return ESP_ERR_INVALID_ARG;
    }
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(WS_CAN1_TX_GPIO, WS_CAN1_RX_GPIO, TWAI_MODE_NORMAL);
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    ESP_RETURN_ON_ERROR(twai_driver_install(&general, &timing, &filter), TAG, "twai install failed");
    ESP_RETURN_ON_ERROR(twai_start(), TAG, "twai start failed");
    uint32_t alerts = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR |
                      TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_TX_FAILED;
    ESP_RETURN_ON_ERROR(twai_reconfigure_alerts(alerts, NULL), TAG, "twai alerts failed");
    s_can1_ready = true;
    s_can1_bitrate_kbps = kbps;
    return ESP_OK;
}

esp_err_t bsp_can1_set_rate(uint32_t kbps)
{
    if (!bsp_can1_valid_rate(kbps)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_can1_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_can1_mutex, portMAX_DELAY);
    if (s_can1_ready) {
        esp_err_t err = twai_stop();
        if (err != ESP_OK) {
            xSemaphoreGive(s_can1_mutex);
            return err;
        }
        err = twai_driver_uninstall();
        if (err != ESP_OK) {
            xSemaphoreGive(s_can1_mutex);
            return err;
        }
        s_can1_ready = false;
    }
    esp_err_t err = bsp_can1_init(kbps);
    xSemaphoreGive(s_can1_mutex);
    ws_log_clear(1);
    return err;
}

bool bsp_can1_send(uint32_t can_id, const uint8_t *data, uint8_t len, bool extd)
{
    if (!extd && can_id > 0x7FF) {
        extd = true;
    }
    if (extd && can_id > 0x1FFFFFFF) {
        return false;
    }

    uint16_t offset = 0;
    bool all_ok = true;
    do {
        uint8_t frame_len = len - offset;
        if (frame_len > 8) {
            frame_len = 8;
        }
        twai_message_t msg = {0};
        msg.identifier = can_id;
        msg.extd = extd;
        msg.data_length_code = frame_len;
        if (frame_len) {
            memcpy(msg.data, data + offset, frame_len);
        }

        xSemaphoreTake(s_can1_mutex, portMAX_DELAY);
        esp_err_t err = s_can1_ready ? twai_transmit(&msg, pdMS_TO_TICKS(200)) : ESP_ERR_INVALID_STATE;
        xSemaphoreGive(s_can1_mutex);
        log_can1_frame("TX", can_id, frame_len ? data + offset : NULL, frame_len, extd, err == ESP_OK);
        all_ok = all_ok && err == ESP_OK;
        offset += frame_len;
    } while (offset < len);
    return all_ok;
}

static void can1_task(void *arg)
{
    (void)arg;

    while (1) {
        xSemaphoreTake(s_can1_mutex, portMAX_DELAY);
        if (!s_can1_ready) {
            xSemaphoreGive(s_can1_mutex);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        uint32_t alerts = 0;
        esp_err_t ret = twai_read_alerts(&alerts, pdMS_TO_TICKS(50));
        if (ret == ESP_OK && (alerts & TWAI_ALERT_RX_DATA)) {
            twai_message_t msg;
            while (twai_receive(&msg, 0) == ESP_OK) {
                log_can1_frame("RX", msg.identifier, msg.data, msg.data_length_code, msg.extd, true);
                ws_log_append(1, bsp_rtc_now(), msg.identifier, msg.data, msg.data_length_code, msg.extd);
            }
        }
        if (ret == ESP_OK && (alerts & (TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_TX_FAILED))) {
            twai_status_info_t st;
            twai_get_status_info(&st);
            ESP_LOGW(TAG, "CAN CH1 alert 0x%08" PRIx32 ", bus_err=%" PRIu32 ", tx_failed=%" PRIu32,
                     alerts, st.bus_error_count, st.tx_failed_count);
        }
        xSemaphoreGive(s_can1_mutex);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t bsp_can1_start_task(void)
{
    if (!s_can1_task_handle) {
        BaseType_t ret = xTaskCreatePinnedToCore(can1_task, "can1_task", 4096, NULL, 4, &s_can1_task_handle, 0);
        if (ret != pdPASS) {
            s_can1_task_handle = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}
