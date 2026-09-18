#ifndef BSP_RTC_H
#define BSP_RTC_H

#include "bsp_common.h"

/**
 * @brief Initialize the I2C bus and PCF85063 RTC.
 *
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t bsp_rtc_init(void);

/**
 * @brief Set the RTC and software time base.
 *
 * @param t New date and time.
 */
void bsp_rtc_set(datetime_t t);

/**
 * @brief Get the latest software time.
 *
 * @return Current date and time snapshot.
 */
datetime_t bsp_rtc_now(void);

/**
 * @brief Refresh the software RTC snapshot from the PCF85063 if possible.
 *
 * @return ESP_OK if the external RTC was read, otherwise ESP_FAIL after using the uptime fallback.
 */
esp_err_t bsp_rtc_refresh(void);

/**
 * @brief Check whether the PCF85063 INT pin is asserted.
 *
 * @return true when the active-low INT pin reads low.
 */
bool bsp_rtc_int_active(void);

/**
 * @brief Start the RTC polling task.
 *
 * @return ESP_OK if the task is running, or an ESP-IDF error code.
 */
esp_err_t bsp_rtc_start_task(void);

#endif
