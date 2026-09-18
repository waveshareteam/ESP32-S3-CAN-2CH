#ifndef BSP_COMMON_H
#define BSP_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#define CAN_LOG_MAX 1000
#define EVENT_MAX 8
#define HTTP_BUF_MAX 1200

#define WS_I2C_SCL_IO 38
#define WS_I2C_SDA_IO 39
#define WS_RTC_INT_GPIO 40
#define WS_I2C_NUM 0
#define WS_I2C_FREQ_HZ 400000
#define WS_I2C_TIMEOUT_MS 1000

#define WS_CAN1_TX_GPIO 15
#define WS_CAN1_RX_GPIO 16

#define WS_XL2515_CS_GPIO 17
#define WS_XL2515_INT_GPIO 18
#define WS_XL2515_SCLK_GPIO 21
#define WS_XL2515_MOSI_GPIO 41
#define WS_XL2515_MISO_GPIO 42

#define WS_STA_SSID "YOUR_WIFI_SSID"
#define WS_STA_PASS "YOUR_WIFI_PASSWORD"
#define WS_STA_MAXIMUM_RETRY 10

/**
 * @brief Calendar time used by the local RTC and scheduler.
 */
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t dotw;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} datetime_t;

/**
 * @brief Supported scheduled event repetition modes.
 */
typedef enum {
    REP_NONE = 0,
    REP_MILLISECONDS = 1,
    REP_SECONDS = 2,
    REP_MINUTES = 3,
    REP_HOURS = 4,
    REP_EVERYDAY = 5,
    REP_WEEKLY = 6,
    REP_MONTHLY = 7,
} repetition_event_t;

/**
 * @brief Parsed CAN payload used by Web handlers and schedulers.
 */
typedef struct {
    uint32_t can_id;
    uint8_t extd;
    uint8_t *data;
    size_t len;
} can_payload_t;

extern const char *const ws_week_names[7];

/** @brief Convert a BCD byte to decimal. */
uint8_t ws_bcd_to_dec(uint8_t v);

/** @brief Convert a decimal byte to BCD. */
uint8_t ws_dec_to_bcd(uint8_t v);

/** @brief Find a marker in text and parse one value from it. */
bool ws_scan_marker(const char *text, const char *marker, const char *format, ...);

/** @brief Convert two hexadecimal characters into one byte. */
uint8_t ws_hex_byte(char high, char low);

/** @brief Format a datetime value into a caller-provided buffer. */
void ws_datetime_to_str(char *out, size_t len, datetime_t t);

/** @brief Add seconds to a lightweight datetime value. */
datetime_t ws_datetime_add_seconds(datetime_t t, uint32_t add);

/** @brief Decode a URL-encoded string in place. */
void ws_url_decode(char *s);

/** @brief Read and URL-decode a query argument from an HTTP request. */
bool ws_get_query_arg(httpd_req_t *req, const char *name, char *out, size_t out_len);

/** @brief Allocate CAN log buffers and synchronization primitives. */
esp_err_t ws_logs_init(void);

/** @brief Clear one CAN channel log. */
void ws_log_clear(uint8_t channel);

/** @brief Append one CAN frame to a channel log. */
void ws_log_append(uint8_t channel, datetime_t now, uint32_t can_id, const uint8_t *data, uint8_t len, bool extd);

/** @brief Send one channel log as JSON and clear it. */
esp_err_t ws_log_send_json_and_clear(httpd_req_t *req, uint8_t channel);

#endif
