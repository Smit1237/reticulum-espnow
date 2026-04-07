# Changelog

## v1.1.3 -- Merged Binaries, Web Flasher Overhaul

### Merged Firmware Binaries
- **All firmware binaries now include bootloader + partition table + boot selector + app** in a single file. Flash at offset 0x0 — works on completely erased/new chips.
- Previous app-only binaries (offset 0x10000) required an existing bootloader and would fail on clean flash.
- Merge happens automatically during build via `extra_script.py` post-build action.
- Per-chip bootloader offsets handled: ESP32/S2 at 0x1000, C3/S3/C6 at 0x0000.
- `boot_app0.bin` (OTA boot selector at 0xe000) included — was the missing piece causing boot failures.
- Tested on clean-erased: ESP32-C3, ESP32-S3, ESP32 classic (WROOM/WROVER) — all boot correctly.

### Web Flasher
- **Flash offset changed from 0x10000 to 0x0** for merged binaries.
- **Full flash erase checkbox** — optional erase before flashing for clean installs.
- **Serial Monitor (Step 4)** — view device output after flashing (pairing PIN, boot messages) directly in the browser. Start/Stop with separate port connection at 115200 baud.
- **Unlocked UI on unknown chips** — if chip auto-detection fails or returns an unsupported chip, all firmware types and boards are shown instead of blocking the UI. Chip detection still pre-filters when successful.
- **CI pipeline fix** — `.app.bin` files excluded from release artifacts, only merged binaries deployed.

### Documentation
- Fixed Russian translation in HTML manual: ESP32-S3 solder bridge is for RGB LED, not PSRAM.

### Standalone Flasher
- Desktop `esp32-flasher.html` updated with offset 0x0, erase checkbox, and serial monitor.

---

## v1.1.2 -- Periodic Re-Announce & Transport Improvements

### Transport Re-Announce
- **Periodic probe destination re-announce** for transport nodes. Retrieves the registered probe destination by hash and calls `announce()` — safe reuse of existing object, no duplicate registration crash.
- Default interval: 30 minutes (configurable via `-DREANNOUNCE_INTERVAL_MS` build flag)
- Heap guard: auto-reboots when free heap drops below 40KB (identity and paths persist on flash, node recovers in ~2 seconds)

### Self-Echo Filter
- **ESPNOWInterface TX echo filter** prevents the node from processing its own broadcast packets. 8-slot ring buffer of 16-byte prefixes matched against incoming data. Reduces per-announce heap fragmentation from ~5KB to ~670 bytes.

### Retranslator Display
- Shows packet counts (TX/RX) instead of byte counts, consistent with BLE and UART clients
- Added `rxPackets()`/`txPackets()` static counters to ESPNOWInterface

### Configuration
- `REANNOUNCE_INTERVAL_MS` — configurable per-environment via build flag (default 30 minutes)
  Example: `-DREANNOUNCE_INTERVAL_MS=7200000` for 2-hour interval

---

## v1.1.1 -- BLE Throughput & Stability

### BLE Throughput
- **MTU-aware notification fragmentation** -- payloads exceeding (MTU-3) bytes are split into chunks with retry on NimBLE TX buffer exhaustion. Previously, large ESP-NOW packets (up to 1470 bytes) were silently truncated to 509 bytes.
- **Data Length Extension (DLE)** -- requests 251-byte link-layer PDUs (vs 27 default), reducing LL packets per notification by ~10x
- **2M PHY negotiation** on BLE 5.0 chips (C3, S3, C6) -- doubles air-time speed
- **Fast connection parameters** -- requests 15-30ms interval after auth (vs phone default 30-50ms)
- **MTU 517** (up from 512) for proper 512-byte ATT payload support
- **BLE RX buffer** increased to 6144 bytes (from 2048) matching RNode firmware
- **Notify pacing** -- `vTaskDelay(1)` after each `ble_send()` prevents back-to-back notification overflow during handshake

### Throughput Optimization
- **Removed `delay(2)`** after `esp_now_send()` in BLE and UART clients -- send is async, delay was unnecessary (saves 2ms per TX packet)
- **Removed `delay(5)`** from radio config ACK responses in both clients (saves 30ms during handshake)
- **BLE client HOUSEKEEPING_MS** reduced from 50ms to 10ms -- worst-case outbound latency drops from 55ms to 15ms

### BLE Connection Stability
- Connection parameter and PHY updates **deferred to `onAuthenticationComplete`** instead of `onConnect` -- prevents disrupting pairing handshake
- Connection params relaxed to 15-30ms (7.5ms caused supervision timeouts on ESP32 classic with WiFi+BLE coexistence)

### T-Display Fixes
- **Backlight GPIO 4 / ADC2 fix** -- WiFi PHY blob switches GPIO 4 (ADC2_CH0) from digital to RTC IO mux, killing backlight. Fixed with `rtc_gpio_deinit()` to force pad back to digital GPIO mode
- **`ensureBacklight()` watchdog** on all three firmware types for T-Display target -- detects and restores hijacked backlight pin every 2 seconds
- **Boot screen leftover fix** -- `_needsClear` one-shot flag clears screen on first status update after boot/pairing mode

### UART Client
- **Added LED helpers** with active-low support -- was completely missing LED control (inverted heartbeat, no power-off on display toggle)
- **LED power management** -- LEDs turn off when display is toggled off, matching BLE client and retranslator behavior

### Project
- **MIT License** added
- **Root README.md** rewritten with architecture diagram, hardware table, full build environment list, web flasher link, dependency licenses
- **Documentation** updated for all v1.1.1 changes

---

## v1.1.0 -- Event-Driven Loops & CI Automation

### Performance
- **Event-driven main loops** for all three firmware types:
  - Client: blocks on ESP-NOW RX queue with 50ms timeout (xQueueReceive)
  - UART client: same pattern with 10ms timeout for serial throughput
  - Retranslator: binary semaphore on ESPNOWInterface, signaled from ISR
- CPU sleeps while idle, wakes instantly on mesh data arrival
- Removed all `delay(1)` polling from loops

### Build Quality
- **Zero compiler warnings** across all targets (C3, S3, ESP32, T-Display)
- Fixed `DISPLAY_TYPE` redefinition warning (DisplayTypes.h default removed)
- Renamed KISS protocol constants to avoid build flag conflicts (`KISS_MCU_ESP32`, `KISS_PLATFORM_ESP32`)
- Removed deprecated `NimBLEService::start()` call (NimBLE 2.x)
- Fixed volatile increment deprecation (C++20) in ESPNOWInterface
- C++-only warning flags moved to CXXFLAGS via extra_script.py

### Display Fixes
- Fixed ST7789 boot screen leftover text (`_needsClear` one-shot clear flag)

### CI/CD
- **Automated gh-pages deploy**: new `deploy` job in release workflow
  - Copies firmware binaries to `firmware/<version>/` on gh-pages
  - Generates `firmware/versions.json` manifest automatically
  - Updates flasher HTML from source branch
- **Web flasher version selector**: users can pick any released firmware version
  - Cascading dropdowns: Version -> Type -> Board
  - Falls back to GitHub API if versions.json not yet deployed

### Infrastructure
- ESPNOWInterface: added `_rx_notify` semaphore with `waitForData()` for external consumers

---

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
