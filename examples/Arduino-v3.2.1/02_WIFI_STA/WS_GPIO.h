#pragma once

#include <Arduino.h>     // Reference the ESP32 built-in serial port library

/*************************************************************  I/O  *************************************************************/
#define GPIO_PIN_CH1      47    // CH1 Control GPIO

#define TXD2              15    // The TXD of TWAI corresponds to GPIO   CAN
#define RXD2              16    // The RXD of TWAI corresponds to GPIO   CAN

#define XL2515_CS         17    // CAN CH2 XL2515 CS
#define XL2515_INT        18    // CAN CH2 XL2515 INT
#define XL2515_SCLK       21    // CAN CH2 XL2515 SCLK
#define XL2515_MOSI       41    // CAN CH2 XL2515 MOSI
#define XL2515_MISO       42    // CAN CH2 XL2515 MISO

#define RTC_INT           40    // RTC PCF85063 INT, external 10K pull-up on PCB


/*************************************************************  I/O  *************************************************************/
void digitalToggle(int pin);
