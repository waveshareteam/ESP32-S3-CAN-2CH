# Factory Firmware

[English](README.md) | [简体中文](README_ZH.md)

This directory stores the tested factory image for the ESP32-S3-CAN-2CH development board.

| File | Purpose |
| --- | --- |
| [ESP32-S3-CAN-2CH_FactoryOnly_20260920.bin](ESP32-S3-CAN-2CH_FactoryOnly_20260920.bin) | Factory recovery firmware for the ESP32-S3-CAN-2CH board, dated 20260920. Flash from `0x0`. |

Before flashing:

- Back up any device-specific data that must be preserved.
- Confirm the board model and hardware revision.
- Erase the target device before writing the merged image.
- Follow the flashing procedure in the product documentation.

SHA-256: `F385BF20ECABA0525C804423E40140B11B5FEECCC8821FD0FE9B586E81C1D503`

This file is a single merged BIN for factory recovery.
