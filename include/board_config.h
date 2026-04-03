#pragma once

#include "DisplayTypes.h"

// ============================================================
// Board configuration — selected via -DBOARD_XXX build flag
// ============================================================

// ---- ESP32-C3 with 0.42" SSD1306 OLED (72x40) ----
#if defined(BOARD_ESP32C3)

#define DISPLAY_TYPE    DISPLAY_SSD1306
#define OLED_SDA        5
#define OLED_SCL        6
#define OLED_RST        -1
#define OLED_ADDR       0x3C
#define OLED_WIDTH      72
#define OLED_HEIGHT     40
#define LED_PWR_PIN     4
#define LED_USER_PIN    8
#define LED_ACTIVE_LOW  1       // LEDs wired active LOW on this board
#define BUTTON_BOOT_PIN 9
#define HAS_PSRAM       0
#define BOARD_NAME      "ESP32-C3 Node"

// ---- LilyGO T-Display (ESP32 + 1.14" ST7789 TFT 135x240) ----
#elif defined(BOARD_TDISPLAY)

#define DISPLAY_TYPE    DISPLAY_ST7789
#define TFT_BL_PIN      4
#define LED_PWR_PIN     -1
#define LED_USER_PIN    -1      // No user LED on T-Display
#define BUTTON_BOOT_PIN 0
#define BUTTON_USER_PIN 35      // Extra user button
#define HAS_PSRAM       0
#define BOARD_NAME      "T-Display"

// ---- ESP32-S3 generic DevKit ----
#elif defined(BOARD_ESP32S3)

#define DISPLAY_TYPE    DISPLAY_NONE
#define LED_PWR_PIN     -1
#define LED_USER_PIN    -1      // No simple GPIO LED
#ifdef RGB_BUILTIN
  #define LED_RGB_PIN   RGB_BUILTIN  // Use Arduino core's detected pin
#else
  #define LED_RGB_PIN   48      // Fallback: most S3 devkits use GPIO 48
#endif
#define BUTTON_BOOT_PIN 0
#define HAS_PSRAM       1
#define BOARD_NAME      "ESP32-S3 Node"

// ---- ESP32 classic generic DevKit ----
#elif defined(BOARD_ESP32)

#define DISPLAY_TYPE    DISPLAY_NONE
#define LED_PWR_PIN     -1
#define LED_USER_PIN    2
#define BUTTON_BOOT_PIN 0
#define HAS_PSRAM       0
#define BOARD_NAME      "ESP32 Node"

// ---- ESP32-C6 generic DevKit ----
#elif defined(BOARD_ESP32C6)

#define DISPLAY_TYPE    DISPLAY_NONE
#define LED_PWR_PIN     -1
#define LED_USER_PIN    8
#define BUTTON_BOOT_PIN 9
#define HAS_PSRAM       0
#define BOARD_NAME      "ESP32-C6 Node"

// ---- ESP32-S2 generic DevKit (retranslator only, no BLE) ----
#elif defined(BOARD_ESP32S2)

#define DISPLAY_TYPE    DISPLAY_NONE
#define LED_PWR_PIN     -1
#define LED_USER_PIN    2
#define BUTTON_BOOT_PIN 0
#define HAS_PSRAM       0
#define BOARD_NAME      "ESP32-S2 Node"

// ---- Fallback: unknown board ----
#else
#warning "No BOARD_XXX defined, using generic defaults"

#define DISPLAY_TYPE    DISPLAY_NONE
#define LED_PWR_PIN     -1
#define LED_USER_PIN    2
#define BUTTON_BOOT_PIN 0
#define HAS_PSRAM       0
#define BOARD_NAME      "ESP32 Node"

#endif

// ---- Shared config (all boards) ----

// ESP-NOW
#define ESPNOW_CHANNEL  13      // WiFi channel (1-13)

// Long Range mode — Espressif proprietary 802.11 LR PHY
// Doubles range (~500m LOS) at cost of throughput (512/256 Kbps vs 1 Mbps)
// Supported on: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
// NOT supported on: ESP32-C2
// All mesh nodes must use the same setting
#ifndef ESPNOW_LONG_RANGE
#define ESPNOW_LONG_RANGE  1    // 1 = LR enabled (default), 0 = standard mode
#endif

// Transport node re-announce interval (milliseconds)
// Default 2 hours — matches Python Reticulum behavior
// Important for multi-hop chains where distant nodes need periodic rediscovery
#ifndef TRANSPORT_ANNOUNCE_INTERVAL_MS
#define TRANSPORT_ANNOUNCE_INTERVAL_MS  (2 * 60 * 60 * 1000UL)  // 2 hours
#endif
