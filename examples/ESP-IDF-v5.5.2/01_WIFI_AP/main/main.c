#include "waveshare_twai_port.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_ERROR_CHECK(waveshare_main_wifi_ap_init());

    ESP_LOGI(TAG, "Connect to Wi-Fi network \"%s\" and open http://192.168.4.1", WS_AP_SSID);
}
