#include <Arduino.h>
#include "board_config.h"
#include "Display.h"

#include <Preferences.h>

#include <microStore/FileSystem.h>
#include <microStore/Adapters/LittleFSFileSystem.h>

#include <ESPNOWInterface.h>

#include <Reticulum.h>
#include <Transport.h>
#include <Interface.h>
#include <Log.h>
#include <Bytes.h>
#include <Type.h>
#include <Utilities/OS.h>

static Preferences prefs;

// NVS keys
#define NVS_NAMESPACE   "rnode"
#define NVS_INITIALIZED "init"
#define NVS_DISPLAY     "disp"

// LED helpers
inline void led_flash() {
#ifdef LED_RGB_PIN
	neopixelWrite(LED_RGB_PIN, 4, 4, 4);
#elif LED_USER_PIN >= 0
	digitalWrite(LED_USER_PIN, HIGH);
#endif
}
inline void led_off() {
#ifdef LED_RGB_PIN
	neopixelWrite(LED_RGB_PIN, 0, 0, 0);
#elif LED_USER_PIN >= 0
	digitalWrite(LED_USER_PIN, LOW);
#endif
}
inline void led_reset() {
#ifdef LED_RGB_PIN
	neopixelWrite(LED_RGB_PIN, 8, 0, 0);  // red = factory reset
#elif LED_USER_PIN >= 0
	digitalWrite(LED_USER_PIN, HIGH);
#endif
}

RNS::Reticulum reticulum({RNS::Type::NONE});
RNS::Interface espnow_interface({RNS::Type::NONE});


// Track RX/TX for LED activity
static size_t last_rxb = 0;
static size_t last_txb = 0;

void updateDisplay() {
	if (!Display::isOn()) return;
	Display::showStatus(true, "TRANSPORT",
	                    (unsigned long)espnow_interface.rxb(),
	                    (unsigned long)espnow_interface.txb(),
	                    (unsigned long)ESP.getFreeHeap());
}

// =============== First boot / Factory reset ===============

bool check_first_boot() {
	prefs.begin(NVS_NAMESPACE, true);  // read-only
	bool initialized = prefs.getBool(NVS_INITIALIZED, false);
	prefs.end();
	return !initialized;
}

void mark_initialized() {
	prefs.begin(NVS_NAMESPACE, false);
	prefs.putBool(NVS_INITIALIZED, true);
	prefs.end();
}

void factory_reset() {
	Serial.println("\r\n*** FACTORY RESET ***\r\n");
	Display::showBootScreen("FACTORY", "RESET...");

	led_reset();

	// Clear all NVS data
	prefs.begin(NVS_NAMESPACE, false);
	prefs.clear();
	prefs.end();

	// Format filesystem — wipes identity, paths, everything
	microStore::FileSystem filesystem{microStore::Adapters::LittleFSFileSystem()};
	filesystem.init();
	filesystem.format();

	Serial.println("NVS cleared, filesystem formatted.\r\n");
	Serial.println("Rebooting...\r\n");
	delay(1000);
	ESP.restart();
}

void first_boot_init() {
	Serial.println("\r\n=== FIRST BOOT — Initializing ===\r\n");
	Display::showBootScreen("FIRST BOOT", "initializing...");

	// Format filesystem for clean slate
	Serial.println("Formatting filesystem...\r\n");
	microStore::FileSystem filesystem{microStore::Adapters::LittleFSFileSystem()};
	filesystem.init();
	filesystem.format();

	// Re-init after format
	if (!filesystem.init()) {
		Serial.println("ERROR: filesystem init failed after format!\r\n");
		return;
	}
	RNS::Utilities::OS::register_filesystem(filesystem);

	// Boot Reticulum briefly to generate and persist transport identity
	Serial.println("Generating transport identity...\r\n");
	RNS::Reticulum tempReticulum;
	tempReticulum.transport_enabled(true);
	tempReticulum.probe_destination_enabled(true);
	tempReticulum.start();

	// Persist transport data (identity saved to filesystem)
	RNS::Transport::persist_data();

	Serial.println("Transport identity created and saved.\r\n");

	// Mark as initialized
	mark_initialized();

	// Set default display state
	prefs.begin(NVS_NAMESPACE, false);
	prefs.putBool(NVS_DISPLAY, true);
	prefs.end();

	Serial.println("First boot complete. Rebooting into normal mode...\r\n");
	Display::showBootScreen("FIRST BOOT", "done, rebooting");
	delay(1000);
	ESP.restart();
}

// =============== Reticulum setup (normal boot) ===============

void reticulum_setup() {
	INFO("Setting up Transport node...");

	try {
		HEAD("Registering FileSystem...", RNS::LOG_TRACE);
		microStore::FileSystem filesystem{microStore::Adapters::LittleFSFileSystem()};
		if (!filesystem.init()) {
			INFO("Filesystem mount failed, formatting...");
			filesystem.format();
			filesystem.init();
		}
		RNS::Utilities::OS::register_filesystem(filesystem);

		HEAD("Registering ESPNOWInterface...", RNS::LOG_TRACE);
		espnow_interface = new ESPNOWInterface("ESPNOWInterface", ESPNOW_CHANNEL);
		espnow_interface.mode(RNS::Type::Interface::MODE_GATEWAY);
		RNS::Transport::register_interface(espnow_interface);
		espnow_interface.start();

		// Tune table sizes based on available memory at runtime
		uint32_t freePsram = ESP.getFreePsram();
		uint32_t freeHeap = ESP.getFreeHeap();
		INFOF("Memory: heap=%lu PSRAM=%lu", (unsigned long)freeHeap, (unsigned long)freePsram);

		if (freePsram > 4000000) {
			RNS::Transport::path_table_maxsize(4096);
			RNS::Transport::announce_table_maxsize(1500);
			RNS::Transport::hashlist_maxsize(15000);
		} else if (freePsram > 1000000) {
			RNS::Transport::path_table_maxsize(1024);
			RNS::Transport::announce_table_maxsize(512);
			RNS::Transport::hashlist_maxsize(5000);
		} else if (freeHeap > 200000) {
			RNS::Transport::path_table_maxsize(128);
			RNS::Transport::announce_table_maxsize(48);
			RNS::Transport::hashlist_maxsize(256);
		} else {
			RNS::Transport::path_table_maxsize(48);
			RNS::Transport::announce_table_maxsize(24);
			RNS::Transport::hashlist_maxsize(128);
		}
		INFOF("Table limits: paths=%u announces=%u hashlist=%u",
			RNS::Transport::path_table_maxsize(),
			RNS::Transport::announce_table_maxsize(),
			RNS::Transport::hashlist_maxsize());

		HEAD("Creating Reticulum transport instance...", RNS::LOG_TRACE);
		reticulum = RNS::Reticulum();
		reticulum.transport_enabled(true);
		reticulum.probe_destination_enabled(true);
		reticulum.start();

		INFOF("Transport ready! Free heap: %lu PSRAM free: %lu",
			(unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram());
	}
	catch (const std::exception& e) {
		ERRORF("Exception in reticulum_setup: %s", e.what());
	}
}

// =============== Setup ===============

void setup() {
	Serial.begin(115200);
	delay(500);
	Serial.println("\r\n=== TRANSPORT NODE ===");

#if LED_USER_PIN >= 0
	pinMode(LED_USER_PIN, OUTPUT);
#endif
	led_flash();
	pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP);

	// Display init
	Display::init();

	// Check for factory reset: if BOOT held during power-on, wait 5s
	if (digitalRead(BUTTON_BOOT_PIN) == LOW) {
		Display::showBootScreen("HOLD 5s", "for RESET");
		Serial.println("BOOT button held — hold 5 seconds for factory reset\r\n");
		unsigned long start = millis();
		while (digitalRead(BUTTON_BOOT_PIN) == LOW) {
			if (millis() - start > 5000) {
				factory_reset();  // Never returns — reboots
			}
			delay(50);
		}
		Serial.println("Released early — normal boot\r\n");
	}

	// Check first boot
	if (check_first_boot()) {
		first_boot_init();  // Never returns — reboots after init
	}

	// Normal boot — load display state
	prefs.begin(NVS_NAMESPACE, true);
	bool displayOn = prefs.getBool(NVS_DISPLAY, true);
	prefs.end();
	if (!displayOn) Display::setPowerSave(true);
	Display::showBootScreen("TRANSPORT", "booting...");

#ifdef DEBUG_BUILD
	RNS::loglevel(RNS::LOG_TRACE);
#else
	RNS::loglevel(RNS::LOG_WARNING);
#endif
	reticulum_setup();

	led_off();
	updateDisplay();
}

// =============== Loop ===============

void loop() {
	reticulum.loop();

	// NOTE: Periodic transport re-announce not implemented on C3 due to
	// microReticulum abort() during announce rebroadcast on constrained hardware.
	// Single-hop ESP-NOW broadcast doesn't need it — all nodes hear initial announce.
	// For multi-hop chains, use S3 retranslator which has more resources.

	// BOOT button: double-tap = toggle display, hold 5s = factory reset
	static unsigned long lastTapAt = 0;
	static bool btnDown = false;
	static unsigned long btnDownAt = 0;

	bool pressed = (digitalRead(BUTTON_BOOT_PIN) == LOW);

	if (pressed && !btnDown) {
		btnDown = true;
		btnDownAt = millis();
	}

	if (pressed && btnDown && millis() - btnDownAt > 5000) {
		factory_reset();  // Never returns
	}

	if (!pressed && btnDown) {
		btnDown = false;
		unsigned long dur = millis() - btnDownAt;

		if (dur < 500) {
			if (millis() - lastTapAt < 400) {
				bool on = !Display::isOn();
				Serial.printf("Display: %s\r\n", on ? "ON" : "OFF");
				Display::setPowerSave(!on);

				prefs.begin(NVS_NAMESPACE, false);
				prefs.putBool(NVS_DISPLAY, on);
				prefs.end();

				if (on) updateDisplay();
				lastTapAt = 0;
			} else {
				lastTapAt = millis();
			}
		}
	}

	// Update display every 2 seconds
	static unsigned long last_display = 0;
	if (millis() - last_display > 2000) {
		updateDisplay();
		last_display = millis();
	}

	// LED: flash on packet activity
	size_t cur_rxb = espnow_interface.rxb();
	size_t cur_txb = espnow_interface.txb();
	if (cur_rxb != last_rxb || cur_txb != last_txb) {
		led_flash();
		last_rxb = cur_rxb;
		last_txb = cur_txb;
	} else {
		led_off();
	}
}

int _write(int file, char *ptr, int len) {
	return Serial.write(ptr, len);
}
