#include "bsp_rtc.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define PCF85063_ADDR 0x51

static const char *TAG = "bsp_rtc";

static bool s_i2c_ready;
static datetime_t s_datetime_now = {2026, 6, 29, 1, 0, 0, 0};
static int64_t s_datetime_base_us;
static datetime_t s_datetime_base;
static TaskHandle_t s_rtc_task_handle;

static void datetime_set_base(datetime_t t)
{
    s_datetime_now = t;
    s_datetime_base = t;
    s_datetime_base_us = esp_timer_get_time();
}

static esp_err_t i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[16];
    if (len + 1 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = reg;
    memcpy(&buf[1], data, len);
    return i2c_master_write_to_device(WS_I2C_NUM, addr, buf, len + 1, pdMS_TO_TICKS(WS_I2C_TIMEOUT_MS));
}

static esp_err_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(WS_I2C_NUM, addr, &reg, 1, data, len, pdMS_TO_TICKS(WS_I2C_TIMEOUT_MS));
}

static esp_err_t ws_i2c_init(void)
{
    if (s_i2c_ready) {
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = WS_I2C_SDA_IO,
        .scl_io_num = WS_I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = WS_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(WS_I2C_NUM, &conf), TAG, "i2c config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(WS_I2C_NUM, conf.mode, 0, 0, 0), TAG, "i2c install failed");
    s_i2c_ready = true;

    uint8_t v = 0x01;
    esp_err_t err = i2c_master_write_to_device(WS_I2C_NUM, 0x24, &v, 1, pdMS_TO_TICKS(WS_I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c write to 0x24 failed: %s", esp_err_to_name(err));
    }
    v = 0x20;
    err = i2c_master_write_to_device(WS_I2C_NUM, 0x38, &v, 1, pdMS_TO_TICKS(WS_I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c write to 0x38 failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

static esp_err_t rtc_int_gpio_init(void)
{
    gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << WS_RTC_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&int_cfg);
}

static bool rtc_read(datetime_t *t)
{
    uint8_t buf[7];
    if (i2c_read_reg(PCF85063_ADDR, 0x04, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }
    t->second = ws_bcd_to_dec(buf[0] & 0x7F);
    t->minute = ws_bcd_to_dec(buf[1] & 0x7F);
    t->hour = ws_bcd_to_dec(buf[2] & 0x3F);
    t->day = ws_bcd_to_dec(buf[3] & 0x3F);
    t->dotw = ws_bcd_to_dec(buf[4] & 0x07);
    t->month = ws_bcd_to_dec(buf[5] & 0x1F);
    t->year = (uint16_t)(1970 + ws_bcd_to_dec(buf[6]));
    return t->month >= 1 && t->month <= 12 && t->day >= 1 && t->day <= 31;
}

void bsp_rtc_set(datetime_t t)
{
    datetime_set_base(t);
    uint8_t buf[7] = {
        ws_dec_to_bcd(t.second),
        ws_dec_to_bcd(t.minute),
        ws_dec_to_bcd(t.hour),
        ws_dec_to_bcd(t.day),
        ws_dec_to_bcd(t.dotw),
        ws_dec_to_bcd(t.month),
        ws_dec_to_bcd((uint8_t)(t.year >= 1970 ? t.year - 1970 : t.year)),
    };
    if (i2c_write_reg(PCF85063_ADDR, 0x04, buf, sizeof(buf)) == ESP_OK) {
        ESP_LOGI(TAG, "RTC updated");
    } else {
        ESP_LOGW(TAG, "RTC write failed, software time updated only");
    }
}

datetime_t bsp_rtc_now(void)
{
    return s_datetime_now;
}

esp_err_t bsp_rtc_refresh(void)
{
    datetime_t t;
    if (rtc_read(&t)) {
        datetime_set_base(t);
        return ESP_OK;
    }

    uint32_t elapsed = (uint32_t)((esp_timer_get_time() - s_datetime_base_us) / 1000000);
    s_datetime_now = ws_datetime_add_seconds(s_datetime_base, elapsed);
    return ESP_FAIL;
}

bool bsp_rtc_int_active(void)
{
    return gpio_get_level(WS_RTC_INT_GPIO) == 0;
}

static void rtc_task(void *arg)
{
    (void)arg;

    while (1) {
        (void)bsp_rtc_refresh();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t bsp_rtc_init(void)
{
    ESP_RETURN_ON_ERROR(rtc_int_gpio_init(), TAG, "rtc int gpio config failed");
    ESP_RETURN_ON_ERROR(ws_i2c_init(), TAG, "i2c init failed");
    if (!rtc_read(&s_datetime_now)) {
        ESP_LOGW(TAG, "PCF85063 RTC not readable, using uptime clock display");
    }
    datetime_set_base(s_datetime_now);
    return ESP_OK;
}

esp_err_t bsp_rtc_start_task(void)
{
    if (!s_rtc_task_handle) {
        BaseType_t ret = xTaskCreatePinnedToCore(rtc_task, "rtc_task", 3072, NULL, 3, &s_rtc_task_handle, 0);
        if (ret != pdPASS) {
            s_rtc_task_handle = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}
