#pragma once

#include "WS_XL2515.h"
#include "WS_PCF85063.h"

#define CAN2_Received_Len_MAX 1000

extern char *CAN2_Read_Data;
extern size_t CAN2_Received_Len;
extern uint32_t CAN2_bitrate_kbps;

void CAN2_Init(void);
void CAN2_UpdateRate(uint32_t bitrate_kbps);
void CAN2_Loop(void);
void CAN2Task(void *parameter);
bool CAN2_Set_Bitrate(uint32_t bitrate_kbps);
bool CAN2_CopyAndClearReadData(char *dest, size_t dest_len);
void CAN2_ClearReadData(void);
void send_can2_message(uint32_t CAN_ID, const uint8_t *Data, uint8_t Data_length, bool Frame_type);
