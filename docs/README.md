# Reticulum ESP-NOW Mesh Firmware

ESP-NOW based mesh networking firmware for ESP32 microcontrollers, providing Reticulum network connectivity through BLE and UART interfaces. Enables off-grid, encrypted mesh communication using Sideband, Columba, and other Reticulum-compatible applications.

## Architecture

```
Phone (Sideband/Columba)         ESP32 Client Node           ESP32 Retranslator          ESP32 Client Node         Phone
+-------------------+          +------------------+        +------------------+        +------------------+     +-------------------+
| Reticulum Stack   |<--BLE-->| KISS Protocol    |<-ESP-->| Reticulum        |<-ESP-->| KISS Protocol    |<-->| Reticulum Stack   |
| LXMF Messaging    |  NUS    | ESP-NOW v2       | NOW    | Transport Stack  | NOW    | ESP-NOW v2       |BLE | LXMF Messaging    |
+-------------------+         +------------------+        +------------------+        +------------------+    +-------------------+
```

The system consists of three firmware types:

- **Client** -- BLE bridge between a phone running Reticulum and the ESP-NOW mesh
- **Retranslator** -- Transport node that routes and relays packets across the mesh
- **UART Client** -- Serial bridge between a PC running Reticulum and the mesh

All nodes communicate over ESP-NOW v2.0 with 802.11 Long Range mode, achieving up to 500m line-of-sight range with 1470-byte payload capacity.

## Supported Hardware

| Board | Chip | Display | BLE | Client | Retranslator | UART |
|-------|------|---------|-----|--------|-------------|------|
| ESP32-C3 DevKit + OLED | ESP32-C3 | SSD1306 72x40 | 5.0 | Yes | Yes | Yes |
| LilyGO T-Display | ESP32 | ST7789 135x240 | 4.2 | Yes | Yes | Yes |
| ESP32-S3 DevKitC | ESP32-S3 | None | 5.0 | Yes | Yes | Yes |
| ESP32 DevKit | ESP32 | None | 4.2 | Yes | Yes | Yes |
| ESP32-C6 DevKitC | ESP32-C6 | None | 5.0 | Yes | Yes | Yes |
| ESP32-S2 Saola | ESP32-S2 | None | No | No | Yes | Yes |

ESP32-S3 retranslator variants with PSRAM:
- **N16R8 / N8R8** (8MB octal PSRAM): up to 4096 paths
- **N4R2 / N8R2 / N16R2** (2MB quad PSRAM): up to 1024 paths
- **No PSRAM**: up to 128 paths

## Quick Start

### Prerequisites

- PlatformIO CLI or IDE
- USB cable for the target board
- For BLE client: Android phone with Sideband or Columba app

### Build

```bash
# Clone the repository
git clone <repository-url>
cd reticulum-espnow

# Build for ESP32-C3 client (default)
pio run -e client_c3

# Build for retranslator
pio run -e retranslator_c3

# Build for UART client
pio run -e uart_c3
```

### Flash

```bash
# Flash to connected board (auto-detect port)
pio run -e client_c3 -t upload

# Flash to specific port
pio run -e client_c3 -t upload --upload-port COM24
```

### Monitor Serial Output

```bash
pio device monitor
```

For UART client firmware, serial is used for KISS protocol at 921600 baud. Use a separate terminal or the device display for status.

## Configuration

All configuration is in `include/board_config.h`.

### ESP-NOW Channel

```c
#define ESPNOW_CHANNEL  13      // WiFi channel (1-13)
```

All nodes in the mesh must use the same channel. Use the channel scanner tool to find the least congested channel.

### Long Range Mode

```c
#define ESPNOW_LONG_RANGE  1    // 1 = enabled (default), 0 = standard
```

802.11 LR mode doubles range (~500m line-of-sight) at the cost of reduced throughput (512/256 Kbps vs 1 Mbps). Supported on all ESP32 variants except ESP32-C2. All mesh nodes must use the same setting.

### Transport Announce Interval

```c
#define TRANSPORT_ANNOUNCE_INTERVAL_MS  (2 * 60 * 60 * 1000UL)  // 2 hours
```

How often the retranslator re-announces itself on the mesh. Relevant for multi-hop chain deployments.

### Debug Build

Build with verbose Reticulum logging:

```bash
pio run -e retranslator_c3_debug
```

Release builds use LOG_WARNING level (minimal output). Debug builds use LOG_TRACE (full verbose).

## User Controls

### BLE Client (main_client.cpp)

| Action | Duration | Result |
|--------|----------|--------|
| Double-tap BOOT | 2 quick taps | Toggle display and LEDs on/off (persisted) |
| Hold BOOT 2+ seconds | Release after 2s | Enter BLE pairing mode |

### Retranslator (main_retranslator.cpp)

| Action | Duration | Result |
|--------|----------|--------|
| Double-tap BOOT | 2 quick taps | Toggle display and LEDs on/off (persisted) |
| Hold BOOT 5+ seconds | Keep holding | Factory reset (clears identity and paths) |

### UART Client (main_uart_client.cpp)

| Action | Duration | Result |
|--------|----------|--------|
| Double-tap BOOT | 2 quick taps | Toggle display on/off (persisted) |

## BLE Pairing

### With Sideband (Android)

1. Flash the BLE client firmware to a board
2. Power on the board -- it begins BLE advertising as "RNode XXXX" (XXXX = last 2 bytes of MAC)
3. In Sideband, go to Settings > Connectivity > enable "Connect via RNode"
4. Go to Hardware > RNode > check "Device requires BLE"
5. Tap "Scan" -- the device should appear
6. Tap to pair -- a 6-digit PIN appears on the device display (or serial output for screenless boards)
7. Enter the PIN on the phone
8. After successful pairing, Sideband shows the RNode interface as active

### Re-pairing

To pair with a different phone:
1. Hold BOOT button for 2+ seconds to enter pairing mode
2. This clears existing bonds and restarts advertising
3. The display shows "PAIRING..." and the LED blinks blue
4. Follow the pairing steps above with the new phone

### Screenless Boards

On boards without a display (ESP32-S3, ESP32, ESP32-C6), the pairing PIN is printed to the serial console every 10 seconds:

```
*** PAIRING PIN: 474567 ***
```

Connect via USB serial monitor at 115200 baud to see the PIN. On ESP32-S3 boards with dual USB ports, the PIN appears on both ports.

## UART Setup

### Reticulum Configuration

Add to your Reticulum config file (`~/.reticulum/config`):

```ini
[[ESP-NOW Serial]]
  type = RNodeInterface
  port = /dev/ttyUSB0
  speed = 921600
```

On Windows:
```ini
[[ESP-NOW Serial]]
  type = RNodeInterface
  port = COM11
  speed = 921600
```

The UART client emulates the RNode KISS protocol, so Reticulum connects to it as if it were a standard RNode device.

## Display Information

### SSD1306 OLED (72x40, ESP32-C3)

The display shows four lines of information:

**Client -- Connected:**
```
[*] RNode C6D4
TX: 1234
RX: 5678
32.5 C
```

**Client -- Disconnected:**
```
[O] RNode C6D4
TX: 0
RX: 0
28.1 C
```

**Client -- Pairing Mode:**
```
[O] RNode C6D4
PAIRING...
Connect from
phone now
```

**Client -- PIN Display:**
```
PAIR PIN:
  474567
```

**Retranslator:**
```
[~] TRANSPORT
TX: 167
RX: 4521
31.2 C
```

Icons: [*] = BLE connected (filled circle), [O] = BLE disconnected (empty circle), [~] = radio/transport

### ST7789 TFT (135x240, T-Display)

Same information as OLED but with color coding:
- Green circle and "BLE OK" = connected
- Red circle and "BLE --" = disconnected
- Yellow = best channel / recommended
- Cyan = pairing PIN digits

### Display Toggle

Double-tap BOOT button to turn display on or off. The state is saved to non-volatile memory and survives power cycles. When the display is off, LEDs are also disabled to reduce power consumption.

## Factory Reset (Retranslator Only)

Hold the BOOT button for 5 seconds to perform a factory reset:

1. All NVS data is cleared (display preferences, initialization flag)
2. The filesystem is formatted (Reticulum identity and all learned paths are erased)
3. The LED turns red during the reset process
4. The device reboots automatically
5. On the next boot, a new transport identity is generated

This operation is irreversible. The retranslator will appear as a new node on the mesh.

### First Boot

When a retranslator is flashed for the first time (or after factory reset):

1. The device detects it has not been initialized
2. The filesystem is formatted for clean storage
3. A new Reticulum transport identity is generated and saved
4. The device reboots into normal operation

If power is lost during first boot, the device will boot normally on the next attempt with an empty filesystem. Reticulum will generate a new identity automatically.

## Build Environments

### Client (BLE Bridge)

| Environment | Board | Display | Notes |
|------------|-------|---------|-------|
| client_c3 | ESP32-C3 | SSD1306 OLED | Default target |
| client_tdisplay | T-Display | ST7789 TFT | Color display |
| client_s3 | ESP32-S3 | None | RGB NeoPixel LED, dual serial |
| client_esp32 | ESP32 | None | Classic ESP32 |
| client_c6 | ESP32-C6 | None | WiFi 6 capable |

### Retranslator (Transport Node)

| Environment | Board | Display | Paths | Notes |
|------------|-------|---------|-------|-------|
| retranslator_c3 | ESP32-C3 | SSD1306 | 48 | Small networks |
| retranslator_c3_debug | ESP32-C3 | SSD1306 | 48 | Verbose logging |
| retranslator_tdisplay | T-Display | ST7789 | 48 | Color display |
| retranslator_s3 | ESP32-S3 | None | 128 | No PSRAM variant |
| retranslator_s3_r2 | ESP32-S3 | None | 1024 | 2MB quad PSRAM |
| retranslator_s3_r8 | ESP32-S3 | None | 4096 | 8MB octal PSRAM |
| retranslator_esp32 | ESP32 | None | 128 | Classic ESP32 |
| retranslator_c6 | ESP32-C6 | None | 48 | WiFi 6 |
| retranslator_s2 | ESP32-S2 | None | 48 | No BLE, relay only |

### UART Client (Serial Bridge)

| Environment | Board | Display | Notes |
|------------|-------|---------|-------|
| uart_c3 | ESP32-C3 | SSD1306 | With OLED status |
| uart_tdisplay | T-Display | ST7789 | With TFT status |
| uart_s3 | ESP32-S3 | None | Dual serial output |
| uart_esp32 | ESP32 | None | Classic ESP32 |
| uart_c6 | ESP32-C6 | None | WiFi 6 |
| uart_s2 | ESP32-S2 | None | No BLE available |

## Known Limitations

### ESP32-C3 Retranslator Memory

The ESP32-C3 has 320KB SRAM with no PSRAM. Under heavy announce traffic (testnet interconnect, large networks), the microReticulum transport stack experiences heap pressure due to upstream memory management issues. The C3 retranslator is recommended for small networks (up to 20 nodes). For larger deployments or testnet connectivity, use the ESP32-S3 with PSRAM.

### microReticulum Upstream Bugs

The following are known issues in the microReticulum library (not in this firmware):

- `_held_announces` data structure grows without bounds under heavy traffic
- `_announce_table` uses `insert()` without `erase()`, allowing stale entries
- Path table migration from heap to flash storage is incomplete

These issues are documented in the RTNode-HeltecV4 project. Patches exist but require forking microReticulum.

### Periodic Re-announce

Transport node periodic re-announce (for multi-hop chain discovery) is currently disabled due to stability issues with `Destination::announce()` on constrained hardware. The initial boot announce works correctly. For single-hop ESP-NOW broadcast meshes, this has no practical impact since all nodes hear each other directly.

## Troubleshooting

### BLE device not visible on phone

- Ensure the device is powered and the LED is blinking (heartbeat)
- On Android 15+, enable "Show unsupported BLE devices" in Developer Options
- Try holding BOOT for 2 seconds to re-enter pairing mode
- Ensure no other phone is currently connected (one connection at a time)

### Sideband shows "no unpaired RNodes"

- The device BLE name must start with "RNode " (with space). This is set automatically from the MAC address
- Unpair the device from Android Bluetooth settings, then scan again in Sideband
- Ensure "Device requires BLE" is checked in Sideband RNode hardware settings

### Serial output garbled

- Check baud rate: 115200 for monitor, 921600 for UART client data
- On ESP32-S3 boards, both USB ports output serial data. Connect to either one
- Ensure terminal uses CR+LF line endings

### Display stays black

- Double-tap BOOT button -- the display may be toggled off (state is persisted)
- For first flash, the display state defaults to ON

### Factory reset not working

- Hold BOOT for a full 5 seconds. The LED turns red when the reset triggers
- If the board is unresponsive, hold BOOT while pressing RESET, then release BOOT to enter download mode and reflash

### ESP-NOW not connecting between nodes

- All nodes must be on the same channel (ESPNOW_CHANNEL in board_config.h)
- All nodes must use the same LR mode setting (ESPNOW_LONG_RANGE)
- Check that nodes are within range (~200m standard, ~500m with LR mode)

## License

This project uses the following open-source components:

- [microReticulum](https://github.com/attermann/microReticulum) -- Apache 2.0
- [Reticulum](https://github.com/markqvist/reticulum) -- MIT
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) -- Apache 2.0
- [U8g2](https://github.com/olikraus/U8g2) -- BSD
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) -- Mixed
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) -- MIT (channel scanner)
