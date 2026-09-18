#include <Arduino.h>
#include "WS_CAN.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstring>

static bool driver_installed = false;
static TaskHandle_t CANTaskHandle = NULL;
static SemaphoreHandle_t CANMutex = NULL;
char * CAN_Read_Data;
size_t CAN_Received_Len = 0;
uint32_t CAN_bitrate_kbps = 250;
static const char *CAN1_Status = "not initialized";

static bool CAN_Select_Timing(uint32_t bitrate_kbps, twai_timing_config_t *t_config)
{
  switch (bitrate_kbps) {
    case 25:
      *t_config = TWAI_TIMING_CONFIG_25KBITS();
      return true;
    case 50:
      *t_config = TWAI_TIMING_CONFIG_50KBITS();
      return true;
    case 100:
      *t_config = TWAI_TIMING_CONFIG_100KBITS();
      return true;
    case 125:
      *t_config = TWAI_TIMING_CONFIG_125KBITS();
      return true;
    case 250:
      *t_config = TWAI_TIMING_CONFIG_250KBITS();
      return true;
    case 500:
      *t_config = TWAI_TIMING_CONFIG_500KBITS();
      return true;
    case 800:
      *t_config = TWAI_TIMING_CONFIG_800KBITS();
      return true;
    case 1000:
      *t_config = TWAI_TIMING_CONFIG_1MBITS();
      return true;
    default:
      Serial.printf("Unsupported CAN CH1 bitrate: %lu kbps\r\n", bitrate_kbps);
      return false;
  }
}

static bool CAN_Take(uint32_t timeout_ms)
{
  if (CANMutex == NULL) {
    return true;
  }
  return xSemaphoreTake(CANMutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void CAN_Give(void)
{
  if (CANMutex != NULL) {
    xSemaphoreGive(CANMutex);
  }
}

static bool CAN_Start_With_Timing(const twai_timing_config_t *t_config)
{
  if (t_config == NULL) {
    return false;
  }

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TXD2, (gpio_num_t)RXD2, TWAI_MODE_NORMAL);
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, t_config, &f_config) != ESP_OK) {
    return false;
  }

  if (twai_start() != ESP_OK) {
    twai_driver_uninstall();
    return false;
  }

  uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR |
                              TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_TX_IDLE |
                              TWAI_ALERT_TX_FAILED;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) != ESP_OK) {
    twai_stop();
    twai_driver_uninstall();
    return false;
  }

  return true;
}

static void CAN_Restore_Current_Bitrate(void)
{
  twai_timing_config_t old_t_config;
  if (!CAN_Select_Timing(CAN_bitrate_kbps, &old_t_config)) {
    Serial.printf("CAN CH1 cannot restore invalid previous bitrate:%lu kbps\r\n", CAN_bitrate_kbps);
    return;
  }
  if (CAN_Start_With_Timing(&old_t_config)) {
    driver_installed = true;
    Serial.printf("CAN CH1 restored previous bitrate:%lu kbps\r\n", CAN_bitrate_kbps);
  } else {
    Serial.printf("CAN CH1 failed to restore previous bitrate:%lu kbps\r\n", CAN_bitrate_kbps);
  }
}

void CAN_Init(void)
{                                // Initializing serial port
  CAN1_Status = "initializing";

  if (CANMutex == NULL) {
    CANMutex = xSemaphoreCreateMutex();
  }
  if (CANMutex == NULL) {
    CAN1_Status = "failed to create mutex";
    Serial.printf("CAN_1 failed to create mutex\r\n");
    return;
  }

  // Initialize configuration structures using macro initializers
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TXD2, (gpio_num_t)RXD2, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();  //Look in the api-reference for other speed sets.
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  // Install TWAI driver
  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    CAN1_Status = "failed to install TWAI driver";
    Serial.printf("CAN_1 failed to install TWAI driver\r\n");
    return;
  }

  // Start TWAI driver
  if (twai_start() != ESP_OK) {
    CAN1_Status = "failed to start TWAI driver";
    Serial.printf("CAN_1 failed to start TWAI driver\r\n");
    return;
  }

  // Reconfigure alerts to detect TX alerts and Bus-Off errors
  uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_FAILED;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) != ESP_OK) {
    CAN1_Status = "failed to reconfigure alerts";
    Serial.printf("CAN_1 failed to reconfigure alerts\r\n");
    return;
  }

  // TWAI driver is now successfully installed and started
  driver_installed = true;
  CAN1_Status = "initialized";
  Serial.printf("CAN_1 initialized\r\n");

  if (xTaskCreatePinnedToCore(
    CANTask,    
    "CANTask",   
    4096,                
    NULL,                 
    3,                   
    &CANTaskHandle,                 
    0                   
  ) != pdPASS) {
    CAN1_Status = "initialized, CANTask failed";
    Serial.printf("CAN_1 failed to create CANTask\r\n");
    CANTaskHandle = NULL;
  }
}

void CAN_UpdateRate(uint32_t bitrate_kbps)                                             // Initializing serial port
{   
  if(CAN_Set_Bitrate(bitrate_kbps) == ESP_OK) {
    CAN_bitrate_kbps = bitrate_kbps;
    Serial.printf("Update CAN CH1 rate to:%ldkbps\r\n",bitrate_kbps);
  }
  else {
    Serial.printf("CAN CH1 rate update failed\r\n");
  }
}
esp_err_t CAN_Set_Bitrate(uint32_t bitrate_kbps) {
  if (!driver_installed) {
    Serial.printf("CAN CH1 TWAI driver not installed. Call CAN_Init() first.\r\n");
    return ESP_ERR_INVALID_STATE;
  }

  twai_timing_config_t new_t_config;
  if (!CAN_Select_Timing(bitrate_kbps, &new_t_config)) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!CAN_Take(2000)) {
    Serial.printf("CAN CH1 mutex timeout while changing bitrate\r\n");
    return ESP_ERR_TIMEOUT;
  }

  CAN_Received_Len = 0;
  if (CAN_Read_Data != NULL) {
    memset(CAN_Read_Data, 0, CAN_Received_Len_MAX);
  }

  driver_installed = false;
  twai_stop();
  vTaskDelay(pdMS_TO_TICKS(10));
  twai_driver_uninstall();

  twai_general_config_t new_g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TXD2, (gpio_num_t)RXD2, TWAI_MODE_NORMAL);
  twai_filter_config_t new_f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&new_g_config, &new_t_config, &new_f_config) != ESP_OK) {
    Serial.printf("CAN CH1 failed to reinstall TWAI driver.\r\n");
    CAN_Restore_Current_Bitrate();
    CAN_Give();
    return ESP_FAIL;
  }

  if (twai_start() != ESP_OK) {
    Serial.printf("CAN CH1 failed to restart TWAI driver.\r\n");
    twai_driver_uninstall();
    CAN_Restore_Current_Bitrate();
    CAN_Give();
    return ESP_FAIL;
  }

  uint32_t new_alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR |
                                  TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_TX_IDLE |
                                  TWAI_ALERT_TX_FAILED;

  if (twai_reconfigure_alerts(new_alerts_to_enable, NULL) != ESP_OK) {
    Serial.printf("CAN CH1 failed to reconfigure alerts.\r\n");
    twai_stop();
    twai_driver_uninstall();
    CAN_Restore_Current_Bitrate();
    CAN_Give();
    return ESP_FAIL;
  }

  driver_installed = true;
  CAN_Give();
  return ESP_OK;
}

bool CAN_CopyAndClearReadData(char *dest, size_t dest_len)
{
  if (dest == NULL || dest_len == 0) {
    return false;
  }
  dest[0] = '\0';
  if (!CAN_Take(100)) {
    return false;
  }
  if (CAN_Read_Data == NULL || CAN_Read_Data[0] == '\0') {
    CAN_Give();
    return false;
  }
  snprintf(dest, dest_len, "%s", CAN_Read_Data);
  CAN_Received_Len = 0;
  memset(CAN_Read_Data, 0, CAN_Received_Len_MAX);
  CAN_Give();
  return true;
}

void CAN_ClearReadData(void)
{
  if (!CAN_Take(100)) {
    return;
  }
  CAN_Received_Len = 0;
  if (CAN_Read_Data != NULL) {
    memset(CAN_Read_Data, 0, CAN_Received_Len_MAX);
  }
  CAN_Give();
}

static bool CAN_Transmit(const twai_message_t *message)
{
  if (!CAN_Take(200)) {
    Serial.printf("CAN CH1 mutex timeout while transmitting\r\n");
    return false;
  }

  if (!driver_installed) {
    CAN_Give();
    Serial.printf("CAN CH1 TWAI driver not installed\r\n");
    return false;
  }

  esp_err_t err = twai_transmit(message, pdMS_TO_TICKS(100));
  CAN_Give();
  return err == ESP_OK;
}

static void print_can1_tx_message(const twai_message_t &message, bool ok)
{
  Serial.printf("CAN CH1 TX %s ID:0x%lx Type:%s Len:%u Data:",
         ok ? "OK" : "FAILED",
         message.identifier,
         message.extd ? "Extended" : "Standard",
         message.data_length_code);
  for (uint8_t i = 0; i < message.data_length_code && i < 8; i++) {
    Serial.printf(" %02X", message.data[i]);
  }
  Serial.printf("\r\n");
}

void send_message_Bytes(twai_message_t message) {
  send_message(message.identifier, message.data, message.data_length_code, message.extd);
}
// Standard frames ID: 0x000 to 0x7FF
// Extended frames ID: 0x00000000  to 0x1FFFFFFF
// Frame_type : 1闂佹寧绋掗鎭眛ended frames   0闂佹寧绋掗悺鏀朼ndard frames
void send_message(uint32_t CAN_ID, const uint8_t* Data, uint8_t Data_length, bool Frame_type) {
  // Send message
  // Configure message to transmit
  if (Data == NULL && Data_length > 0) {
    Serial.printf("CAN CH1 TX data is NULL\r\n");
    return;
  }
  twai_message_t message = {0};
  message.identifier = CAN_ID;
  message.rtr = 0;                              // Disable remote frame
  if(!Frame_type && CAN_ID > 0x7FF){            // Standard frames and ID overflow
    Serial.printf("The frame type is set incorrectly and data will eventually be sent as an extended frame!!!!\r\n");
    message.extd = 1;
  }
  else 
    message.extd = Frame_type;
  if(Data_length > 8){
    uint16_t Frame_count = (Data_length / 8);
    for (int i = 0; i < Frame_count; i++) {
      message.data_length_code = 8;
      for (int j = 0; j < 8; j++) {
        message.data[j] = Data[j + (i * 8)];
      }
      // Queue message for transmission
      if (CAN_Transmit(&message)) {
        print_can1_tx_message(message, true);
      } else {
        print_can1_tx_message(message, false);
      }
    }
    if(Data_length % 8){
      uint8_t Data_length_Now = Data_length % 8;
      message.data_length_code = Data_length_Now;
      for (int k = 0; k < Data_length_Now; k++) {
        message.data[k] = Data[k + (Data_length - Data_length_Now)];
      }
      // Queue message for transmission
      if (CAN_Transmit(&message)) {
        print_can1_tx_message(message, true);
      } else {
        print_can1_tx_message(message, false);
      }
    }
  }
  else{
    message.data_length_code = Data_length;
    for (int i = 0; i < Data_length; i++) {
      message.data[i] = Data[i];
    }
    // Queue message for transmission
    if (CAN_Transmit(&message)) {
      print_can1_tx_message(message, true);
    } else {
      print_can1_tx_message(message, false);
    }
  }
}



static void handle_rx_message(twai_message_t &message) {
  if (!(message.rtr)) {
    if (message.data_length_code > 0) {
      Serial.printf("CAN CH1 RX OK ID:0x%lx Type:%s Len:%u Data:",
             message.identifier,
             message.extd ? "Extended" : "Standard",
             message.data_length_code);
      for (int i = 0; i < message.data_length_code; i++) {
        Serial.printf(" %02X", message.data[i]);
      }
      Serial.printf("\r\n");
    } else {
      Serial.printf("CAN CH1 RX ID:0x%lx Type:%s Len:0 Data:\r\n",
             message.identifier,
             message.extd ? "Extended" : "Standard");
    }
  } else {
    Serial.printf("CAN CH1 RX RTR ID:0x%lx Type:%s\r\n",
           message.identifier,
           message.extd ? "Extended" : "Standard");
  }
}

static void append_rx_message(const twai_message_t &message) {
  if (CAN_Read_Data == NULL) {
    return;
  }

  uint8_t data_len = message.data_length_code;
  if (data_len > 8) {
    data_len = 8;
  }

  char datetime_str[50];
  datetime_to_str(datetime_str, datetime);
  if (CAN_Received_Len + sizeof(datetime_str) + (data_len * 6) + 80 >= CAN_Received_Len_MAX) {
    Serial.printf("Note : The data received by CAN CH1 is full.\r\n");
    CAN_Received_Len = 0;
    memset(CAN_Read_Data, 0, CAN_Received_Len_MAX);
    return;
  }

  char temp[8 * 6 + 1] = {0};
  for (uint8_t i = 0; i < data_len; i++) {
    char hex_byte[6];
    snprintf(hex_byte, sizeof(hex_byte), "0x%02X ", message.data[i]);
    strcat(temp, hex_byte);
  }

  CAN_Received_Len += snprintf(
    CAN_Read_Data + CAN_Received_Len,
    CAN_Received_Len_MAX - CAN_Received_Len,
    "%s :\n   CAN CH1 ID:0x%lx   CAN Type:%s\n   %s\n",
    datetime_str,
    message.identifier,
    message.extd ? "Extended frames" : "Standard frames",
    temp
  );
}

unsigned long previousMillis = 0;  // will store last time a message was send
#if Communication_failure_Enable
  static unsigned long previous_bus_error_time = 0; // To store the last time a BUS_ERROR was printed
#endif
void CAN_Loop(void)
{
  {
    if (!CAN_Take(100)) {
      return;
    }

    if (!driver_installed) {
      CAN_Give();
      return;
    }

    uint32_t new_alerts_triggered = 0;
    esp_err_t alert_ret = twai_read_alerts(&new_alerts_triggered, pdMS_TO_TICKS(POLLING_RATE_MS));
    if (alert_ret != ESP_OK || new_alerts_triggered == 0) {
      CAN_Give();
      return;
    }

    twai_status_info_t new_twaistatus;
    twai_get_status_info(&new_twaistatus);

    if (new_alerts_triggered & TWAI_ALERT_ERR_PASS) {
      Serial.printf("CAN CH1 alert: TWAI controller has become error passive.\r\n");
    }
    if (new_alerts_triggered & TWAI_ALERT_BUS_ERROR) {
      #if Communication_failure_Enable
        unsigned long currentMillis = millis();
        if (currentMillis - previous_bus_error_time >= BUS_ERROR_INTERVAL_MS) {
          Serial.printf("Note if there are other devices on the CAN bus and that both CAN channels use the same bitrate\r\n");
          previous_bus_error_time = currentMillis;
        }
      #endif
    }
    if (new_alerts_triggered & TWAI_ALERT_RX_QUEUE_FULL) {
      Serial.printf("CAN CH1 alert: RX queue is full causing a received frame to be lost.\r\n");
      Serial.printf("CAN CH1 RX buffered: %ld\t", new_twaistatus.msgs_to_rx);
      Serial.printf("CAN CH1 RX missed: %ld\t", new_twaistatus.rx_missed_count);
      Serial.printf("CAN CH1 RX overrun %ld\n", new_twaistatus.rx_overrun_count);
    }
    if (new_alerts_triggered & TWAI_ALERT_TX_FAILED) {
      Serial.printf("CAN CH1 alert: Transmission failed.\r\n");
      Serial.printf("CAN CH1 TX buffered: %ld\t", new_twaistatus.msgs_to_tx);
      Serial.printf("CAN CH1 TX error: %ld\t", new_twaistatus.tx_error_counter);
      Serial.printf("CAN CH1 TX failed: %ld\n", new_twaistatus.tx_failed_count);
    }
    if (new_alerts_triggered & TWAI_ALERT_RX_DATA) {
      twai_message_t new_message;
      while (twai_receive(&new_message, 0) == ESP_OK) {
        handle_rx_message(new_message);
        append_rx_message(new_message);
      }
    }

    CAN_Give();
  }
  return;
}

void CANTask(void *parameter) {
  CAN_Read_Data = (char *)heap_caps_malloc(CAN_Received_Len_MAX, MALLOC_CAP_SPIRAM);
  if (!CAN_Read_Data) {
    CAN_Read_Data = (char *)malloc(CAN_Received_Len_MAX);
  }
  if (!CAN_Read_Data) {
    Serial.printf("CAN CH1 failed to allocate receive buffer\n");
    vTaskDelete(NULL);
    return;
  }
  memset(CAN_Read_Data, 0, CAN_Received_Len_MAX);
  while(1){
    CAN_Loop();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  vTaskDelete(NULL);
}
