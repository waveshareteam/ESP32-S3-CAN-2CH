#include "bsp_xl2515.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bsp_rtc.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define XL_CANSTAT 0x0E
#define XL_CANCTRL 0x0F
#define XL_CNF3 0x28
#define XL_CNF2 0x29
#define XL_CNF1 0x2A
#define XL_CANINTE 0x2B
#define XL_CANINTF 0x2C
#define XL_EFLG 0x2D
#define XL_TXB0CTRL 0x30
#define XL_TXB0SIDH 0x31
#define XL_TXB0DLC 0x35
#define XL_TXB0D0 0x36
#define XL_RXB0CTRL 0x60
#define XL_RXB0SIDH 0x61
#define XL_RXB0DLC 0x65
#define XL_RXB0D0 0x66
#define XL_RXB1CTRL 0x70
#define XL_RXB1SIDH 0x71
#define XL_RXB1DLC 0x75
#define XL_RXB1D0 0x76
#define XL_RXM0SIDH 0x20
#define XL_RXM1SIDH 0x24
#define XL_RXF0SIDH 0x00
#define XL_RXF1SIDH 0x04
#define XL_RXF2SIDH 0x08
#define XL_RXF3SIDH 0x10
#define XL_RXF4SIDH 0x14
#define XL_RXF5SIDH 0x18

#define XL_CAN_RESET 0xC0
#define XL_CAN_READ 0x03
#define XL_CAN_WRITE 0x02
#define XL_CAN_BIT_MODIFY 0x05
#define XL_REQOP 0xE0
#define XL_REQOP_CONFIG 0x80
#define XL_REQOP_NORMAL 0x00
#define XL_OPMODE_CONFIG 0x80
#define XL_OPMODE_NORMAL 0x00
#define XL_CLKOUT_DISABLED 0x00
#define XL_RXM_RCV_ALL 0x60
#define XL_BUKT_ROLLOVER 0x04
#define XL_RX0IE 0x01
#define XL_RX1IE 0x02
#define XL_ERRIE 0x20
#define XL_RX0IF 0x01
#define XL_RX1IF 0x02
#define XL_TX0IF 0x04
#define XL_TX1IF 0x08
#define XL_TX2IF 0x10
#define XL_ERRIF 0x20
#define XL_MERRF 0x80
#define XL_TXREQ 0x08
#define XL_TXREQ_SET 0x08
#define XL_EXIDE_SET 0x08

static const char *TAG = "bsp_xl2515";

static SemaphoreHandle_t s_xl_mutex;
static TaskHandle_t s_can2_task_handle;
static spi_device_handle_t s_xl_spi;
static bool s_xl_ready;
static uint32_t s_can2_bitrate_kbps = 250;

static void log_can2_frame(const char *direction, uint32_t can_id, const uint8_t *data, uint8_t len, bool extd, bool ok)
{
    char hex[3 * 8 + 1] = {0};
    size_t pos = 0;
    for (uint8_t i = 0; i < len && i < 8 && pos < sizeof(hex); i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, " %02X", data ? data[i] : 0);
    }
    ESP_LOGI(TAG, "CAN CH2 %s %s ID:0x%" PRIx32 " Type:%s Len:%u Data:%s",
             direction,
             ok ? "OK" : "FAILED",
             can_id,
             extd ? "Extended" : "Standard",
             len,
             hex);
}

static esp_err_t xl_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(s_xl_spi, &t);
}

static esp_err_t xl_read_reg(uint8_t reg, uint8_t *value)
{
    uint8_t tx[3] = {XL_CAN_READ, reg, 0};
    uint8_t rx[3] = {0};
    esp_err_t err = xl_transfer(tx, rx, sizeof(tx));
    if (err != ESP_OK) {
        return err;
    }
    *value = rx[2];
    return ESP_OK;
}

static esp_err_t xl_read_regs(uint8_t reg, uint8_t *data, uint8_t len)
{
    uint8_t tx[12] = {0};
    uint8_t rx[12] = {0};
    if (len + 2 > sizeof(tx)) {
        return ESP_ERR_INVALID_SIZE;
    }
    tx[0] = XL_CAN_READ;
    tx[1] = reg;
    ESP_RETURN_ON_ERROR(xl_transfer(tx, rx, len + 2), TAG, "xl read regs failed");
    memcpy(data, &rx[2], len);
    return ESP_OK;
}

static esp_err_t xl_write_regs(uint8_t reg, const uint8_t *data, uint8_t len)
{
    uint8_t tx[12] = {0};
    if (len + 2 > sizeof(tx)) {
        return ESP_ERR_INVALID_SIZE;
    }
    tx[0] = XL_CAN_WRITE;
    tx[1] = reg;
    memcpy(&tx[2], data, len);
    return xl_transfer(tx, NULL, len + 2);
}

static esp_err_t xl_write_reg(uint8_t reg, uint8_t value)
{
    return xl_write_regs(reg, &value, 1);
}

static esp_err_t xl_bit_modify(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t tx[4] = {XL_CAN_BIT_MODIFY, reg, mask, value};
    return xl_transfer(tx, NULL, sizeof(tx));
}

static esp_err_t xl_reset(void)
{
    uint8_t cmd = XL_CAN_RESET;
    return xl_transfer(&cmd, NULL, 1);
}

static bool xl_rate_regs(uint32_t kbps, uint8_t regs[3])
{
    static const uint8_t table[8][3] = {
        {0x13, 0xA4, 0x04}, {0x09, 0xA4, 0x04}, {0x04, 0x9E, 0x03}, {0x03, 0x9E, 0x03},
        {0x01, 0x1E, 0x03}, {0x00, 0x9E, 0x03}, {0x00, 0x92, 0x02}, {0x00, 0x82, 0x02},
    };
    int idx = -1;
    switch (kbps) {
    case 25: idx = 0; break;
    case 50: idx = 1; break;
    case 100: idx = 2; break;
    case 125: idx = 3; break;
    case 250: idx = 4; break;
    case 500: idx = 5; break;
    case 800: idx = 6; break;
    case 1000: idx = 7; break;
    default: return false;
    }
    memcpy(regs, table[idx], 3);
    return true;
}

static esp_err_t xl_write_id(uint8_t reg, uint32_t can_id, bool extd)
{
    uint8_t buf[4] = {0};
    if (extd) {
        can_id &= 0x1FFFFFFF;
        buf[0] = (uint8_t)(can_id >> 21);
        buf[1] = (uint8_t)(((can_id >> 13) & 0xE0) | XL_EXIDE_SET | ((can_id >> 16) & 0x03));
        buf[2] = (uint8_t)(can_id >> 8);
        buf[3] = (uint8_t)can_id;
    } else {
        can_id &= 0x7FF;
        buf[0] = (uint8_t)(can_id >> 3);
        buf[1] = (uint8_t)((can_id & 0x07) << 5);
    }
    return xl_write_regs(reg, buf, sizeof(buf));
}

static esp_err_t xl_read_id(uint8_t reg, uint32_t *can_id, bool *extd)
{
    uint8_t buf[4] = {0};
    ESP_RETURN_ON_ERROR(xl_read_regs(reg, buf, sizeof(buf)), TAG, "xl read id failed");
    if (buf[1] & XL_EXIDE_SET) {
        *extd = true;
        *can_id = ((uint32_t)buf[0] << 21) | ((uint32_t)(buf[1] & 0xE0) << 13) |
                  ((uint32_t)(buf[1] & 0x03) << 16) | ((uint32_t)buf[2] << 8) | buf[3];
    } else {
        *extd = false;
        *can_id = ((uint32_t)buf[0] << 3) | (buf[1] >> 5);
    }
    return ESP_OK;
}

static esp_err_t xl_configure(uint32_t kbps)
{
    uint8_t regs[3];
    if (!xl_rate_regs(kbps, regs)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_xl_mutex, portMAX_DELAY);
    esp_err_t err = xl_write_reg(XL_CANCTRL, XL_REQOP_CONFIG | XL_CLKOUT_DISABLED);
    if (err != ESP_OK) {
        xSemaphoreGive(s_xl_mutex);
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t canstat = 0;
    err = xl_read_reg(XL_CANSTAT, &canstat);
    if (err != ESP_OK) {
        xSemaphoreGive(s_xl_mutex);
        return err;
    }
    if ((canstat & XL_REQOP) != XL_OPMODE_CONFIG) {
        xSemaphoreGive(s_xl_mutex);
        ESP_LOGW(TAG, "XL2515 did not enter config mode");
        return ESP_ERR_INVALID_STATE;
    }
    err = xl_write_reg(XL_CNF1, regs[0]);
    err = err == ESP_OK ? xl_write_reg(XL_CNF2, regs[1]) : err;
    err = err == ESP_OK ? xl_write_reg(XL_CNF3, regs[2]) : err;
    err = err == ESP_OK ? xl_write_reg(XL_RXB0CTRL, XL_RXM_RCV_ALL | XL_BUKT_ROLLOVER) : err;
    err = err == ESP_OK ? xl_write_reg(XL_RXB1CTRL, XL_RXM_RCV_ALL) : err;
    uint8_t zero[4] = {0};
    uint8_t regs4[] = {XL_RXM0SIDH, XL_RXM1SIDH, XL_RXF0SIDH, XL_RXF1SIDH, XL_RXF2SIDH, XL_RXF3SIDH, XL_RXF4SIDH, XL_RXF5SIDH};
    for (size_t i = 0; i < sizeof(regs4); i++) {
        err = err == ESP_OK ? xl_write_regs(regs4[i], zero, sizeof(zero)) : err;
    }
    err = err == ESP_OK ? xl_write_reg(XL_TXB0CTRL, 0x03) : err;
    err = err == ESP_OK ? xl_write_reg(XL_CANINTF, 0x00) : err;
    err = err == ESP_OK ? xl_write_reg(XL_EFLG, 0x00) : err;
    err = err == ESP_OK ? xl_write_reg(XL_CANINTE, XL_RX0IE | XL_RX1IE | XL_ERRIE) : err;
    err = err == ESP_OK ? xl_write_reg(XL_CANCTRL, XL_REQOP_NORMAL | XL_CLKOUT_DISABLED) : err;
    if (err != ESP_OK) {
        xSemaphoreGive(s_xl_mutex);
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    err = xl_read_reg(XL_CANSTAT, &canstat);
    xSemaphoreGive(s_xl_mutex);
    if (err != ESP_OK) {
        return err;
    }
    return (canstat & XL_REQOP) == XL_OPMODE_NORMAL ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t bsp_xl2515_init(uint32_t kbps)
{
    if (!s_xl_mutex) {
        s_xl_mutex = xSemaphoreCreateMutex();
        if (!s_xl_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = WS_XL2515_MOSI_GPIO,
        .miso_io_num = WS_XL2515_MISO_GPIO,
        .sclk_io_num = WS_XL2515_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = WS_XL2515_CS_GPIO,
        .queue_size = 1,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &devcfg, &s_xl_spi), TAG, "xl spi add failed");

    gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << WS_XL2515_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "xl int gpio config failed");

    xSemaphoreTake(s_xl_mutex, portMAX_DELAY);
    esp_err_t reset_err = xl_reset();
    xSemaphoreGive(s_xl_mutex);
    ESP_RETURN_ON_ERROR(reset_err, TAG, "xl reset failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    s_can2_bitrate_kbps = kbps;
    esp_err_t config_err = xl_configure(s_can2_bitrate_kbps);
    s_xl_ready = config_err == ESP_OK;
    ESP_LOGI(TAG, "XL2515 %s", s_xl_ready ? "initialized" : "initialization failed");
    return config_err;
}

bool bsp_xl2515_set_rate(uint32_t kbps)
{
    bool ok = xl_configure(kbps) == ESP_OK;
    if (ok) {
        s_can2_bitrate_kbps = kbps;
        ws_log_clear(2);
    }
    return ok;
}

uint32_t bsp_xl2515_get_rate(void)
{
    return s_can2_bitrate_kbps;
}

bool bsp_xl2515_send(uint32_t can_id, const uint8_t *data, uint8_t len, bool extd)
{
    if (!s_xl_ready) {
        return false;
    }
    if (!extd && can_id > 0x7FF) {
        extd = true;
    }
    if (extd && can_id > 0x1FFFFFFF) {
        ESP_LOGW(TAG, "invalid extended CAN ID: 0x%" PRIx32, can_id);
        return false;
    }

    uint16_t offset = 0;
    bool all_ok = true;
    do {
        uint8_t frame_len = len - offset;
        if (frame_len > 8) {
            frame_len = 8;
        }
        xSemaphoreTake(s_xl_mutex, portMAX_DELAY);
        uint8_t waits = 0;
        uint8_t txb0ctrl = 0;
        esp_err_t err = xl_read_reg(XL_TXB0CTRL, &txb0ctrl);
        while (err == ESP_OK && (txb0ctrl & XL_TXREQ) && waits++ < 50) {
            xSemaphoreGive(s_xl_mutex);
            vTaskDelay(pdMS_TO_TICKS(1));
            xSemaphoreTake(s_xl_mutex, portMAX_DELAY);
            err = xl_read_reg(XL_TXB0CTRL, &txb0ctrl);
        }
        bool ok = err == ESP_OK && (txb0ctrl & XL_TXREQ) == 0;
        if (ok) {
            err = xl_write_id(XL_TXB0SIDH, can_id, extd);
            err = err == ESP_OK ? xl_write_reg(XL_TXB0DLC, frame_len & 0x0F) : err;
            if (frame_len) {
                err = err == ESP_OK ? xl_write_regs(XL_TXB0D0, data + offset, frame_len) : err;
            }
            err = err == ESP_OK ? xl_bit_modify(XL_TXB0CTRL, XL_TXREQ, XL_TXREQ_SET) : err;
            ok = err == ESP_OK;
        }
        xSemaphoreGive(s_xl_mutex);
        log_can2_frame("TX", can_id, frame_len ? data + offset : NULL, frame_len, extd, ok);
        all_ok = all_ok && ok;
        offset += frame_len;
    } while (offset < len);
    return all_ok;
}

static bool xl_recv_one(uint32_t *can_id, uint8_t *data, uint8_t *len, bool *extd)
{
    if (!s_xl_ready || gpio_get_level(WS_XL2515_INT_GPIO) == 1) {
        return false;
    }
    xSemaphoreTake(s_xl_mutex, portMAX_DELAY);
    uint8_t intf = 0;
    esp_err_t err = xl_read_reg(XL_CANINTF, &intf);
    if (err != ESP_OK) {
        xSemaphoreGive(s_xl_mutex);
        return false;
    }
    uint8_t id_reg = 0, dlc_reg = 0, data_reg = 0, clear = 0;
    if (intf & XL_RX0IF) {
        id_reg = XL_RXB0SIDH; dlc_reg = XL_RXB0DLC; data_reg = XL_RXB0D0; clear = XL_RX0IF;
    } else if (intf & XL_RX1IF) {
        id_reg = XL_RXB1SIDH; dlc_reg = XL_RXB1DLC; data_reg = XL_RXB1D0; clear = XL_RX1IF;
    } else {
        if (intf & (XL_TX0IF | XL_TX1IF | XL_TX2IF | XL_ERRIF | XL_MERRF)) {
            (void)xl_bit_modify(XL_CANINTF, XL_TX0IF | XL_TX1IF | XL_TX2IF | XL_ERRIF | XL_MERRF, 0);
        }
        xSemaphoreGive(s_xl_mutex);
        return false;
    }
    err = xl_read_id(id_reg, can_id, extd);
    uint8_t dlc = 0;
    err = err == ESP_OK ? xl_read_reg(dlc_reg, &dlc) : err;
    if (err != ESP_OK) {
        xSemaphoreGive(s_xl_mutex);
        return false;
    }
    *len = dlc & 0x0F;
    if (*len > 8) {
        *len = 8;
    }
    err = xl_read_regs(data_reg, data, *len);
    err = err == ESP_OK ? xl_bit_modify(XL_CANINTF, clear, 0) : err;
    xSemaphoreGive(s_xl_mutex);
    return err == ESP_OK;
}

static void can2_task(void *arg)
{
    (void)arg;

    while (1) {
        uint32_t id;
        uint8_t data[8];
        uint8_t len;
        bool extd;
        uint8_t count = 0;
        while (count++ < 16 && xl_recv_one(&id, data, &len, &extd)) {
            log_can2_frame("RX", id, data, len, extd, true);
            ws_log_append(2, bsp_rtc_now(), id, data, len, extd);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t bsp_xl2515_start_task(void)
{
    if (!s_can2_task_handle) {
        BaseType_t ret = xTaskCreatePinnedToCore(can2_task, "can2_task", 4096, NULL, 3, &s_can2_task_handle, 0);
        if (ret != pdPASS) {
            s_can2_task_handle = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}
