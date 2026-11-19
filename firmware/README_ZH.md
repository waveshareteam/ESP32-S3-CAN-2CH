# 出厂固件

[English](README.md) | [简体中文](README_ZH.md)

本目录保存 ESP32-S3-CAN-2CH 开发板的实测出厂镜像。

| 文件 | 用途 |
| --- | --- |
| [ESP32-S3-CAN-2CH_FactoryOnly_20260920.bin](ESP32-S3-CAN-2CH_FactoryOnly_20260920.bin) | ESP32-S3-CAN-2CH 出厂恢复固件，日期为 20260920，从 `0x0` 地址开始烧录。 |

烧录前：

- 备份需要保留的设备专用数据。
- 确认开发板型号和硬件版本。
- 烧录合并镜像前先整片擦除目标设备。
- 按产品文档中的烧录流程操作。

SHA-256：`F385BF20ECABA0525C804423E40140B11B5FEECCC8821FD0FE9B586E81C1D503`

该文件是用于恢复出厂功能的单个合并 BIN。
