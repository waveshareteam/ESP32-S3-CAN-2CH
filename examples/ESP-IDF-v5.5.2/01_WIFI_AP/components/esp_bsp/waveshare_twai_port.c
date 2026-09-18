#include "waveshare_twai_port.h"

#include "bsp_can.h"
#include "bsp_common.h"
#include "bsp_rtc.h"
#include "bsp_web.h"
#include "bsp_xl2515.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "waveshare_twai";

esp_err_t waveshare_main_wifi_ap_init(void)
{
    ESP_RETURN_ON_ERROR(ws_logs_init(), TAG, "log init failed");
    ESP_RETURN_ON_ERROR(bsp_rtc_init(), TAG, "rtc init failed");
    ESP_RETURN_ON_ERROR(bsp_can1_init(250), TAG, "can1 init failed");
    ESP_RETURN_ON_ERROR(bsp_xl2515_init(250), TAG, "can2 init failed");
    ESP_RETURN_ON_ERROR(bsp_web_init(), TAG, "web init failed");

    ESP_RETURN_ON_ERROR(bsp_rtc_start_task(), TAG, "rtc task start failed");
    ESP_RETURN_ON_ERROR(bsp_can1_start_task(), TAG, "can1 task start failed");
    ESP_RETURN_ON_ERROR(bsp_xl2515_start_task(), TAG, "can2 task start failed");
    ESP_RETURN_ON_ERROR(bsp_web_start_event_task(), TAG, "event task start failed");
    return ESP_OK;
}

esp_err_t waveshare_twai_init(void)
{
    return waveshare_main_wifi_ap_init();
}

esp_err_t waveshare_twai_transmit(void)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    return ESP_OK;
}
