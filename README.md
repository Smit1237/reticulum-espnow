# Reticulum ESP-NOW Mesh Firmware

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

ESP-NOW mesh networking firmware for ESP32 microcontrollers providing [Reticulum](https://reticulum.network/) network connectivity for off-grid encrypted communication.

```
  Phone (Sideband/Columba)          PC / Laptop
         |  BLE                       |  USB Serial (921600 baud)
    [ ESP32 Client ]             [ ESP32 UART Client ]
         |  ESP-NOW                   |  ESP-NOW
    ~~~~ mesh ~~~~~~~~~~~~~~~~~~~~~ mesh ~~~~
         |                |                |
   [ Retranslator ]  [ Retranslator ]  [ Retranslator ]
     (transport)      (transport)      (transport)
```

## Features

- **ESP-NOW v2.0** mesh with 1470-byte payloads and 802.11 Long Range mode (~500m LOS)
- **BLE bridge** for [Sideband](https://github.com/markqvist/Sideband) / [Columba](https://github.com/markqvist/Columba) mobile apps (RNode-compatible KISS protocol)
- **UART bridge** for PC/laptop Reticulum clients at 921600 baud
- **Transport node** (retranslator) with path learning, forwarding, and persistent identity
- **BLE optimization**: MTU 517, Data Length Extension (DLE), 2M PHY on BLE 5.0 chips, MTU-aware notification fragmentation
- **Event-driven architecture**: CPU sleeps while idle, wakes instantly on mesh data
- **Display support**: SSD1306 OLED (72x40, 128x64) and ST7789 TFT (135x240 color)
- **Passkey BLE pairing** with on-screen PIN display
- **Factory reset** via 5s BOOT hold, display toggle via double-tap
- **37 build environments** across 6 chip families

## Supported Hardware

| Chip | BLE | Display | PSRAM | Notes |
|------|-----|---------|-------|-------|
| ESP32-C3 | 5.0 (2M PHY) | SSD1306 OLED | No | Primary dev board |
| ESP32-S3 | 5.0 (2M PHY) | None | 2/8MB variants | Best for transport nodes |
| ESP32 classic | 4.2 | None | No | Generic DevKit |
| ESP32-WROVER | 4.2 | None | 4MB | Good for transport |
| ESP32-CAM | 4.2 | None | 2MB | Camera not used, PSRAM free |
| ESP32-C6 | 5.0 (2M PHY) | None | No | WiFi 6 |
| ESP32-S2 | None | None | No | Retranslator/RNode UART/Serial only |
| LilyGO T-Display | 4.2 | ST7789 TFT | No | Color display |

## Firmware Types

| Type | Purpose | Host Connection | Build prefix |
|------|---------|----------------|-------------|
| **Client** | Phone-to-mesh bridge | BLE (Sideband/Columba) | `client_*` |
| **Retranslator** | Mesh relay/router | None (infrastructure) | `retranslator_*` |
| **RNode UART** | PC-to-mesh bridge (RNodeInterface) | USB Serial (921600 baud) | `uart_*` |
| **Serial Bridge** | PC-to-mesh bridge (SerialInterface) | USB Serial HDLC (921600 baud) | `serial_*` |

## Quick Start

```bash
# Install PlatformIO
pip install platformio

# Build and flash BLE client for ESP32-C3
pio run -e client_c3 -t upload

# Build retranslator for ESP32-S3 with 8MB PSRAM
pio run -e retranslator_s3_r8 -t upload

# Build UART client for T-Display
pio run -e uart_tdisplay -t upload
```

## Web Flasher

Flash firmware directly from your browser (Chrome/Edge) -- no PlatformIO needed:

**[Open Web Flasher](https://smit1237.github.io/reticulum-espnow/flasher/)**

The flasher supports all released versions with automatic chip detection and firmware selection.

## Build Environments

| Environment | Chip | Type | Display |
|-------------|------|------|---------|
| `client_c3` | C3 | BLE Client | SSD1306 OLED |
| `client_s3` | S3 | BLE Client | None |
| `client_esp32` | ESP32 | BLE Client | None |
| `client_espcam` | ESP32-CAM | BLE Client | None |
| `client_c3_generic` | C3 (headless) | BLE Client | None |
| `client_c6` | C6 | BLE Client | None |
| `client_tdisplay` | ESP32 | BLE Client | ST7789 TFT |
| `retranslator_c3` | C3 | Transport | SSD1306 OLED |
| `retranslator_s3` | S3 | Transport | None |
| `retranslator_s3_r2` | S3 (2MB PSRAM) | Transport | None |
| `retranslator_s3_r8` | S3 (8MB PSRAM) | Transport | None |
| `retranslator_esp32` | ESP32 | Transport | None |
| `retranslator_wrover` | WROVER (4MB PSRAM) | Transport | None |
| `retranslator_espcam` | ESP32-CAM (2MB PSRAM) | Transport | None |
| `retranslator_c6` | C6 | Transport | None |
| `retranslator_s2` | S2 | Transport | None |
| `retranslator_tdisplay` | ESP32 | Transport | ST7789 TFT |
| `retranslator_c3_generic` | C3 (headless) | Transport | None |
| `uart_c3` | C3 | RNode UART | SSD1306 OLED |
| `uart_s3` | S3 | RNode UART | None |
| `uart_esp32` | ESP32 | RNode UART | None |
| `uart_c6` | C6 | RNode UART | None |
| `uart_s2` | S2 | RNode UART | None |
| `uart_tdisplay` | ESP32 | RNode UART | ST7789 TFT |
| `uart_c3_generic` | C3 (headless) | RNode UART | None |
| `uart_espcam` | ESP32-CAM | RNode UART | None (115200 baud) |
| `serial_c3` | C3 | Serial Bridge | SSD1306 OLED |
| `serial_s3` | S3 | Serial Bridge | None |
| `serial_esp32` | ESP32 | Serial Bridge | None |
| `serial_c6` | C6 | Serial Bridge | None |
| `serial_s2` | S2 | Serial Bridge | None |
| `serial_c3_generic` | C3 (headless) | Serial Bridge | None |
| `serial_espcam` | ESP32-CAM | Serial Bridge | None (115200 baud) |

Debug variants available: `retranslator_c3_debug`, `retranslator_esp32_debug`, `retranslator_wrover_debug`, `retranslator_espcam_debug`

Headless C3 variants (`*_c3_generic`) force `DISPLAY_TYPE=DISPLAY_NONE` for bare ESP32-C3 Super Mini / DevKitM-1 boards without the 0.42" OLED — prevents I2C `ESP_ERR_INVALID_STATE` errors on absent display hardware.

## Documentation

- [English Documentation](docs/README.md) -- full technical reference
- [Russian Documentation](docs/README_RU.md) -- complete Russian translation
- [HTML Manual](https://smit1237.github.io/reticulum-espnow/docs/manual.html) -- interactive manual with screen mockups (EN/RU)
- [Changelog](CHANGELOG.md) -- version history

## License

This project is licensed under the [MIT License](LICENSE).

### Dependencies

| Component | License |
|-----------|---------|
| [microReticulum](https://github.com/attermann/microReticulum) | Apache 2.0 |
| [Reticulum](https://github.com/markqvist/reticulum) | MIT |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | Apache 2.0 |
| [U8g2](https://github.com/olikraus/U8g2) | BSD |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | Mixed (FreeBSD) |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | MIT |
| [MsgPack](https://github.com/FRAMEWORKIOT/MsgPack) | MIT |
