#include <Arduino.h>
#include "board_config.h"
#include "Display.h"

#include <Preferences.h>

#include <microStore/FileSystem.h>
#include <microStore/Adapters/LittleFSFileSystem.h>

#include <ESPNOWInterface.h>

#include <Reticulum.h>
#include <Transport.h>
#include <Destination.h>
#include <Identity.h>
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

// LED helpers — respect active-low boards
#ifndef LED_ACTIVE_LOW
  #define LED_ACTIVE_LOW 0
#endif
#define LED_ON_STATE  (LED_ACTIVE_LOW ? LOW : HIGH)
#define LED_OFF_STATE (LED_ACTIVE_LOW ? HIGH : LOW)

inline void led_flash() {
#ifdef LED_RGB_PIN
	neopixelWrite(LED_RGB_PIN, 4, 4, 4);
#elif LED_USER_PIN >= 0
	digitalWrite(LED_USER_PIN, LED_ON_STATE);
#endif
}
inline void led_off() {
#ifdef LED_RGB_PIN
	neopixelWrite(LED_RGB_PIN, 0, 0, 0);
#elif LED_USER_PIN >= 0
	digitalWrite(LED_USER_PIN, LED_OFF_STATE);
#endif
}
inline void led_pwr_off() {
#if LED_PWR_PIN >= 0
	digitalWrite(LED_PWR_PIN, LED_OFF_STATE);
#endif
}
inline void led_pwr_on() {
#if LED_PWR_PIN >= 0
	digitalWrite(LED_PWR_PIN, LED_ON_STATE);
#endif
}
inline void led_reset() {
#ifdef LED_RGB_PIN
	neopixelWrite(LED_RGB_PIN, 8, 0, 0);  // red = factory reset
#elif LED_USER_PIN >= 0
	digitalWrite(LED_USER_PIN, LED_ON_STATE);
#endif
}

RNS::Reticulum reticulum({RNS::Type::NONE});
RNS::Interface espnow_interface({RNS::Type::NONE});


// Track RX/TX for LED activity
static size_t last_rxb = 0;
static size_t last_txb = 0;

static float chip_temp = 0;
static unsigned long last_temp_read = 0;

// Periodic re-announce
static RNS::Bytes probe_hash;           // Hash of probe destination (computed after start)
static uint32_t reannounce_count = 0;   // Counter for debug display
#define REANNOUNCE_INTERVAL_MS (10 * 60 * 1000UL)  // 10 minutes (debug), change to 2h for production

void updateDisplay() {
	if (!Display::isOn()) return;
	// Show reannounce count instead of temperature (debug)
	Display::showStatus(true, "TRANSPORT",
	                    (unsigned long)espnow_interface.txb(),
	                    (unsigned long)espnow_interface.rxb(),
	                    (float)reannounce_count);
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

	// Mark initialized FIRST — if power is lost after format but before
	// this flag, the device would loop formatting forever. By setting the
	// flag first, a power-loss just means we boot normally with an empty
	// filesystem, which self-heals (Reticulum generates a new identity).
	mark_initialized();

	// Set default display state
	prefs.begin(NVS_NAMESPACE, false);
	prefs.putBool(NVS_DISPLAY, true);
	prefs.end();

	// Format filesystem for clean slate
	Serial.println("Formatting filesystem...\r\n");
	microStore::FileSystem filesystem{microStore::Adapters::LittleFSFileSystem()};
	filesystem.init();
	filesystem.format();

	// Re-init after format
	if (!filesystem.init()) {
		Serial.println("ERROR: filesystem init failed after format!\r\n");
		ESP.restart();
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

		// Compute probe destination hash for periodic re-announce
		// The probe destination was created in Transport::start() and is
		// registered in Transport::_destinations. We retrieve it by hash
		// to call announce() without creating a duplicate (which would crash).
		probe_hash = RNS::Destination::hash(
			RNS::Transport::identity(), "rnstransport", "probe");
		Serial.printf("Probe destination hash: %s\r\n", probe_hash.toHex().c_str());

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
#ifndef FIRMWARE_VERSION
	#define FIRMWARE_VERSION "dev"
#endif
	Serial.printf("\r\n=== TRANSPORT NODE %s ===\r\n", FIRMWARE_VERSION);

#if LED_USER_PIN >= 0
	pinMode(LED_USER_PIN, OUTPUT);
#endif
#if LED_PWR_PIN >= 0
	pinMode(LED_PWR_PIN, OUTPUT);
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
	if (!displayOn) {
		Display::setPowerSave(true);
		led_off();
		led_pwr_off();
	}
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

// =============== Loop (event-driven) ===============
//
// Block on ESPNOWInterface notification semaphore with timeout.
// CPU sleeps while idle, wakes instantly when mesh data arrives.
// Transport::jobs() and housekeeping run at the timeout interval.

#define HOUSEKEEPING_MS 50  // 50ms — balances responsiveness with CPU savings

void loop() {
	// --- Event wait: block until ESP-NOW data or timeout ---
	ESPNOWInterface::waitForData(pdMS_TO_TICKS(HOUSEKEEPING_MS));

	// --- Run Reticulum (processes queued packets + transport jobs) ---
	reticulum.loop();

	// --- Periodic transport re-announce ---
	// Retrieves the probe destination registered by Transport::start() and
	// calls announce() on it. Safe because we reuse the existing registered
	// object (no new Destination creation = no duplicate hash = no crash).
	static unsigned long last_reannounce = 0;
	if (millis() - last_reannounce > REANNOUNCE_INTERVAL_MS) {
		last_reannounce = millis();

		if (probe_hash.size() > 0 && ESP.getFreeHeap() > 40000) {
			try {
				RNS::Destination probe = RNS::Transport::find_destination_from_hash(probe_hash);
				if (probe) {
					probe.announce();
					reannounce_count++;
					Serial.printf("Re-announce #%lu (heap: %lu)\r\n",
						(unsigned long)reannounce_count, (unsigned long)ESP.getFreeHeap());
				} else {
					Serial.println("Re-announce: probe destination not found in registry");
				}
			}
			catch (const std::exception& e) {
				Serial.printf("Re-announce FAILED: %s\r\n", e.what());
			}
		} else if (probe_hash.size() == 0) {
			Serial.println("Re-announce: probe hash not computed");
		} else {
			Serial.printf("Re-announce: skipped, low heap (%lu)\r\n",
				(unsigned long)ESP.getFreeHeap());
		}
	}

	// --- Button handling: double-tap = toggle display, hold 5s = factory reset ---
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

				if (!on) {
					led_off();
					led_pwr_off();
				} else {
					led_pwr_on();
				}

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

	// --- Periodic display update ---
	static unsigned long last_display = 0;
	if (millis() - last_display > 2000) {
		updateDisplay();
		Display::ensureBacklight();
		last_display = millis();
	}

	// --- LED: flash on packet activity (only when display is on) ---
	if (!Display::isOn()) { led_off(); }
	else {
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
}

int _write(int file, char *ptr, int len) {
	return Serial.write(ptr, len);
}
