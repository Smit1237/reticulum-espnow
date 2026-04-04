#pragma once

#include <stdint.h>

namespace Display {
	void init();
	void showBootScreen(const char* name, const char* subtitle);
	void showStatus(bool connected, const char* name,
	                unsigned long tx, unsigned long rx, float tempC);
	void showPairingMode(const char* name);
	void showPin(uint32_t pin);
	void clear();
	void setPowerSave(bool on);
	void ensureBacklight();
	bool isOn();
}
