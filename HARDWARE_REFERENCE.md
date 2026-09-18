# ESP32-S3-CAN-2CH Hardware Reference

> Hardware quick reference for developers and AI tools. The content is based on this repository's `hardware/schematics/ESP32-S3-CAN-2CH.pdf` schematic, and cross-checked with the current examples / BSP software pin definitions.

## 1. Board Overview

- Product: `ESP32-S3-CAN-2CH`
- Main controller: `ESP32-S3R8`
- External Flash: `XM25QH128DHIQT`, 128 Mbit / 16 MB
- Main onboard resources: dual CAN channels, USB Type-C, PCF85063AT RTC, 32.768 kHz RTC crystal, 40 MHz main crystal, isolated power supply, ESP32-S3 native USB download/log interface, UART expansion interface, 20-pin expansion header
- CAN channels:
  - CAN CH1: ESP32-S3 internal TWAI controller + isolation + CAN transceiver
  - CAN CH2: `XL2515QF20` external CAN controller + isolation + CAN transceiver

## 2. Onboard Peripherals

| Module | Device / Function | Interface | Parameters | GPIO / Signal |
|---|---|---|---|---|
| Main controller | ESP32-S3R8 | - | Wi-Fi / BLE, package with built-in PSRAM | - |
| External Flash | XM25QH128DHIQT | SPI Flash | 128 Mbit / 16 MB | `SPID` / `SPIQ` / `SPICLK` / `SPICS0` / `SPIWP` / `SPIHD` |
| CAN CH1 | ESP32-S3 TWAI + CAN transceiver | TWAI | Onboard 120R termination resistor position `R23` | TX=`GPIO15` (`TXD1`), RX=`GPIO16` (`RXD1`) |
| CAN CH2 | XL2515QF20 + CAN transceiver | SPI | 16 MHz crystal, onboard 120R termination resistor position `R30` | CS=`GPIO17`, INT=`GPIO18`, SCLK=`GPIO21`, MOSI=`GPIO41`, MISO=`GPIO42` |
| RTC | PCF85063AT | I2C | 7-bit address usually `0x51`, 32.768 kHz crystal | SCL=`GPIO38`, SDA=`GPIO39`, INT=`GPIO40` |
| USB Type-C | ESP32-S3 native USB | USB | Download, logs, USB communication | D-=`GPIO19` (`USB_N` / `D_N`), D+=`GPIO20` (`USB_P` / `D_P`) |
| UART0 | Default serial port | UART | Routed to the expansion header | TX=`U0TXD` / `GPIO43`, RX=`U0RXD` / `GPIO44` |
| BOOT button | Key1 | GPIO / strapping | Download-mode button | `GPIO0` |
| RESET button | Key2 | Reset | `CHIP_PU` / `RESET` | `RESET` |
| 3.3 V power | MP1605GTF-Z | Power | 5 V to system 3.3 V | `3V3` |
| Isolated 5 V | B0505LS-1W | Isolated power | CAN isolated-side supply | `Relay-5V` / `SGND` |
| Isolated 3.3 V | ME6217C33M5G | Power | Isolated-side 3.3 V | `Relay-3V3` / `SGND` |

## 3. GPIO Assignment

The table below lists GPIOs in ascending order, including schematic signal names and onboard or header connections.

| GPIO | Schematic Signal | Connected To | Notes |
|---:|---|---|---|
| GPIO0 | `IO0` / `GPIO0` | BOOT button | Strapping pin; hold during reset to enter download mode |
| GPIO1 | `IO1` / `GPIO1` | - | - |
| GPIO2 | `IO2` / `GPIO2` | - | - |
| GPIO3 | `IO3` / `GPIO3` | P1 expansion header | P1-9 |
| GPIO4 | `IO4` / `GPIO4` | P1 expansion header | P1-11 |
| GPIO5 | `IO5` / `GPIO5` | P1 expansion header | P1-13 |
| GPIO6 | `IO6` / `GPIO6` | P1 expansion header | P1-15 |
| GPIO7 | `IO7` / `GPIO7` | P1 expansion header | P1-17 |
| GPIO8 | `IO8` / `GPIO8` | P1 expansion header | P1-19 |
| GPIO9 | `IO9` / `GPIO9` | P1 expansion header | P1-20 |
| GPIO10 | `IO10` / `GPIO10` | P1 expansion header | P1-18 |
| GPIO11 | `IO11` / `GPIO11` | P1 expansion header | P1-16 |
| GPIO12 | `IO12` / `GPIO12` | P1 expansion header | P1-14 |
| GPIO13 | `IO13` / `GPIO13` | P1 expansion header | P1-12 |
| GPIO14 | `IO14` / `GPIO14` | P1 expansion header | P1-10 |
| GPIO15 | `TXD1` | CAN CH1 TX, routed through an isolator to the CAN transceiver | Example defines `WS_CAN1_TX_GPIO=15` |
| GPIO16 | `RXD1` | CAN CH1 RX, routed through an isolator to the CAN transceiver | Example defines `WS_CAN1_RX_GPIO=16` |
| GPIO17 | `XL2515_CS` | CAN CH2 external controller chip select | Example defines `WS_XL2515_CS_GPIO=17` |
| GPIO18 | `XL2515_INT` | CAN CH2 external controller interrupt | Example defines `WS_XL2515_INT_GPIO=18` |
| GPIO19 | `D_N` / `USB_N` | USB Type-C D- / P1 expansion header | ESP32-S3 native USB, P1-8 |
| GPIO20 | `D_P` / `USB_P` | USB Type-C D+ / P1 expansion header | ESP32-S3 native USB, P1-6 |
| GPIO21 | `XL2515_SCLK` | CAN CH2 external controller SPI SCLK | Example defines `WS_XL2515_SCLK_GPIO=21` |
| GPIO33 | `IO33` | - | Not recommended as a general-purpose GPIO |
| GPIO34 | `IO34` | - | Not recommended as a general-purpose GPIO |
| GPIO35 | `IO35` | - | Not recommended as a general-purpose GPIO |
| GPIO36 | `IO36` | - | Not recommended as a general-purpose GPIO |
| GPIO37 | `IO37` | - | Not recommended as a general-purpose GPIO |
| GPIO38 | `RTC_SCL` | PCF85063AT SCL | Example defines `WS_I2C_SCL_IO=38` |
| GPIO39 | `RTC_SDA` | PCF85063AT SDA | Example defines `WS_I2C_SDA_IO=39` |
| GPIO40 | `RTC_INT` | PCF85063AT INT | Example defines `WS_RTC_INT_GPIO=40` |
| GPIO41 | `XL2515_MOSI` | CAN CH2 external controller SPI MOSI | Example defines `WS_XL2515_MOSI_GPIO=41` |
| GPIO42 | `XL2515_MISO` | CAN CH2 external controller SPI MISO | Example defines `WS_XL2515_MISO_GPIO=42` |
| GPIO43 | `TXD` / `U0TXD` | UART0 TX / P1 expansion header | Default log UART TX, P1-5 |
| GPIO44 | `RXD` / `U0RXD` | UART0 RX / P1 expansion header | Default log UART RX, P1-7 |
| GPIO45 | `IO45` | - | ESP32-S3 strapping-related pin; use with caution |
| GPIO46 | `IO46` | - | ESP32-S3 strapping-related pin, generally input-only; use with caution |
| GPIO47 | `IO47` | - | - |
| GPIO48 | `IO48` | - | - |

## 4. CAN Interfaces

| Channel | Controller | MCU-side Interface | Isolation / Transceiver | External Interface | Termination |
|---|---|---|---|---|---|
| CAN CH1 | ESP32-S3 internal TWAI | TX=`GPIO15`, RX=`GPIO16` | Digital isolator + CAN transceiver | `CAN_1` / `CANH_0` / `CANL_0` | `R23` 120R |
| CAN CH2 | XL2515QF20 | SPI: CS=`GPIO17`, INT=`GPIO18`, SCLK=`GPIO21`, MOSI=`GPIO41`, MISO=`GPIO42` | Digital isolator + CAN transceiver | `CAN_2` / `CANH_1` / `CANL_1` | `R30` 120R |

The CAN bus side of CH1 and CH2 uses an isolated power domain, marked in the schematic as `Relay-5V`, `Relay-3V3`, and `SGND`. When connecting an external CAN bus, distinguish the logic-side `GND` from the isolated-side `SGND`.

## 5. I2C / RTC

| Device | Component | I2C Address | Pins |
|---|---|---:|---|
| RTC | PCF85063AT | `0x51` | SCL=`GPIO38`, SDA=`GPIO39`, INT=`GPIO40` |

The RTC uses the 32.768 kHz crystal `Y2`; SCL/SDA have 10K pull-ups to `3V3`.

## 6. Expansion Header

The schematic shows `P1` as a 20-pin expansion header:

| P1 Pin | Signal | P1 Pin | Signal |
|---:|---|---:|---|
| 1 | `3V3` | 2 | `5V` |
| 3 | `GND` | 4 | `GND` |
| 5 | `TXD(GPIO43)` | 6 | `D_P(GPIO20)` |
| 7 | `RXD(GPIO44)` | 8 | `D_N(GPIO19)` |
| 9 | `IO3(GPIO3)` | 10 | `IO14(GPIO14)` |
| 11 | `IO4(GPIO4)` | 12 | `IO13(GPIO13)` |
| 13 | `IO5(GPIO5)` | 14 | `IO12(GPIO12)` |
| 15 | `IO6(GPIO6)` | 16 | `IO11(GPIO11)` |
| 17 | `IO7(GPIO7)` | 18 | `IO10(GPIO10)` |
| 19 | `IO8(GPIO8)` | 20 | `IO9(GPIO9)` |

Use the schematic `P1` symbol and board silkscreen / pinout image as the final reference for physical pin order. GPIOs occupied by CAN, USB, RTC, and Flash should not be reused as normal expansion GPIOs.

## 7. Usage Notes

- When troubleshooting peripherals in Arduino / ESP32 projects, first check that the board target is ESP32-S3, and verify the port, baud rate, `USB CDC On Boot`, and `Core Debug Level` IDE/build settings.
- `GPIO15/GPIO16` are used for CAN CH1 TWAI TX/RX. Do not reuse them for other peripherals.
- `GPIO17/GPIO18/GPIO21/GPIO41/GPIO42` are used for CAN CH2 XL2515 SPI and interrupt. Do not casually share them with other SPI devices unless the software explicitly handles bus arbitration and chip select.
- `GPIO19/GPIO20` are connected to USB Type-C D-/D+ and are not recommended for use as normal GPIO.
- `GPIO38/GPIO39/GPIO40` are used by the PCF85063AT RTC; avoid I2C address conflicts with `0x51` when adding external I2C devices.
- `GPIO0` is the BOOT button / strapping pin, and `RESET` is the `CHIP_PU` reset signal. They are not recommended as normal user inputs.
- `GPIO33` to `GPIO37` are not recommended as normal GPIO.
- When debugging the CAN bus, in addition to the software bit rate, check that the external bus has proper termination, CANH/CANL are not swapped, and the isolated-side ground reference matches the field wiring requirements.

## 8. References

| Type | Link |
|---|---|
| Chinese Wiki | https://docs.waveshare.net/ESP32-S3-CAN-2CH/ |
| English Documentation | https://docs.waveshare.com/ESP32-S3-CAN-2CH |
| Schematic | `hardware/schematics/ESP32-S3-CAN-2CH.pdf` |
| ESP-IDF BSP pin definitions | `examples/ESP-IDF-v5.5.2/01_WIFI_AP/components/esp_bsp/bsp_common.h` |
