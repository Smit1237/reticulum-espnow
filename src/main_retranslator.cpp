#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
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

#include "board_config.h"

// --- OLED ---
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// Display on/off state (persisted to NVS)
static Preferences prefs;
static bool displayOn = true;

// Status icon: radio waves (transport active)
static const uint8_t icon_radio[] = {
	0x00, // ........
	0x70, // .XXX....
	0x18, // ...XX...
	0x64, // .XX..X..
	0x12, // ...X..X.
	0x4A, // .X..X.X.
	0x2A, // ..X.X.X.
	0x2A  // ..X.X.X.
};

RNS::Reticulum reticulum({RNS::Type::NONE});
RNS::Interface espnow_interface({RNS::Type::NONE});

// --- OLED update ---

void oled_update() {
	if (!displayOn) { u8g2.clearBuffer(); u8g2.sendBuffer(); return; }

	char buf[20];
	u8g2.clearBuffer();

	// Row 1: radio icon + TRANSPORT label
	u8g2.drawXBM(0, 0, 8, 8, icon_radio);
	u8g2.setFont(u8g2_font_5x7_tf);
	u8g2.drawStr(10, 7, "TRANSPORT");

	// Row 2: RX bytes
	snprintf(buf, sizeof(buf), "RX: %lu", (unsigned long)espnow_interface.rxb());
	u8g2.drawStr(0, 17, buf);

	// Row 3: TX bytes
	snprintf(buf, sizeof(buf), "TX: %lu", (unsigned long)espnow_interface.txb());
	u8g2.drawStr(0, 27, buf);

	// Row 4: free heap
	snprintf(buf, sizeof(buf), "heap:%lu", (unsigned long)ESP.getFreeHeap());
	u8g2.drawStr(0, 37, buf);

	u8g2.sendBuffer();
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
		// Use ~70KB for tables, leave ~74KB headroom for runtime
		RNS::Transport::path_table_maxsize(48);      // ~48KB (most important)
		RNS::Transport::announce_table_maxsize(24);   // ~17KB
		RNS::Transport::hashlist_maxsize(128);        // ~5KB
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

	// Read display state from NVS
	prefs.begin("rnode", false);
	displayOn = prefs.getBool("disp", true);
	prefs.end();

	// OLED
	Wire.begin(OLED_SDA, OLED_SCL);
	u8g2.setBusClock(400000);
	u8g2.begin();
	if (!displayOn) u8g2.setPowerSave(1);

	u8g2.clearBuffer();
	u8g2.setFont(u8g2_font_5x7_tf);
	u8g2.drawXBM(0, 0, 8, 8, icon_radio);
	u8g2.drawStr(10, 7, "TRANSPORT");
	u8g2.drawStr(0, 17, "booting...");
	u8g2.sendBuffer();

	RNS::loglevel(RNS::LOG_TRACE);
	reticulum_setup();

	digitalWrite(LED_USER_PIN, LOW);
	oled_update();
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
				displayOn = !displayOn;
				Serial.printf("Display: %s\n", displayOn ? "ON" : "OFF");

				prefs.begin("rnode", false);
				prefs.putBool("disp", displayOn);
				prefs.end();

				if (displayOn) {
					u8g2.setPowerSave(0);
					oled_update();
				} else {
					u8g2.clearBuffer();
					u8g2.sendBuffer();
					u8g2.setPowerSave(1);
				}
				lastTapAt = 0;
			} else {
				lastTapAt = millis();
			}
		}
	}

	// Update display every 2 seconds
	static unsigned long last_display = 0;
	if (millis() - last_display > 2000) {
		oled_update();
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
