# Reticulum ESP-NOW Mesh Firmware

ESP-NOW based mesh networking firmware for ESP32 microcontrollers. Provides Reticulum network connectivity through BLE and UART interfaces for off-grid encrypted mesh communication.

## Features

- ESP-NOW v2.0 mesh with 802.11 Long Range mode (~500m LOS)
- BLE bridge for Sideband/Columba mobile apps (RNode compatible)
- UART bridge for PC/laptop Reticulum clients (921600 baud)
- Transport node (retranslator) with path learning and forwarding
- Support for ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
- SSD1306 OLED and ST7789 TFT display support
- Passkey BLE pairing with on-screen PIN
- Factory reset, display toggle, NVS persistence
- 20 build environments across 6 chip families

## Documentation

- [English Documentation](docs/README.md)
- [Russian Documentation (Dokumentatsiya na russkom)](docs/README_RU.md)
- [HTML Manual with Screen Mockups](docs/manual.html)

## Quick Start

```bash
# Build BLE client for ESP32-C3
pio run -e client_c3

# Build retranslator
pio run -e retranslator_c3

# Build UART client
pio run -e uart_c3

# Flash
pio run -e client_c3 -t upload
```

## Firmware Types

| Type | Purpose | Host Connection |
|------|---------|----------------|
| Client | Phone-to-mesh bridge | BLE (Sideband/Columba) |
| Retranslator | Mesh relay/router | None (infrastructure) |
| UART Client | PC-to-mesh bridge | USB Serial (921600 baud) |

## License

See [docs/README.md](docs/README.md) for component licenses.
