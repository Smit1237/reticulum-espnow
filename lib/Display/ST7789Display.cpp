#include "board_config.h"
#include "DisplayTypes.h"

#if DISPLAY_TYPE == DISPLAY_ST7789

#include "ST7789Display.h"
#include <SPI.h>
#include <TFT_eSPI.h>

// GPIO 4 (TFT backlight on T-Display) is ADC2_CH0 on ESP32 classic.
// The WiFi PHY blob uses SAR ADC2 internally for RF power detection,
// which switches GPIO 4 from digital to RTC IO mux — killing the
// backlight output. rtc_gpio_deinit() forces it back to digital mode.
#if defined(CONFIG_IDF_TARGET_ESP32) && defined(TFT_BL_PIN)
  #include <driver/rtc_io.h>
  #define BL_IS_ADC2_PIN 1
#endif

static TFT_eSPI tft = TFT_eSPI();
static bool _displayOn = true;
static bool _needsClear = true;  // Clear screen on first status draw after boot/mode change

// Color theme
#define BG_COLOR      TFT_BLACK
#define TEXT_COLOR    TFT_WHITE
#define LABEL_COLOR   TFT_LIGHTGREY
#define CONN_COLOR    TFT_GREEN
#define DISCONN_COLOR TFT_RED
#define PIN_COLOR     TFT_CYAN
#define PAIR_COLOR    TFT_YELLOW

// Force backlight pin back to digital GPIO mode.
// On ESP32 classic, WiFi PHY can switch ADC2 pins (including GPIO 4)
// to RTC IO mux, disconnecting the digital output driver.
static void backlightOn() {
#ifdef TFT_BL_PIN
	if (TFT_BL_PIN < 0) return;
  #ifdef BL_IS_ADC2_PIN
	rtc_gpio_deinit((gpio_num_t)TFT_BL_PIN);  // RTC mux → digital GPIO mux
  #endif
	pinMode(TFT_BL_PIN, OUTPUT);
	digitalWrite(TFT_BL_PIN, HIGH);
#endif
}

static void backlightOff() {
#ifdef TFT_BL_PIN
	if (TFT_BL_PIN < 0) return;
  #ifdef BL_IS_ADC2_PIN
	rtc_gpio_deinit((gpio_num_t)TFT_BL_PIN);
  #endif
	pinMode(TFT_BL_PIN, OUTPUT);
	digitalWrite(TFT_BL_PIN, LOW);
#endif
}

namespace Display {

void init() {
	tft.init();
	tft.setRotation(1);  // Landscape: 240x135
	tft.fillScreen(BG_COLOR);
	backlightOn();
}

void showBootScreen(const char* name, const char* subtitle) {
	tft.fillScreen(BG_COLOR);
	tft.setTextFont(4);
	tft.setTextColor(TEXT_COLOR, BG_COLOR);
	tft.setCursor(40, 30);
	tft.print(name);
	tft.setTextFont(2);
	tft.setTextColor(LABEL_COLOR, BG_COLOR);
	tft.setCursor(60, 80);
	tft.print(subtitle);
	_needsClear = true;  // Next showStatus() must clear boot leftovers
}

void showStatus(bool connected, const char* name,
                unsigned long tx, unsigned long rx, float tempC) {
	if (!_displayOn) return;

	// One-shot clear after boot/pairing screen to wipe leftover content
	if (_needsClear) {
		tft.fillScreen(BG_COLOR);
		_needsClear = false;
	}

	// Use text background color to overwrite old text (no full screen clear = no flicker)
	// Row 1: connection indicator + name
	if (connected)
		tft.fillCircle(12, 16, 8, CONN_COLOR);
	else {
		tft.fillCircle(12, 16, 8, BG_COLOR);
		tft.drawCircle(12, 16, 8, DISCONN_COLOR);
	}

	tft.setTextFont(4);
	tft.setTextColor(TEXT_COLOR, BG_COLOR);
	tft.setCursor(28, 4);
	tft.print(name);
	tft.print("        "); // clear trailing chars

	// Stats area
	tft.setTextFont(2);
	char buf[16];

	tft.setTextColor(LABEL_COLOR, BG_COLOR);
	tft.setCursor(4, 42);
	tft.print("TX: ");
	tft.setTextColor(TEXT_COLOR, BG_COLOR);
	snprintf(buf, sizeof(buf), "%-10lu", tx);
	tft.print(buf);

	tft.setTextColor(LABEL_COLOR, BG_COLOR);
	tft.setCursor(4, 64);
	tft.print("RX: ");
	tft.setTextColor(TEXT_COLOR, BG_COLOR);
	snprintf(buf, sizeof(buf), "%-10lu", rx);
	tft.print(buf);

	tft.setTextColor(LABEL_COLOR, BG_COLOR);
	tft.setCursor(4, 86);
	snprintf(buf, sizeof(buf), "%.1f C    ", tempC);
	tft.print(buf);
}

void showPairingMode(const char* name) {
	if (!_displayOn) return;

	_needsClear = true;  // Status screen must clear after pairing mode

	// Alternating border color for visual pairing indication
	static bool blink = false;
	blink = !blink;
	uint16_t borderColor = blink ? PAIR_COLOR : DISCONN_COLOR;

	tft.fillScreen(BG_COLOR);

	// Draw animated border
	tft.drawRect(0, 0, 240, 135, borderColor);
	tft.drawRect(1, 1, 238, 133, borderColor);
	tft.drawRect(2, 2, 236, 131, borderColor);

	tft.setTextFont(4);
	tft.setTextColor(PAIR_COLOR, BG_COLOR);
	tft.setCursor(30, 15);
	tft.print("PAIRING");

	tft.setTextFont(2);
	tft.setTextColor(TEXT_COLOR, BG_COLOR);
	tft.setCursor(20, 60);
	tft.print(name);
	tft.setCursor(20, 85);
	tft.print("Connect from phone");
	tft.setCursor(20, 108);
	tft.print("now");
}

void showPin(uint32_t pin) {
	char pinStr[8];
	snprintf(pinStr, sizeof(pinStr), "%06lu", (unsigned long)pin);

	_needsClear = true;  // Status screen must clear after PIN display
	tft.fillScreen(BG_COLOR);
	tft.setTextFont(2);
	tft.setTextColor(LABEL_COLOR, BG_COLOR);
	tft.setCursor(70, 15);
	tft.print("PAIR PIN:");
	tft.setTextFont(4);
	tft.setTextColor(PIN_COLOR, BG_COLOR);
	tft.setCursor(30, 60);
	tft.print(pinStr);
}

void clear() {
	tft.fillScreen(BG_COLOR);
}

void setPowerSave(bool on) {
	_displayOn = !on;
	if (on) backlightOff(); else backlightOn();
}

// Re-assert backlight if WiFi PHY reconfigured GPIO 4 via ADC2/RTC mux.
// Only needed on ESP32 classic where GPIO 4 = ADC2_CH0.
void ensureBacklight() {
	if (!_displayOn) return;
#ifdef BL_IS_ADC2_PIN
	// Check if the pin was hijacked — digital read returns LOW even though we set HIGH
	if (digitalRead(TFT_BL_PIN) == LOW) {
		backlightOn();
	}
#endif
}

bool isOn() {
	return _displayOn;
}

} // namespace Display

#endif // DISPLAY_TYPE == DISPLAY_ST7789
