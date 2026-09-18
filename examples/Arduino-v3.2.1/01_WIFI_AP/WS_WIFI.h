#pragma once

#include "stdio.h"
#include <stdint.h>
#include <WiFi.h>
#include <WebServer.h> 
#include <WiFiClient.h>
#include <WiFiAP.h>
#include "WS_PCF85063.h"
#include "WS_Information.h"
#include "WS_Struct.h"
#include "WS_RTC.h"
#include "WS_CAN2.h"
#include "WS_CAN.h"

extern char ipStr[16];

void handleGetCAN2Data();
void handleCAN2SetRate();
void handleCAN2Send();
void handleClearCAN2Data();
void handleGetCANData();
void handleCANSetAllRate();
void handleCANSend();
void handleClearCANData();

void WIFI_Init();
void WebTask(void *parameter);

bool ParseRTCData(const char* Text, datetime_t* dt, Web_Receive* SerialData, Repetition_event* cycleEvent);
bool ParseRtcConfig(const char* Text, datetime_t* dt);
bool ParseCANData(const char* Text, CAN_Receive* CANData) ; 
bool ParseCANRateConfig(const char* Text,  uint32_t * CAN_bitrate_kbps);




