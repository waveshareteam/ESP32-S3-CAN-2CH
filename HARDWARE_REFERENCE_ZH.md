# ESP32-S3-CAN-2CH 硬件参考

> 给开发者和 AI 工具使用的硬件速查文档。内容以本仓库 `hardware/schematics/ESP32-S3-CAN-2CH.pdf` 原理图为主，并参考当前示例 / BSP 中的软件引脚定义进行核对。

## 1. 板卡概览

- 产品：`ESP32-S3-CAN-2CH`
- 主控：`ESP32-S3R8`
- 外置 Flash：`XM25QH128DHIQT`，128 Mbit / 16 MB
- 主要板载资源：两路 CAN、USB Type-C、PCF85063AT RTC、32.768 kHz RTC 晶振、40 MHz 主晶振、隔离电源、USB 转 ESP32-S3 原生 USB 下载/日志接口、UART 扩展接口、20Pin 扩展排针
- CAN 通道：
  - CAN CH1：ESP32-S3 内部 TWAI 控制器 + 隔离 + CAN 收发器
  - CAN CH2：`XL2515QF20` 外部 CAN 控制器 + 隔离 + CAN 收发器

## 2. 板载外设

| 模块 | 器件 / 功能 | 接口 | 参数 | GPIO / 信号 |
|---|---|---|---|---|
| 主控 | ESP32-S3R8 | - | Wi-Fi / BLE，内置 PSRAM 型号封装 | - |
| 外置 Flash | XM25QH128DHIQT | SPI Flash | 128 Mbit / 16 MB | `SPID` / `SPIQ` / `SPICLK` / `SPICS0` / `SPIWP` / `SPIHD` |
| CAN CH1 | ESP32-S3 TWAI + CAN 收发器 | TWAI | 板载 120R 终端电阻位置 `R23` | TX=`GPIO15` (`TXD1`)，RX=`GPIO16` (`RXD1`) |
| CAN CH2 | XL2515QF20 + CAN 收发器 | SPI | 16 MHz 晶振，板载 120R 终端电阻位置 `R30` | CS=`GPIO17`，INT=`GPIO18`，SCLK=`GPIO21`，MOSI=`GPIO41`，MISO=`GPIO42` |
| RTC | PCF85063AT | I2C | 7-bit 地址通常为 `0x51`，32.768 kHz 晶振 | SCL=`GPIO38`，SDA=`GPIO39`，INT=`GPIO40` |
| USB Type-C | ESP32-S3 原生 USB | USB | 下载、日志、USB 通信 | D-=`GPIO19` (`USB_N` / `D_N`)，D+=`GPIO20` (`USB_P` / `D_P`) |
| UART0 | 默认串口 | UART | 扩展排针引出 | TX=`U0TXD` / `GPIO43`，RX=`U0RXD` / `GPIO44` |
| BOOT 按键 | Key1 | GPIO / strapping | 下载模式按键 | `GPIO0` |
| RESET 按键 | Key2 | 复位 | `CHIP_PU` / `RESET` | `RESET` |
| 3.3 V 电源 | MP1605GTF-Z | 电源 | 5 V 转系统 3.3 V | `3V3` |
| 隔离 5 V | B0505LS-1W | 隔离电源 | CAN 隔离侧供电 | `Relay-5V` / `SGND` |
| 隔离 3.3 V | ME6217C33M5G | 电源 | 隔离侧 3.3 V | `Relay-3V3` / `SGND` |

## 3. GPIO 分配

下表按 ESP32-S3 GPIO 编号升序列出原理图中明确占用或标注的信号。

| GPIO | 原理图信号 | 连接到 | 备注 |
|---:|---|---|---|
| GPIO0 | `IO0` / `GPIO0` | BOOT 按键 | Strapping pin，按住复位可进入下载模式 |
| GPIO1 | `IO1` / `GPIO1` | - | - |
| GPIO2 | `IO2` / `GPIO2` | - | - |
| GPIO3 | `IO3` / `GPIO3` | P1 扩展接口 | P1-9 |
| GPIO4 | `IO4` / `GPIO4` | P1 扩展接口 | P1-11 |
| GPIO5 | `IO5` / `GPIO5` | P1 扩展接口 | P1-13 |
| GPIO6 | `IO6` / `GPIO6` | P1 扩展接口 | P1-15 |
| GPIO7 | `IO7` / `GPIO7` | P1 扩展接口 | P1-17 |
| GPIO8 | `IO8` / `GPIO8` | P1 扩展接口 | P1-19 |
| GPIO9 | `IO9` / `GPIO9` | P1 扩展接口 | P1-20 |
| GPIO10 | `IO10` / `GPIO10` | P1 扩展接口 | P1-18 |
| GPIO11 | `IO11` / `GPIO11` | P1 扩展接口 | P1-16 |
| GPIO12 | `IO12` / `GPIO12` | P1 扩展接口 | P1-14 |
| GPIO13 | `IO13` / `GPIO13` | P1 扩展接口 | P1-12 |
| GPIO14 | `IO14` / `GPIO14` | P1 扩展接口 | P1-10 |
| GPIO15 | `TXD1` | CAN CH1 TX，接隔离器后到 CAN 收发器 | 示例中 `WS_CAN1_TX_GPIO=15` |
| GPIO16 | `RXD1` | CAN CH1 RX，接隔离器后到 CAN 收发器 | 示例中 `WS_CAN1_RX_GPIO=16` |
| GPIO17 | `XL2515_CS` | CAN CH2 外部控制器片选 | 示例中 `WS_XL2515_CS_GPIO=17` |
| GPIO18 | `XL2515_INT` | CAN CH2 外部控制器中断 | 示例中 `WS_XL2515_INT_GPIO=18` |
| GPIO19 | `D_N` / `USB_N` | USB Type-C D- / P1 扩展接口 | ESP32-S3 原生 USB，P1-8 |
| GPIO20 | `D_P` / `USB_P` | USB Type-C D+ / P1 扩展接口 | ESP32-S3 原生 USB，P1-6 |
| GPIO21 | `XL2515_SCLK` | CAN CH2 外部控制器 SPI SCLK | 示例中 `WS_XL2515_SCLK_GPIO=21` |
| GPIO33 | `IO33` | - | 不建议作为普通 GPIO |
| GPIO34 | `IO34` | - | 不建议作为普通 GPIO |
| GPIO35 | `IO35` | - | 不建议作为普通 GPIO |
| GPIO36 | `IO36` | - | 不建议作为普通 GPIO |
| GPIO37 | `IO37` | - | 不建议作为普通 GPIO |
| GPIO38 | `RTC_SCL` | PCF85063AT SCL | 示例中 `WS_I2C_SCL_IO=38` |
| GPIO39 | `RTC_SDA` | PCF85063AT SDA | 示例中 `WS_I2C_SDA_IO=39` |
| GPIO40 | `RTC_INT` | PCF85063AT INT | 示例中 `WS_RTC_INT_GPIO=40` |
| GPIO41 | `XL2515_MOSI` | CAN CH2 外部控制器 SPI MOSI | 示例中 `WS_XL2515_MOSI_GPIO=41` |
| GPIO42 | `XL2515_MISO` | CAN CH2 外部控制器 SPI MISO | 示例中 `WS_XL2515_MISO_GPIO=42` |
| GPIO43 | `TXD` / `U0TXD` | UART0 TX / P1 扩展接口 | 默认日志串口 TX，P1-5 |
| GPIO44 | `RXD` / `U0RXD` | UART0 RX / P1 扩展接口 | 默认日志串口 RX，P1-7 |
| GPIO45 | `IO45` | - | ESP32-S3 strapping 相关，使用需谨慎 |
| GPIO46 | `IO46` | - | ESP32-S3 strapping 相关，通常仅输入，使用需谨慎 |
| GPIO47 | `IO47` | - | - |
| GPIO48 | `IO48` | - | - |

## 4. CAN 接口

| 通道 | 控制器 | MCU 侧接口 | 隔离 / 收发 | 外部接口 | 终端电阻 |
|---|---|---|---|---|---|
| CAN CH1 | ESP32-S3 内部 TWAI | TX=`GPIO15`，RX=`GPIO16` | 数字隔离器 + CAN 收发器 | `CAN_1` / `CANH_0` / `CANL_0` | `R23` 120R |
| CAN CH2 | XL2515QF20 | SPI：CS=`GPIO17`，INT=`GPIO18`，SCLK=`GPIO21`，MOSI=`GPIO41`，MISO=`GPIO42` | 数字隔离器 + CAN 收发器 | `CAN_2` / `CANH_1` / `CANL_1` | `R30` 120R |

CH1 和 CH2 的 CAN 总线侧使用隔离电源域，原理图中以 `Relay-5V`、`Relay-3V3` 和 `SGND` 标注。接外部 CAN 总线时注意区分逻辑侧 `GND` 与隔离侧 `SGND`。

## 5. I2C / RTC

| 设备 | 器件 | I2C 地址 | 引脚 |
|---|---|---:|---|
| RTC | PCF85063AT | `0x51` | SCL=`GPIO38`，SDA=`GPIO39`，INT=`GPIO40` |

原理图中 RTC 使用 32.768 kHz 晶振 `Y2`，SCL/SDA 有 10K 上拉到 `3V3`。

## 6. 扩展接口

原理图中的 `P1` 为 20Pin 扩展排针，可见信号包括：

| P1 引脚 | 信号 | P1 引脚 | 信号 |
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

具体排针脚序请以原理图 `P1` 和板载丝印 / pinout 图为准；占用在 CAN、USB、RTC、Flash 上的 GPIO 不建议再作为普通扩展 GPIO 使用。

## 7. 使用注意

- Arduino / ESP32 项目排查外设不工作时，优先检查板型是否选择 ESP32-S3、端口、波特率、`USB CDC On Boot`、`Core Debug Level` 等 IDE/编译设置。
- `GPIO15/GPIO16` 已用于 CAN CH1 的 TWAI TX/RX，不要与其它外设复用。
- `GPIO17/GPIO18/GPIO21/GPIO41/GPIO42` 已用于 CAN CH2 的 XL2515 SPI 与中断，不要与其它 SPI 设备随意共用，除非软件显式处理总线仲裁和片选。
- `GPIO19/GPIO20` 已连接 USB Type-C 的 D-/D+，不建议作为普通 GPIO 使用。
- `GPIO38/GPIO39/GPIO40` 已用于 PCF85063AT RTC；外接 I2C 设备时避免与 `0x51` 地址冲突。
- `GPIO0` 是 BOOT 按键 / strapping pin，`RESET` 是 `CHIP_PU` 复位信号，不建议作为普通用户输入使用。
- `GPIO33` 至 `GPIO37` 不建议作为普通 GPIO 使用。
- CAN 总线调试时，除代码波特率外，还要确认外部总线是否有正确终端电阻、CANH/CANL 是否接反、隔离侧地参考是否符合现场接线要求。

## 8. 参考资料

| 类型 | 链接 |
|---|---|
| 中文 Wiki | https://docs.waveshare.net/ESP32-S3-CAN-2CH/ |
| 英文文档 | https://docs.waveshare.com/ESP32-S3-CAN-2CH |
| 原理图 | `hardware/schematics/ESP32-S3-CAN-2CH.pdf` |
| ESP-IDF BSP 引脚定义 | `examples/ESP-IDF-v5.5.2/01_WIFI_AP/components/esp_bsp/bsp_common.h` |
