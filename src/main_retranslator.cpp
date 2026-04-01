#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

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

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

RNS::Reticulum reticulum({RNS::Type::NONE});
RNS::Interface espnow_interface({RNS::Type::NONE});

void oled_update() {
	char line1[20], line2[20], line3[20];
	snprintf(line1, sizeof(line1), "TRANSPORT");
	snprintf(line2, sizeof(line2), "rx:%lu tx:%lu", (unsigned long)espnow_interface.rxb(), (unsigned long)espnow_interface.txb());
	snprintf(line3, sizeof(line3), "heap:%lu", (unsigned long)ESP.getFreeHeap());

	u8g2.clearBuffer();
	u8g2.setFont(u8g2_font_5x7_tf);
	u8g2.drawStr(0, 7, line1);
	u8g2.drawStr(0, 17, line2);
	u8g2.drawStr(0, 27, line3);
	u8g2.sendBuffer();
}

void reticulum_setup() {
	INFO("Setting up Retranslator node...");

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

		HEAD("Creating Reticulum transport instance...", RNS::LOG_TRACE);
		reticulum = RNS::Reticulum();
		reticulum.transport_enabled(true);
		reticulum.probe_destination_enabled(true);
		reticulum.start();

		HEAD("Retranslator ready!", RNS::LOG_TRACE);
	}
	catch (const std::exception& e) {
		ERRORF("Exception in reticulum_setup: %s", e.what());
	}
}

void setup() {
	Serial.begin(115200);
	delay(500);
	Serial.println();
	Serial.println("=== RETRANSLATOR NODE ===");

	pinMode(LED_USER_PIN, OUTPUT);
	digitalWrite(LED_USER_PIN, HIGH);

	Wire.begin(OLED_SDA, OLED_SCL);
	u8g2.setBusClock(400000);
	u8g2.begin();

	u8g2.clearBuffer();
	u8g2.setFont(u8g2_font_5x7_tf);
	u8g2.drawStr(0, 7, "RELAY");
	u8g2.drawStr(0, 17, "booting...");
	u8g2.sendBuffer();

	RNS::loglevel(RNS::LOG_TRACE);
	reticulum_setup();

	digitalWrite(LED_USER_PIN, LOW);
	oled_update();
}

void loop() {
	reticulum.loop();

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
