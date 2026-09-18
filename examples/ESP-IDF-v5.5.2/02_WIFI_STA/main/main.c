#include "waveshare_twai_port.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_ERROR_CHECK(waveshare_main_wifi_sta_init());

    ESP_LOGI(TAG, "Connected to Wi-Fi network \"%s\". Open the STA IP address printed above.", WS_STA_SSID);
}
