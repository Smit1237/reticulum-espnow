#pragma once

// ============================================================
// Board configuration — selected via -DBOARD_XXX build flag
// ============================================================

// ---- ESP32-C3 with 0.42" SSD1306 OLED (72x40) ----
#if defined(BOARD_ESP32C3)

#define HAS_OLED        1
#define OLED_SDA        5
#define OLED_SCL        6
#define OLED_RST        -1
#define OLED_ADDR       0x3C
#define OLED_WIDTH      72
#define OLED_HEIGHT     40
#define OLED_OFFSET_X   28
#define OLED_OFFSET_Y   0
#define LED_PWR_PIN     4
#define LED_USER_PIN    8
#define BUTTON_BOOT_PIN 9
#define HAS_PSRAM       0
#define BOARD_NAME      "ESP32-C3 Node"

// ---- ESP32-S3 generic DevKit ----
#elif defined(BOARD_ESP32S3)

#define HAS_OLED        0       // No OLED by default, override per-board
#define OLED_SDA        1
#define OLED_SCL        2
#define OLED_RST        -1
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_OFFSET_X   0
#define OLED_OFFSET_Y   0
#define LED_PWR_PIN     -1
#define LED_USER_PIN    48      // RGB LED on many S3 devkits (neopixel)
#define BUTTON_BOOT_PIN 0
#define HAS_PSRAM       1
#define BOARD_NAME      "ESP32-S3 Node"

// ---- ESP32 classic generic DevKit ----
#elif defined(BOARD_ESP32)

#define HAS_OLED        0
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_RST        -1
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_OFFSET_X   0
#define OLED_OFFSET_Y   0
#define LED_PWR_PIN     -1
#define LED_USER_PIN    2       // Built-in LED on most ESP32 devkits
#define BUTTON_BOOT_PIN 0
#define HAS_PSRAM       0
#define BOARD_NAME      "ESP32 Node"

// ---- ESP32-C6 generic DevKit ----
#elif defined(BOARD_ESP32C6)

#define HAS_OLED        0
#define OLED_SDA        6
#define OLED_SCL        7
#define OLED_RST        -1
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_OFFSET_X   0
#define OLED_OFFSET_Y   0
#define LED_PWR_PIN     -1
#define LED_USER_PIN    8       // Built-in LED on C6 devkits
#define BUTTON_BOOT_PIN 9
#define HAS_PSRAM       0
#define BOARD_NAME      "ESP32-C6 Node"

// ---- ESP32-S2 generic DevKit (retranslator only, no BLE) ----
#elif defined(BOARD_ESP32S2)

#define HAS_OLED        0
#define OLED_SDA        8
#define OLED_SCL        9
#define OLED_RST        -1
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_OFFSET_X   0
#define OLED_OFFSET_Y   0
#define LED_PWR_PIN     -1
#define LED_USER_PIN    2
#define BUTTON_BOOT_PIN 0
#define HAS_PSRAM       0
#define BOARD_NAME      "ESP32-S2 Node"

// ---- Fallback: unknown board ----
#else
#warning "No BOARD_XXX defined, using generic defaults"

#define HAS_OLED        0
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_RST        -1
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_OFFSET_X   0
#define OLED_OFFSET_Y   0
#define LED_PWR_PIN     -1
#define LED_USER_PIN    2
#define BUTTON_BOOT_PIN 0
#define HAS_PSRAM       0
#define BOARD_NAME      "ESP32 Node"

#endif

// ---- Shared config (all boards) ----

// ESP-NOW
#define ESPNOW_CHANNEL  1       // WiFi channel for ESP-NOW
