#ifndef BSP_WEB_H
#define BSP_WEB_H

#include "esp_err.h"

/**
 * @brief Initialize Wi-Fi AP mode and start the HTTP server.
 *
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t bsp_web_init(void);

/**
 * @brief Start the scheduled CAN event task.
 *
 * @return ESP_OK if the task is running, or an ESP-IDF error code.
 */
esp_err_t bsp_web_start_event_task(void);

#endif
