#pragma once

// ---- Board: ESP32-C3 with 0.42" SSD1306 OLED (72x40) ----

// OLED Display (SSD1306 72x40 I2C)
#define OLED_SDA        5
#define OLED_SCL        6
#define OLED_RST        -1    // No reset pin
#define OLED_ADDR       0x3C
#define OLED_WIDTH      72
#define OLED_HEIGHT     40
// SSD1306 internal buffer is 128x64; the 72x40 panel sits at offset (28,0)
#define OLED_OFFSET_X   28
#define OLED_OFFSET_Y   0

// LEDs
#define LED_PWR_PIN     4
#define LED_USER_PIN    8

// Buttons
#define BUTTON_BOOT_PIN 9     // BOOT button (active LOW)

// ESP-NOW
#define ESPNOW_CHANNEL  1     // WiFi channel for ESP-NOW

// WiFi TX power (units of 0.25 dBm, applied to ESP-NOW)
// 80 = 20 dBm (max, ~200m LOS, ~150mA)
// 68 = 17 dBm (~150m, ~140mA)
// 52 = 13 dBm (~100m, ~130mA)
// 34 = 8.5 dBm (~50m, ~125mA)
// -4 = -1 dBm (~10m, ~120mA)
#define ESPNOW_TX_POWER 80    // 20 dBm max

// Board identity
#define BOARD_NAME      "ESP32-C3 ESP-NOW Node"
