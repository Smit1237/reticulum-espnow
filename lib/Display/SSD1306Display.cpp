#include "board_config.h"
#include "DisplayTypes.h"

#if DISPLAY_TYPE == DISPLAY_SSD1306

#include "SSD1306Display.h"
#include <Wire.h>
#include <U8g2lib.h>

// Select U8g2 constructor based on display dimensions
#if OLED_WIDTH == 72 && OLED_HEIGHT == 40
static U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#elif OLED_WIDTH == 128 && OLED_HEIGHT == 64
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#elif OLED_WIDTH == 128 && OLED_HEIGHT == 32
static U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#else
#error "Unsupported SSD1306 OLED dimensions"
#endif

// Status icons 8x8
static const uint8_t icon_disconn[] = {
	0x3C, 0x42, 0x81, 0x81, 0x81, 0x81, 0x42, 0x3C  // empty circle
};
static const uint8_t icon_conn[] = {
	0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C  // filled circle
};
static const uint8_t icon_radio[] = {
	0x00, 0x70, 0x18, 0x64, 0x12, 0x4A, 0x2A, 0x2A  // radio waves
};

static bool _displayOn = true;

namespace Display {

void init() {
	Wire.begin(OLED_SDA, OLED_SCL);
	u8g2.setBusClock(400000);
	u8g2.begin();
}

void showBootScreen(const char* name, const char* subtitle) {
	u8g2.clearBuffer();
	u8g2.setFont(u8g2_font_5x7_tf);
	u8g2.drawStr(0, 7, name);
	u8g2.drawStr(0, 17, subtitle);
	u8g2.sendBuffer();
}

void showStatus(bool connected, const char* name,
                unsigned long tx, unsigned long rx, float tempC) {
	if (!_displayOn) return;

	char buf[20];
	u8g2.clearBuffer();

	// Row 1: icon + name
	if (connected)
		u8g2.drawXBM(0, 0, 8, 8, icon_conn);
	else
		u8g2.drawXBM(0, 0, 8, 8, icon_disconn);

	u8g2.setFont(u8g2_font_5x7_tf);
	u8g2.drawStr(10, 7, name);

	// Row 2: TX
	snprintf(buf, sizeof(buf), "TX: %lu", tx);
	u8g2.drawStr(0, 17, buf);

	// Row 3: RX
	snprintf(buf, sizeof(buf), "RX: %lu", rx);
	u8g2.drawStr(0, 27, buf);

	// Row 4: temperature
	snprintf(buf, sizeof(buf), "%.1f C", tempC);
	u8g2.drawStr(0, 37, buf);

	u8g2.sendBuffer();
}

void showPairingMode(const char* name) {
	if (!_displayOn) return;

	u8g2.clearBuffer();
	u8g2.drawXBM(0, 0, 8, 8, icon_disconn);
	u8g2.setFont(u8g2_font_5x7_tf);
	u8g2.drawStr(10, 7, name);
	u8g2.drawStr(0, 20, "PAIRING...");
	u8g2.drawStr(0, 30, "Connect from");
	u8g2.drawStr(0, 38, "phone now");
	u8g2.sendBuffer();
}

void showPin(uint32_t pin) {
	char pinStr[8];
	snprintf(pinStr, sizeof(pinStr), "%06lu", (unsigned long)pin);

	u8g2.clearBuffer();
	u8g2.setFont(u8g2_font_5x7_tf);
	u8g2.drawStr(14, 7, "PAIR PIN:");
	u8g2.setFont(u8g2_font_profont22_tn);
	u8g2.drawStr(0, 35, pinStr);
	u8g2.sendBuffer();
}

void clear() {
	u8g2.clearBuffer();
	u8g2.sendBuffer();
}

void setPowerSave(bool on) {
	_displayOn = !on;
	if (on) {
		u8g2.clearBuffer();
		u8g2.sendBuffer();
		u8g2.setPowerSave(1);
	} else {
		u8g2.setPowerSave(0);
	}
}

bool isOn() {
	return _displayOn;
}

} // namespace Display

#endif // DISPLAY_TYPE == DISPLAY_SSD1306
