#ifndef BSP_CAN_H
#define BSP_CAN_H

#include "bsp_common.h"

/**
 * @brief Initialize the on-chip TWAI controller used as CAN channel 1.
 *
 * @param kbps Bit rate in kbit/s. Supported values are 25, 50, 100, 125, 250,
 *             500, 800, and 1000.
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t bsp_can1_init(uint32_t kbps);

/**
 * @brief Reconfigure CAN channel 1 bit rate.
 *
 * @param kbps Bit rate in kbit/s.
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t bsp_can1_set_rate(uint32_t kbps);

/**
 * @brief Get the currently configured CAN channel 1 bit rate.
 *
 * @return Bit rate in kbit/s.
 */
uint32_t bsp_can1_get_rate(void);

/**
 * @brief Check whether a CAN channel 1 bit rate is supported.
 *
 * @param kbps Bit rate in kbit/s.
 * @return true if supported, false otherwise.
 */
bool bsp_can1_valid_rate(uint32_t kbps);

/**
 * @brief Send a CAN payload through channel 1.
 *
 * Payloads longer than one classic CAN frame are split into 8-byte frames.
 *
 * @param can_id 11-bit or 29-bit CAN identifier.
 * @param data Payload buffer.
 * @param len Payload length in bytes.
 * @param extd true to send an extended frame.
 * @return true if every frame was queued successfully.
 */
bool bsp_can1_send(uint32_t can_id, const uint8_t *data, uint8_t len, bool extd);

/**
 * @brief Start the CAN channel 1 receive task.
 *
 * @return ESP_OK if the task is running, or an ESP-IDF error code.
 */
esp_err_t bsp_can1_start_task(void);

#endif
