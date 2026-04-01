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

RNS::Reticulum reticulum({RNS::Type::NONE});
RNS::Interface espnow_interface({RNS::Type::NONE});

void updateDisplay() {
	if (!Display::isOn()) return;
	Display::showStatus(true, "TRANSPORT",
	                    (unsigned long)espnow_interface.rxb(),
	                    (unsigned long)espnow_interface.txb(),
	                    (unsigned long)ESP.getFreeHeap());
}

// --- Reticulum setup ---

void reticulum_setup() {
	INFO("Setting up Transport node...");

	try {
		HEAD("Registering FileSystem...", RNS::LOG_TRACE);
		microStore::FileSystem filesystem{microStore::Adapters::LittleFSFileSystem()};
		filesystem.init();
		RNS::Utilities::OS::register_filesystem(filesystem);

		HEAD("Registering ESPNOWInterface...", RNS::LOG_TRACE);
		espnow_interface = new ESPNOWInterface("ESPNOWInterface", ESPNOW_CHANNEL);
		espnow_interface.mode(RNS::Type::Interface::MODE_GATEWAY);
		RNS::Transport::register_interface(espnow_interface);
		espnow_interface.start();

		// Tune table sizes for ESP32-C3 (~144KB free after boot)
		RNS::Transport::path_table_maxsize(48);
		RNS::Transport::announce_table_maxsize(24);
		RNS::Transport::hashlist_maxsize(128);
		INFOF("Table limits: paths=%u announces=%u hashlist=%u",
			RNS::Transport::path_table_maxsize(),
			RNS::Transport::announce_table_maxsize(),
			RNS::Transport::hashlist_maxsize());

		HEAD("Creating Reticulum transport instance...", RNS::LOG_TRACE);
		reticulum = RNS::Reticulum();
		reticulum.transport_enabled(true);
		reticulum.probe_destination_enabled(true);
		reticulum.start();

		INFOF("Transport ready! Free heap: %lu", (unsigned long)ESP.getFreeHeap());
	}
	catch (const std::exception& e) {
		ERRORF("Exception in reticulum_setup: %s", e.what());
	}
}

// --- Main ---

void setup() {
	Serial.begin(115200);
	delay(500);
	Serial.println("\n=== TRANSPORT NODE ===");

	pinMode(LED_USER_PIN, OUTPUT);
	digitalWrite(LED_USER_PIN, HIGH);
	pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP);

	// Display init
	Display::init();
	prefs.begin("rnode", false);
	bool displayOn = prefs.getBool("disp", true);
	prefs.end();
	if (!displayOn) Display::setPowerSave(true);
	Display::showBootScreen("TRANSPORT", "booting...");

	RNS::loglevel(RNS::LOG_TRACE);
	reticulum_setup();

	digitalWrite(LED_USER_PIN, LOW);
	updateDisplay();
}

void loop() {
	reticulum.loop();

	// BOOT button: double-tap = toggle display
	static unsigned long lastTapAt = 0;
	static bool btnDown = false;
	static unsigned long btnDownAt = 0;

	bool pressed = (digitalRead(BUTTON_BOOT_PIN) == LOW);

	if (pressed && !btnDown) {
		btnDown = true;
		btnDownAt = millis();
	}

	if (!pressed && btnDown) {
		btnDown = false;
		unsigned long dur = millis() - btnDownAt;

		if (dur < 500) {
			if (millis() - lastTapAt < 400) {
				bool on = !Display::isOn();
				Serial.printf("Display: %s\n", on ? "ON" : "OFF");
				Display::setPowerSave(!on);

				prefs.begin("rnode", false);
				prefs.putBool("disp", on);
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

	// Heartbeat LED
	static unsigned long last_blink = 0;
	if (millis() - last_blink > 3000) {
		digitalWrite(LED_USER_PIN, HIGH);
		delay(30);
		digitalWrite(LED_USER_PIN, LOW);
		last_blink = millis();
	}
}

int _write(int file, char *ptr, int len) {
	return Serial.write(ptr, len);
}
