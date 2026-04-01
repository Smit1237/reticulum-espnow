#include "board_config.h"
#include "DisplayTypes.h"

#if DISPLAY_TYPE == DISPLAY_ST7789

#include "ST7789Display.h"
#include <SPI.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft = TFT_eSPI();
static bool _displayOn = true;

// Color theme
#define BG_COLOR      TFT_BLACK
#define TEXT_COLOR    TFT_WHITE
#define LABEL_COLOR   TFT_LIGHTGREY
#define CONN_COLOR    TFT_GREEN
#define DISCONN_COLOR TFT_RED
#define PIN_COLOR     TFT_CYAN
#define PAIR_COLOR    TFT_YELLOW

namespace Display {

void init() {
	tft.init();
	tft.setRotation(1);  // Landscape: 240x135
	tft.fillScreen(BG_COLOR);

#ifdef TFT_BL_PIN
	if (TFT_BL_PIN >= 0) {
		pinMode(TFT_BL_PIN, OUTPUT);
		digitalWrite(TFT_BL_PIN, HIGH);
	}
#endif
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
}

void showStatus(bool connected, const char* name,
                unsigned long tx, unsigned long rx, unsigned long freeHeap) {
	if (!_displayOn) return;

	tft.fillScreen(BG_COLOR);

	// Row 1: connection indicator + name
	if (connected)
		tft.fillCircle(12, 16, 8, CONN_COLOR);
	else
		tft.drawCircle(12, 16, 8, DISCONN_COLOR);

	tft.setTextFont(4);
	tft.setTextColor(TEXT_COLOR, BG_COLOR);
	tft.setCursor(28, 4);
	tft.print(name);

	// Stats area
	tft.setTextFont(2);

	tft.setTextColor(LABEL_COLOR, BG_COLOR);
	tft.setCursor(4, 42);
	tft.print("TX: ");
	tft.setTextColor(TEXT_COLOR, BG_COLOR);
	tft.print(tx);

	tft.setTextColor(LABEL_COLOR, BG_COLOR);
	tft.setCursor(4, 64);
	tft.print("RX: ");
	tft.setTextColor(TEXT_COLOR, BG_COLOR);
	tft.print(rx);

	tft.setTextColor(LABEL_COLOR, BG_COLOR);
	tft.setCursor(4, 86);
	tft.print("heap: ");
	tft.setTextColor(TEXT_COLOR, BG_COLOR);
	tft.print(freeHeap);

	// BLE status on right side
	tft.setTextColor(connected ? CONN_COLOR : DISCONN_COLOR, BG_COLOR);
	tft.setCursor(140, 42);
	tft.print(connected ? "BLE OK" : "BLE --");
}

void showPairingMode(const char* name) {
	if (!_displayOn) return;

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
#ifdef TFT_BL_PIN
	if (TFT_BL_PIN >= 0) {
		digitalWrite(TFT_BL_PIN, on ? LOW : HIGH);
	}
#endif
}

bool isOn() {
	return _displayOn;
}

} // namespace Display

#endif // DISPLAY_TYPE == DISPLAY_ST7789
