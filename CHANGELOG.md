# Changelog

## v1.0.0 -- Initial Release

### Firmware Types
- **BLE Client** -- RNode-compatible BLE bridge for Sideband/Columba mobile apps
- **Retranslator** -- Reticulum transport node with path learning and forwarding
- **UART Client** -- RNode KISS bridge over USB serial at 921600 baud

### Supported Hardware
- ESP32-C3 (with 0.42" SSD1306 OLED)
- ESP32-S3 (with RGB NeoPixel LED, PSRAM variants: R2/R8)
- ESP32 classic (DevKit, WROVER with 4MB PSRAM, ESP-CAM with 2MB PSRAM)
- ESP32-C6
- ESP32-S2 (retranslator and UART only)
- LilyGO T-Display (ST7789 TFT 135x240)

### Features
- ESP-NOW v2.0 with 1470-byte payloads (requires pioarduino platform)
- 802.11 Long Range mode (~500m line-of-sight)
- BLE pairing with on-screen PIN display (passkey authentication)
- Display abstraction: SSD1306 OLED, ST7789 TFT, headless (NullDisplay)
- Factory reset via 5-second BOOT button hold (retranslator)
- First boot auto-initialization with identity generation
- Display and LED toggle via double-tap BOOT button (NVS persisted)
- Active-low LED support for C3 boards
- RGB NeoPixel LED support for S3 boards (requires solder bridge)
- Dual serial output on S3 boards (USB CDC + UART)
- Chip temperature display (built-in sensor)
- Runtime PSRAM detection with automatic table sizing
- Debug build environments with verbose Reticulum logging
- Broadcast storm prevention (own-MAC filter on ESP-NOW RX)
- ESP-NOW RX queue drop counter for monitoring

### Path Storage Capacity (Retranslator)
- ESP32-C3: 48 paths (small networks, up to 20 nodes)
- ESP32 classic: 128 paths
- ESP32-WROVER (4MB PSRAM): 4096 paths
- ESP32-CAM (2MB PSRAM): 1024 paths
- ESP32-S3 R2 (2MB PSRAM): 1024 paths
- ESP32-S3 R8 (8MB PSRAM): 4096 paths

### Known Limitations
- C3 retranslator: heap pressure under heavy announce traffic (upstream microReticulum issue)
- Periodic transport re-announce disabled due to stability issues on constrained hardware
- ESP32-S2: no BLE (retranslator and UART client only)
