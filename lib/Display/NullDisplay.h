#pragma once

#include <stdint.h>

namespace Display {
	inline void init() {}
	inline void showBootScreen(const char*, const char*) {}
	inline void showStatus(bool, const char*, unsigned long, unsigned long, float) {}
	inline void showPairingMode(const char*) {}
	inline void showPin(uint32_t) {}
	inline void clear() {}
	inline void setPowerSave(bool) {}
	inline bool isOn() { return false; }
}
