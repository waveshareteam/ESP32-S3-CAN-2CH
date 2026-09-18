#include "WS_CAN2.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "freertos/semphr.h"
#include <string.h>

static TaskHandle_t CAN2TaskHandle = NULL;
static SemaphoreHandle_t CAN2BufferMutex = NULL;
char *CAN2_Read_Data = NULL;
size_t CAN2_Received_Len = 0;
uint32_t CAN2_bitrate_kbps = 250;

static bool CAN2_Buffer_Take(uint32_t timeout_ms)
{
  if (CAN2BufferMutex == NULL) {
    return true;
  }
  return xSemaphoreTake(CAN2BufferMutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void CAN2_Buffer_Give(void)
{
  if (CAN2BufferMutex != NULL) {
    xSemaphoreGive(CAN2BufferMutex);
  }
}

static bool CAN2_rate_to_xl2515(uint32_t bitrate_kbps, xl2515_rate_kbps_t *rate)
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

static void print_can2_frame(const char *direction, uint32_t can_id, const uint8_t *data, uint8_t len, bool extd, bool ok)
{
  Serial.printf("CAN CH2 %s %s ID:0x%lx Type:%s Len:%u Data:",
         direction,
         ok ? "OK" : "FAILED",
         can_id,
         extd ? "Extended" : "Standard",
         len);
  for (uint8_t i = 0; i < len && i < 8; i++) {
    Serial.printf(" %02X", data[i]);
  }
  Serial.printf("\r\n");
}

void CAN2_Init(void)
{
  if (CAN2BufferMutex == NULL) {
    CAN2BufferMutex = xSemaphoreCreateMutex();
  }

  xl2515_rate_kbps_t rate = KBPS250;
  CAN2_rate_to_xl2515(CAN2_bitrate_kbps, &rate);
  bsp_xl2515_init(rate);

  if (xTaskCreatePinnedToCore(
    CAN2Task,
    "CAN2Task",
    4096,
    NULL,
    3,
    &CAN2TaskHandle,
    0
  ) != pdPASS) {
    Serial.printf("CAN CH2 failed to create CAN2Task\r\n");
    CAN2TaskHandle = NULL;
  }
}

void CAN2_UpdateRate(uint32_t bitrate_kbps)
{
  if (CAN2_Set_Bitrate(bitrate_kbps)) {
    CAN2_bitrate_kbps = bitrate_kbps;
    Serial.printf("Update CAN CH2 rate to:%lukbps\r\n", bitrate_kbps);
  } else {
    Serial.printf("CAN CH2 rate update failed\r\n");
  }
}

bool CAN2_Set_Bitrate(uint32_t bitrate_kbps)
{
  if (!CAN2_Buffer_Take(1000)) {
    Serial.printf("CAN CH2 buffer mutex timeout while changing bitrate\r\n");
    return false;
  }
  CAN2_Received_Len = 0;
  if (CAN2_Read_Data != NULL) {
    memset(CAN2_Read_Data, 0, CAN2_Received_Len_MAX);
  }
  CAN2_Buffer_Give();

  return bsp_xl2515_set_bitrate(bitrate_kbps);
}

bool CAN2_CopyAndClearReadData(char *dest, size_t dest_len)
{
  if (dest == NULL || dest_len == 0) {
    return false;
  }
  dest[0] = '\0';
  if (!CAN2_Buffer_Take(100)) {
    return false;
  }
  if (CAN2_Read_Data == NULL || CAN2_Read_Data[0] == '\0') {
    CAN2_Buffer_Give();
    return false;
  }
  snprintf(dest, dest_len, "%s", CAN2_Read_Data);
  CAN2_Received_Len = 0;
  memset(CAN2_Read_Data, 0, CAN2_Received_Len_MAX);
  CAN2_Buffer_Give();
  return true;
}

void CAN2_ClearReadData(void)
{
  if (!CAN2_Buffer_Take(100)) {
    return;
  }
  CAN2_Received_Len = 0;
  if (CAN2_Read_Data != NULL) {
    memset(CAN2_Read_Data, 0, CAN2_Received_Len_MAX);
  }
  CAN2_Buffer_Give();
}

void send_can2_message(uint32_t CAN_ID, const uint8_t *Data, uint8_t Data_length, bool Frame_type)
{
  if (Data == NULL && Data_length > 0) {
    Serial.printf("CAN CH2 TX data is NULL\r\n");
    return;
  }

  if (!Frame_type && CAN_ID > 0x7FF) {
    Serial.printf("CAN CH2 frame type was standard but ID is extended, sending extended frame\r\n");
    Frame_type = true;
  }

  uint16_t offset = 0;
  while (offset < Data_length) {
    uint8_t frame_len = Data_length - offset;
    if (frame_len > 8) {
      frame_len = 8;
    }

    bool ok = bsp_xl2515_send_frame(CAN_ID, Data + offset, frame_len, Frame_type);
    print_can2_frame("TX", CAN_ID, Data + offset, frame_len, Frame_type, ok);
    offset += frame_len;
  }

  if (Data_length == 0) {
    bool ok = bsp_xl2515_send_frame(CAN_ID, NULL, 0, Frame_type);
    print_can2_frame("TX", CAN_ID, NULL, 0, Frame_type, ok);
  }
}

static void handle_can2_rx_message(uint32_t can_id, uint8_t *data, uint8_t len, bool extd)
{
  print_can2_frame("RX", can_id, data, len, extd, true);

  if (!CAN2_Buffer_Take(100)) {
    Serial.printf("CAN CH2 buffer mutex timeout while receiving\r\n");
    return;
  }

  if (CAN2_Read_Data == NULL) {
    CAN2_Buffer_Give();
    return;
  }

  char datetime_str[50];
  datetime_to_str(datetime_str, datetime);
  if (CAN2_Received_Len + sizeof(datetime_str) + (len * 6) + 80 >= CAN2_Received_Len_MAX) {
    Serial.printf("Note : The data received by CAN CH2 is full.\r\n");
    CAN2_Received_Len = 0;
    memset(CAN2_Read_Data, 0, CAN2_Received_Len_MAX);
    CAN2_Buffer_Give();
    return;
  }

  char temp[8 * 6 + 1] = {0};
  for (uint8_t i = 0; i < len; i++) {
    char hex_byte[6];
    snprintf(hex_byte, sizeof(hex_byte), "0x%02X ", data[i]);
    strcat(temp, hex_byte);
  }

  CAN2_Received_Len += snprintf(
    CAN2_Read_Data + CAN2_Received_Len,
    CAN2_Received_Len_MAX - CAN2_Received_Len,
    "%s :\n   CAN CH2 ID:0x%lx   CAN Type:%s\n   %s\n",
    datetime_str,
    can_id,
    extd ? "Extended frames" : "Standard frames",
    temp
  );
  CAN2_Buffer_Give();
}

void CAN2_Loop(void)
{
  uint32_t can_id = 0;
  uint8_t len = 0;
  uint8_t data[8] = {0};
  bool extd = false;
  uint8_t frame_count = 0;

  while (frame_count < 16 && bsp_xl2515_recv_frame(&can_id, data, &len, &extd)) {
    handle_can2_rx_message(can_id, data, len, extd);
    frame_count++;
  }
}

void CAN2Task(void *parameter)
{
  CAN2_Read_Data = (char *)heap_caps_malloc(CAN2_Received_Len_MAX, MALLOC_CAP_SPIRAM);
  if (CAN2_Read_Data == NULL) {
    CAN2_Read_Data = (char *)malloc(CAN2_Received_Len_MAX);
  }
  if (CAN2_Read_Data == NULL) {
    Serial.printf("Failed to allocate CAN2_Read_Data buffer\r\n");
    vTaskDelete(NULL);
    return;
  }
  if (!CAN2_Buffer_Take(1000)) {
    Serial.printf("CAN CH2 buffer mutex timeout while initializing\r\n");
    vTaskDelete(NULL);
    return;
  }
  memset(CAN2_Read_Data, 0, CAN2_Received_Len_MAX);
  CAN2_Buffer_Give();

  while (1) {
    CAN2_Loop();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  vTaskDelete(NULL);
}
