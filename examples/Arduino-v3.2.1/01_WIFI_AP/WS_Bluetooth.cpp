#include <Arduino.h>
#include "WS_Bluetooth.h"
/*
0A 0B + data  : CAN CH2 send (0A 0B + 4-byte ID + 1-byte frame type + bytes)
0A 0C + data  : CAN CH1 send (0A 0C + 4-byte ID + 1-byte frame type + bytes)
0A 0D         : 鑾峰彇 WIFI 鐨?IP
*/
BLEServer* pServer;                                                             // Used to represent a BLE server
BLECharacteristic* pTxCharacteristic;
BLECharacteristic* pRxCharacteristic;

/**********************************************************  Bluetooth   *********************************************************/

class MyServerCallbacks : public BLEServerCallbacks {                           //By overriding the onConnect() and onDisconnect() functions
    void onConnect(BLEServer* pServer) {                                        // When the Device is connected, "Device connected" is printed.
    Serial.println("Device connected"); 
  }

  void onDisconnect(BLEServer* pServer) {                                       // "Device disconnected" will be printed when the device is disconnected
    Serial.println("Device disconnected");

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();                 // Re-broadcast so that the device can query
    pAdvertising->addServiceUUID(SERVICE_UUID);                                 // Re-broadcast so that the device can query
    pAdvertising->setScanResponse(true);                                        // Re-broadcast so that the device can query
    pAdvertising->setMinPreferred(0x06);                                        // Re-broadcast so that the device can query 
    pAdvertising->setMinPreferred(0x12);                                        // Re-broadcast so that the device can query 
    BLEDevice::startAdvertising();                                              // Re-broadcast so that the device can query 
    pRxCharacteristic->notify();                                                // Re-broadcast so that the device can query  
    pAdvertising->start();                                                      // Re-broadcast so that the device can query
  }
};

uint8_t IP_Flag = 0;
class MyRXCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {                            // The onWrite function is called when the remote device sends data to your feature
    String rxValue = pCharacteristic->getValue();
    if (!rxValue.isEmpty()) {
      // The received data rxValue is processed here
      uint16_t Ble_Length = rxValue.length();
      if(Ble_Length == 2)
      {
        Serial.printf("BLE RX len:%u\r\n", (unsigned int)Ble_Length);
        const uint8_t* valueBytes = reinterpret_cast<const uint8_t*>(rxValue.c_str());
        if(valueBytes[0] == 0x0A && valueBytes[1] == 0x0D){                      // Instruction check correct
          Serial.printf("BLE Printf IP\r\n");
          IP_Flag = 50;
        }
      }
      else if(Ble_Length > 2)
      {
        Serial.printf("BLE RX len:%u\r\n", (unsigned int)Ble_Length);
        const uint8_t* valueBytes = reinterpret_cast<const uint8_t*>(rxValue.c_str());
        if(valueBytes[0] == 0x0A && valueBytes[1] == 0x0B && Ble_Length > 7){
            uint32_t can_id = ((uint32_t)valueBytes[2] << 24) |
                              ((uint32_t)valueBytes[3] << 16) |
                              ((uint32_t)valueBytes[4] << 8) |
                              (uint32_t)valueBytes[5];
            bool extd = valueBytes[6];
            if ((Ble_Length - 7) > UINT8_MAX) {
              Serial.printf("CAN CH2 BLE payload too long\r\n");
              pRxCharacteristic->setValue("");
              return;
            }
            send_can2_message(can_id, valueBytes + 7, (uint8_t)(Ble_Length - 7), extd);   // Send data from CAN CH2
        }
        else if(valueBytes[0] == 0x0A && valueBytes[1] == 0x0C && Ble_Length > 7){
            if ((Ble_Length - 7) > UINT8_MAX) {
              Serial.printf("CAN CH1 BLE payload too long\r\n");
              pRxCharacteristic->setValue("");
              return;
            }
            uint32_t can_id = ((uint32_t)valueBytes[2] << 24) |
                              ((uint32_t)valueBytes[3] << 16) |
                              ((uint32_t)valueBytes[4] << 8) |
                              (uint32_t)valueBytes[5];
            bool extd = valueBytes[6];
            send_message(can_id, valueBytes + 7, (uint8_t)(Ble_Length - 7), extd); // Send data from CAN CH1
        }
        else
          Serial.printf("Note : Non-instruction data was received - Bluetooth !\r\n");
      }
      else
      {
        Serial.printf("Note : Non-instruction data was received - Bluetooth !\r\n");
      }
      pRxCharacteristic->setValue("");                                           // After data is read, set it to blank for next read
    }
  }
};

void Bluetooth_SendData(char* Data) {  // Send data using Bluetooth
  if (Data != nullptr && strlen(Data) > 0) {
    if (pServer->getConnectedCount() > 0) {
      String SendValue = String(Data);  // Convert char* to String
      pTxCharacteristic->setValue(SendValue);  // Set SendValue to the eigenvalue (String type)
      pTxCharacteristic->notify();  // Sends a notification to all connected devices
    }
  }
}
void Bluetooth_Init()
{
  /*************************************************************************
  Bluetooth
  *************************************************************************/
  BLEDevice::init("ESP32-S3-CAN-2CH");                                        // Initialize Bluetooth and start broadcasting                           
  pServer = BLEDevice::createServer();                                          
  pServer->setCallbacks(new MyServerCallbacks());                               
  BLEService* pService = pServer->createService(SERVICE_UUID);                  
  pTxCharacteristic = pService->createCharacteristic(
                                    TX_CHARACTERISTIC_UUID,
                                    BLECharacteristic:: PROPERTY_READ);         // The eigenvalues are readable and can be read by remote devices
  pRxCharacteristic = pService->createCharacteristic(
                                    RX_CHARACTERISTIC_UUID,
                                    BLECharacteristic::PROPERTY_WRITE);         // The eigenvalues are writable and can be written to by remote devices
  pRxCharacteristic->setCallbacks(new MyRXCallback());

  pRxCharacteristic->setValue("Successfully Connect To ESP32-S3-CAN-2CH");      
  pService->start();   

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();                   
  pAdvertising->addServiceUUID(SERVICE_UUID);                                   
  pAdvertising->setScanResponse(true);                                          
  pAdvertising->setMinPreferred(0x06);                                          
  pAdvertising->setMinPreferred(0x12);                                          
  BLEDevice::startAdvertising();                                                
  pRxCharacteristic->notify();                                                    
  pAdvertising->start();
  Serial.printf("Now you can read it in your phone!\r\n");
  if (xTaskCreatePinnedToCore(
    BLETask,    
    "BLETask",   
    4096,                
    NULL,                 
    2,                   
    NULL,                 
    0                   
  ) != pdPASS) {
    Serial.printf("BLETask create failed\r\n");
  }
}

void BLETask(void *parameter) {
  while(1){
    if(IP_Flag){
      Bluetooth_SendData(ipStr);
      vTaskDelay(pdMS_TO_TICKS(100));
      IP_Flag --;
    }
    else{
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
  vTaskDelete(NULL);
}
