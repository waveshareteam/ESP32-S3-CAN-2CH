
#include "WS_Bluetooth.h"
#include "WS_CAN2.h"
#include "WS_CAN.h"
#include "WS_RTC.h"
#include "WS_WIFI.h"

/********************************************************  Initializing  ********************************************************/
void setup() { 
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  delay(1000);

  if (I2C_Init() != ESP_OK) {
    Serial.printf("I2C initialization failed, stop setup\r\n");
    return;
  }
  CAN2_Init();
  CAN_Init();
  RTC_Init();// RTC
  WIFI_Init();// WIFI
  Bluetooth_Init();// Bluetooth
  
  Serial.printf("Connect to the WIFI network named \"ESP32-S3-CAN-2CH\" and access the Internet using the connected IP address!!!\r\n");
}

/**********************************************************  While  **********************************************************/
void loop() {

}
