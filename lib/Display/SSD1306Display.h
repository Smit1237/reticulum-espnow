#pragma once

#include <stdint.h>

namespace Display {
	void init();
	void showBootScreen(const char* name, const char* subtitle);
	void showStatus(bool connected, const char* name,
	                unsigned long tx, unsigned long rx, float tempC,
	                float cpu0 = -1, float cpu1 = -1);
	void showPairingMode(const char* name);
	void showPin(uint32_t pin);
	void clear();
	void setPowerSave(bool on);
	bool isOn();
}
