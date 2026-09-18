#ifndef WAVESHARE_TWAI_PORT_H
#define WAVESHARE_TWAI_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_common.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Wi-Fi STA, Web UI, RTC, and both CAN channels.
 *
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t waveshare_main_wifi_sta_init(void);

/**
 * @brief Compatibility wrapper for the previous AP-named entry point.
 *
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t waveshare_main_wifi_ap_init(void);

/**
 * @brief Compatibility wrapper for older examples.
 *
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t waveshare_twai_init(void);

/**
 * @brief Compatibility placeholder transmit function for older examples.
 *
 * @return ESP_OK after a short delay.
 */
esp_err_t waveshare_twai_transmit(void);

#ifdef __cplusplus
}
#endif

#endif
