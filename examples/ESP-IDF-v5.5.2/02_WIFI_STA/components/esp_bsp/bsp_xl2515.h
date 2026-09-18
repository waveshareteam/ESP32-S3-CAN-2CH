#ifndef BSP_XL2515_H
#define BSP_XL2515_H

#include "bsp_common.h"

/**
 * @brief Initialize the XL2515 external CAN controller.
 *
 * @param kbps Bit rate in kbit/s.
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t bsp_xl2515_init(uint32_t kbps);

/**
 * @brief Reconfigure the XL2515 CAN bit rate.
 *
 * @param kbps Bit rate in kbit/s.
 * @return true on success.
 */
bool bsp_xl2515_set_rate(uint32_t kbps);

/**
 * @brief Get the currently configured XL2515 bit rate.
 *
 * @return Bit rate in kbit/s.
 */
uint32_t bsp_xl2515_get_rate(void);

/**
 * @brief Send a CAN payload through the XL2515 controller.
 *
 * @param can_id 11-bit or 29-bit CAN identifier.
 * @param data Payload buffer.
 * @param len Payload length in bytes.
 * @param extd true to send an extended frame.
 * @return true if every frame was queued successfully.
 */
bool bsp_xl2515_send(uint32_t can_id, const uint8_t *data, uint8_t len, bool extd);

/**
 * @brief Start the XL2515 receive task.
 *
 * @return ESP_OK if the task is running, or an ESP-IDF error code.
 */
esp_err_t bsp_xl2515_start_task(void);

#endif
