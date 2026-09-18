#include <Arduino.h>
#include "WS_XL2515.h"
#include "WS_GPIO.h"
#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define XL2515_SPI_CLOCK_HZ 10000000UL
#define XL2515_SPI_MODE SPI_MODE0

static SPISettings xl2515_spi_settings(XL2515_SPI_CLOCK_HZ, MSBFIRST, XL2515_SPI_MODE);
static SemaphoreHandle_t xl2515_spi_mutex = NULL;
static volatile bool g_xl2515_recv_flag = false;

static bool xl2515_rate_from_kbps(uint32_t bitrate_kbps, xl2515_rate_kbps_t *rate)
{
  switch (bitrate_kbps) {
    case 25: *rate = KBPS25; return true;
    case 50: *rate = KBPS50; return true;
    case 100: *rate = KBPS100; return true;
    case 125: *rate = KBPS125; return true;
    case 250: *rate = KBPS250; return true;
    case 500: *rate = KBPS500; return true;
    case 800: *rate = KBPS800; return true;
    case 1000: *rate = KBPS1000; return true;
    default: return false;
  }
}

static bool xl2515_take(void)
{
  if (xl2515_spi_mutex == NULL) {
    return true;
  }
  return xSemaphoreTake(xl2515_spi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void xl2515_give(void)
{
  if (xl2515_spi_mutex != NULL) {
    xSemaphoreGive(xl2515_spi_mutex);
  }
}

static void xl2515_select(void)
{
  SPI.beginTransaction(xl2515_spi_settings);
  digitalWrite(XL2515_CS, LOW);
}

static void xl2515_unselect(void)
{
  digitalWrite(XL2515_CS, HIGH);
  SPI.endTransaction();
}

static void xl2515_write_reg_unlocked(uint8_t reg, const uint8_t *data, uint8_t len)
{
  xl2515_select();
  SPI.transfer(CAN_WRITE);
  SPI.transfer(reg);
  for (uint8_t i = 0; i < len; i++) {
    SPI.transfer(data[i]);
  }
  xl2515_unselect();
}

static void xl2515_read_reg_unlocked(uint8_t reg, uint8_t *data, uint8_t len)
{
  xl2515_select();
  SPI.transfer(CAN_READ);
  SPI.transfer(reg);
  for (uint8_t i = 0; i < len; i++) {
    data[i] = SPI.transfer(DUMMY_BYTE);
  }
  xl2515_unselect();
}

static void xl2515_write_reg_byte_unlocked(uint8_t reg, uint8_t byte)
{
  xl2515_write_reg_unlocked(reg, &byte, 1);
}

static uint8_t xl2515_read_reg_byte_unlocked(uint8_t reg)
{
  uint8_t data = 0;
  xl2515_read_reg_unlocked(reg, &data, 1);
  return data;
}

static void xl2515_bit_modify_unlocked(uint8_t reg, uint8_t mask, uint8_t data)
{
  xl2515_select();
  SPI.transfer(CAN_BIT_MODIFY);
  SPI.transfer(reg);
  SPI.transfer(mask);
  SPI.transfer(data);
  xl2515_unselect();
}

static void xl2515_reset_unlocked(void)
{
  xl2515_select();
  SPI.transfer(CAN_RESET);
  xl2515_unselect();
}

static void xl2515_write_id_unlocked(uint8_t reg, uint32_t can_id, bool extd)
{
  uint8_t buf[4] = {0};
  if (extd) {
    can_id &= 0x1FFFFFFF;
    buf[0] = (uint8_t)(can_id >> 21);
    buf[1] = (uint8_t)(((can_id >> 13) & 0xE0) | EXIDE_SET | ((can_id >> 16) & 0x03));
    buf[2] = (uint8_t)(can_id >> 8);
    buf[3] = (uint8_t)can_id;
  } else {
    can_id &= 0x7FF;
    buf[0] = (uint8_t)(can_id >> 3);
    buf[1] = (uint8_t)((can_id & 0x07) << 5);
  }
  xl2515_write_reg_unlocked(reg, buf, sizeof(buf));
}

static void xl2515_read_id_unlocked(uint8_t reg, uint32_t *can_id, bool *extd)
{
  uint8_t buf[4] = {0};
  xl2515_read_reg_unlocked(reg, buf, sizeof(buf));
  if (buf[1] & EXIDE_SET) {
    *extd = true;
    *can_id = ((uint32_t)buf[0] << 21) |
              ((uint32_t)(buf[1] & 0xE0) << 13) |
              ((uint32_t)(buf[1] & 0x03) << 16) |
              ((uint32_t)buf[2] << 8) |
              buf[3];
  } else {
    *extd = false;
    *can_id = ((uint32_t)buf[0] << 3) | (buf[1] >> 5);
  }
}

static bool xl2515_apply_bitrate_unlocked(xl2515_rate_kbps_t rate_kbps)
{
  const uint8_t can_rate_arr[8][3] = {
    {0x13, 0xA4, 0x04},
    {0x09, 0xA4, 0x04},
    {0x04, 0x9E, 0x03},
    {0x03, 0x9E, 0x03},
    {0x01, 0x1E, 0x03},
    {0x00, 0x9E, 0x03},
    {0x00, 0x92, 0x02},
    {0x00, 0x82, 0x02},
  };

  if (rate_kbps > KBPS1000) {
    return false;
  }

  xl2515_write_reg_byte_unlocked(CNF1, can_rate_arr[rate_kbps][0]);
  xl2515_write_reg_byte_unlocked(CNF2, can_rate_arr[rate_kbps][1]);
  xl2515_write_reg_byte_unlocked(CNF3, can_rate_arr[rate_kbps][2]);
  return true;
}

static bool xl2515_configure_unlocked(xl2515_rate_kbps_t rate_kbps)
{
  xl2515_write_reg_byte_unlocked(CANCTRL, REQOP_CONFIG | CLKOUT_DISABLED);
  delay(10);

  if ((xl2515_read_reg_byte_unlocked(CANSTAT) & REQOP) != OPMODE_CONFIG) {
    Serial.printf("XL2515 failed to enter config mode\r\n");
    return false;
  }

  if (!xl2515_apply_bitrate_unlocked(rate_kbps)) {
    return false;
  }

  xl2515_write_reg_byte_unlocked(RXB0CTRL, RXM_RCV_ALL | BUKT_ROLLOVER);
  xl2515_write_reg_byte_unlocked(RXB1CTRL, RXM_RCV_ALL);

  uint8_t zero4[4] = {0, 0, 0, 0};
  xl2515_write_reg_unlocked(RXM0SIDH, zero4, sizeof(zero4));
  xl2515_write_reg_unlocked(RXM1SIDH, zero4, sizeof(zero4));
  xl2515_write_reg_unlocked(RXF0SIDH, zero4, sizeof(zero4));
  xl2515_write_reg_unlocked(RXF1SIDH, zero4, sizeof(zero4));
  xl2515_write_reg_unlocked(RXF2SIDH, zero4, sizeof(zero4));
  xl2515_write_reg_unlocked(RXF3SIDH, zero4, sizeof(zero4));
  xl2515_write_reg_unlocked(RXF4SIDH, zero4, sizeof(zero4));
  xl2515_write_reg_unlocked(RXF5SIDH, zero4, sizeof(zero4));

  xl2515_write_reg_byte_unlocked(TXB0CTRL, TXP_HIGHEST);
  xl2515_write_reg_byte_unlocked(CANINTF, 0x00);
  xl2515_write_reg_byte_unlocked(EFLG, 0x00);
  xl2515_write_reg_byte_unlocked(CANINTE, RX0IE_ENABLED | RX1IE_ENABLED | ERRIE_ENABLED);

  xl2515_write_reg_byte_unlocked(CANCTRL, REQOP_NORMAL | CLKOUT_DISABLED);
  delay(10);
  if ((xl2515_read_reg_byte_unlocked(CANSTAT) & REQOP) != OPMODE_NORMAL) {
    Serial.printf("XL2515 failed to enter normal mode\r\n");
    return false;
  }
  return true;
}

static void IRAM_ATTR xl2515_int_isr(void)
{
  g_xl2515_recv_flag = true;
}

void bsp_xl2515_reset(void)
{
  if (!xl2515_take()) {
    return;
  }
  xl2515_reset_unlocked();
  xl2515_give();
}

void bsp_xl2515_init(xl2515_rate_kbps_t rate_kbps)
{
  if (xl2515_spi_mutex == NULL) {
    xl2515_spi_mutex = xSemaphoreCreateMutex();
  }

  pinMode(XL2515_CS, OUTPUT);
  digitalWrite(XL2515_CS, HIGH);
  pinMode(XL2515_INT, INPUT_PULLUP);
  SPI.begin(XL2515_SCLK, XL2515_MISO, XL2515_MOSI, XL2515_CS);

  attachInterrupt(digitalPinToInterrupt(XL2515_INT), xl2515_int_isr, FALLING);

  if (!xl2515_take()) {
    return;
  }
  xl2515_reset_unlocked();
  xl2515_give();
  delay(100);

  if (!xl2515_take()) {
    return;
  }
  bool ok = xl2515_configure_unlocked(rate_kbps);
  xl2515_give();

  if (ok) {
    Serial.printf("CAN_2 initialized\r\n");
  } else {
    Serial.printf("CAN_2 initialization failed\r\n");
  }
}

bool bsp_xl2515_set_bitrate(uint32_t bitrate_kbps)
{
  xl2515_rate_kbps_t rate;
  if (!xl2515_rate_from_kbps(bitrate_kbps, &rate)) {
    Serial.printf("Unsupported XL2515 bitrate: %lu kbps\r\n", bitrate_kbps);
    return false;
  }

  if (!xl2515_take()) {
    return false;
  }
  bool ok = xl2515_configure_unlocked(rate);
  xl2515_give();
  return ok;
}

bool bsp_xl2515_send_frame(uint32_t can_id, const uint8_t *data, uint8_t len, bool extd)
{
  if (len > 8) {
    len = 8;
  }
  if (!extd && can_id > 0x7FF) {
    Serial.printf("CAN CH2 standard ID overflow, sending as extended frame\r\n");
    extd = true;
  }
  if (extd && can_id > 0x1FFFFFFF) {
    Serial.printf("CAN CH2 extended ID overflow\r\n");
    return false;
  }

  if (!xl2515_take()) {
    return false;
  }

  uint8_t wait_count = 0;
  while ((xl2515_read_reg_byte_unlocked(TXB0CTRL) & TXREQ) && wait_count < 50) {
    xl2515_give();
    delay(1);
    if (!xl2515_take()) {
      return false;
    }
    wait_count++;
  }

  if (xl2515_read_reg_byte_unlocked(TXB0CTRL) & TXREQ) {
    xl2515_give();
    Serial.printf("CAN CH2 TX buffer busy\r\n");
    return false;
  }

  xl2515_write_id_unlocked(TXB0SIDH, can_id, extd);
  xl2515_write_reg_byte_unlocked(TXB0DLC, len & 0x0F);
  if (len > 0 && data != NULL) {
    xl2515_write_reg_unlocked(TXB0D0, data, len);
  }
  xl2515_bit_modify_unlocked(TXB0CTRL, TXREQ, TXREQ_SET);
  xl2515_give();
  return true;
}

void bsp_xl2515_send(uint32_t can_id, uint8_t *data, uint8_t len)
{
  bsp_xl2515_send_frame(can_id, data, len, false);
}

bool bsp_xl2515_recv_frame(uint32_t *can_id, uint8_t *data, uint8_t *len, bool *extd)
{
  if (can_id == NULL || data == NULL || len == NULL) {
    Serial.printf("CAN CH2 RX invalid argument\r\n");
    return false;
  }

  if (!g_xl2515_recv_flag && digitalRead(XL2515_INT) == HIGH) {
    return false;
  }

  if (!xl2515_take()) {
    return false;
  }

  uint8_t intf = xl2515_read_reg_byte_unlocked(CANINTF);
  uint8_t id_reg = 0;
  uint8_t dlc_reg = 0;
  uint8_t data_reg = 0;
  uint8_t clear_flag = 0;

  if (intf & RX0IF) {
    id_reg = RXB0SIDH;
    dlc_reg = RXB0DLC;
    data_reg = RXB0D0;
    clear_flag = RX0IF;
  } else if (intf & RX1IF) {
    id_reg = RXB1SIDH;
    dlc_reg = RXB1DLC;
    data_reg = RXB1D0;
    clear_flag = RX1IF;
  } else {
    if (intf & (TX0IF | TX1IF | TX2IF | ERRIF | MERRF)) {
      xl2515_bit_modify_unlocked(CANINTF, TX0IF | TX1IF | TX2IF | ERRIF | MERRF, 0);
    }
    g_xl2515_recv_flag = false;
    xl2515_give();
    return false;
  }

  bool frame_extd = false;
  xl2515_read_id_unlocked(id_reg, can_id, &frame_extd);
  if (extd != NULL) {
    *extd = frame_extd;
  }

  uint8_t frame_len = xl2515_read_reg_byte_unlocked(dlc_reg) & 0x0F;
  if (frame_len > 8) {
    frame_len = 8;
  }
  *len = frame_len;
  for (uint8_t i = 0; i < frame_len; i++) {
    data[i] = xl2515_read_reg_byte_unlocked(data_reg + i);
  }

  xl2515_bit_modify_unlocked(CANINTF, clear_flag, 0);
  if ((xl2515_read_reg_byte_unlocked(CANINTF) & (RX0IF | RX1IF)) == 0) {
    g_xl2515_recv_flag = false;
  }
  xl2515_give();
  return true;
}

bool bsp_xl2515_recv(uint32_t *can_id, uint8_t *data, uint8_t *len)
{
  bool extd = false;
  return bsp_xl2515_recv_frame(can_id, data, len, &extd);
}
