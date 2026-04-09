#include <Arduino.h>
#include "board_config.h"
#include "Display.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_log.h>
#include <Preferences.h>

// LED helpers — support GPIO (active high/low) and RGB NeoPixel
#ifndef LED_ACTIVE_LOW
  #define LED_ACTIVE_LOW 0
#endif
#define LED_ON_STATE  (LED_ACTIVE_LOW ? LOW : HIGH)
#define LED_OFF_STATE (LED_ACTIVE_LOW ? HIGH : LOW)

inline void led_on() {
#ifdef LED_RGB_PIN
	neopixelWrite(LED_RGB_PIN, 2, 2, 2);
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

// ============================================================
// HDLC framing — Reticulum SerialInterface protocol
// Simplest possible bridge: no handshake, no commands, no CRC.
// Just flag-delimited escaped data.
// ============================================================
#define HDLC_FLAG     0x7E
#define HDLC_ESC      0x7D
#define HDLC_ESC_MASK 0x20

// Serial speed — configurable via build flag
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 921600
#endif

// Max frame payload (Reticulum SerialInterface HW_MTU)
#define HDLC_MTU 564

// --- ESP-NOW ---
static const uint8_t BROADCAST_ADDR[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define ESPNOW_MAX_PAYLOAD 1470
#define ESPNOW_RX_QUEUE_SIZE 16

struct espnow_rx_pkt_t {
	uint8_t  data[ESPNOW_MAX_PAYLOAD];
	uint16_t len;
};
static QueueHandle_t espnow_rx_queue = nullptr;

static uint32_t tx_count = 0;
static uint32_t rx_count = 0;

// Display state
static Preferences prefs;

// =============== ESP-NOW callbacks ===============

static void espnow_recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
	if (!espnow_rx_queue || len <= 0 || len > ESPNOW_MAX_PAYLOAD) return;
	espnow_rx_pkt_t pkt;
	pkt.len = (uint16_t)len;
	memcpy(pkt.data, data, len);
	xQueueSendFromISR(espnow_rx_queue, &pkt, nullptr);
}

static void espnow_send_cb(const wifi_tx_info_t* ti, esp_now_send_status_t st) { (void)ti; }

// =============== HDLC TX to host (Serial) ===============

void hdlc_send(const uint8_t* payload, size_t len) {
	// Send HDLC frame: FLAG + escaped payload + FLAG
	Serial.write(HDLC_FLAG);
	for (size_t i = 0; i < len; i++) {
		uint8_t b = payload[i];
		if (b == HDLC_FLAG) {
			Serial.write(HDLC_ESC);
			Serial.write(HDLC_FLAG ^ HDLC_ESC_MASK);
		} else if (b == HDLC_ESC) {
			Serial.write(HDLC_ESC);
			Serial.write(HDLC_ESC ^ HDLC_ESC_MASK);
		} else {
			Serial.write(b);
		}
	}
	Serial.write(HDLC_FLAG);
}

// =============== HDLC RX parser ===============

static uint8_t  hdlc_buf[HDLC_MTU];
static uint16_t hdlc_len = 0;
static bool     hdlc_in_frame = false;
static bool     hdlc_escape = false;

void hdlc_feed(uint8_t b) {
	if (b == HDLC_FLAG) {
		if (hdlc_in_frame && hdlc_len > 0) {
			// Frame complete — send payload via ESP-NOW
			esp_err_t err = esp_now_send(BROADCAST_ADDR, hdlc_buf, hdlc_len);
			if (err == ESP_OK) {
				tx_count++;
			}
		}
		// Start new frame (or reset after complete frame)
		hdlc_in_frame = true;
		hdlc_len = 0;
		hdlc_escape = false;
		return;
	}

	if (!hdlc_in_frame) return;

	if (b == HDLC_ESC) {
		hdlc_escape = true;
		return;
	}

	if (hdlc_escape) {
		b ^= HDLC_ESC_MASK;
		hdlc_escape = false;
	}

	if (hdlc_len < HDLC_MTU) {
		hdlc_buf[hdlc_len++] = b;
	}
	// Silently drop bytes beyond MTU (matches Reticulum behavior)
}

// =============== Display ===============

void updateDisplay() {
	if (!Display::isOn()) return;
	static float chip_temp = 0;
	static unsigned long last_temp_read = 0;
	if (millis() - last_temp_read > 5000) {
		chip_temp = temperatureRead();
		last_temp_read = millis();
	}
	Display::showStatus(true, "SERIAL", tx_count, rx_count, chip_temp);
}

// =============== SETUP ===============

void setup() {
	Serial.begin(SERIAL_BAUD);

	// Suppress ALL log output to Serial — this port is exclusively for HDLC data.
	// ESP-IDF logs would corrupt the HDLC stream that rnsd/Reticulum expects.
	esp_log_level_set("*", ESP_LOG_NONE);

#if LED_USER_PIN >= 0
	pinMode(LED_USER_PIN, OUTPUT);
#endif
#if LED_PWR_PIN >= 0
	pinMode(LED_PWR_PIN, OUTPUT);
#endif
	led_on();
	pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP);

	// Display init
	Display::init();
	prefs.begin("rnode", false);
	bool displayOn = prefs.getBool("disp", true);
	prefs.end();
	if (!displayOn) {
		Display::setPowerSave(true);
		led_off();
		led_pwr_off();
	}
	Display::showBootScreen("SERIAL", "booting...");

	// ESP-NOW init
	espnow_rx_queue = xQueueCreate(ESPNOW_RX_QUEUE_SIZE, sizeof(espnow_rx_pkt_t));
	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
#if ESPNOW_LONG_RANGE
	esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
#endif
	esp_wifi_set_ps(WIFI_PS_NONE);  // No BLE, full power

	if (esp_now_init() == ESP_OK) {
		esp_now_register_send_cb(espnow_send_cb);
		esp_now_register_recv_cb(espnow_recv_cb);
		esp_now_peer_info_t peer = {};
		memcpy(peer.peer_addr, BROADCAST_ADDR, 6);
		peer.channel = ESPNOW_CHANNEL;
		peer.ifidx = WIFI_IF_STA;
		peer.encrypt = false;
		esp_now_add_peer(&peer);
	}

	Display::showBootScreen("SERIAL", "ready");
	led_off();
	updateDisplay();
}

// =============== LOOP (event-driven) ===============

#define HOUSEKEEPING_MS 10  // 10ms — fast for serial throughput

void loop() {
	// --- Event wait: block until ESP-NOW packet or timeout ---
	espnow_rx_pkt_t pkt;
	bool gotPacket = (xQueueReceive(espnow_rx_queue, &pkt, pdMS_TO_TICKS(HOUSEKEEPING_MS)) == pdTRUE);

	// --- ESP-NOW RX → HDLC frame → Serial TX ---
	if (gotPacket) {
		rx_count++;
		hdlc_send(pkt.data, pkt.len);
		// Drain additional queued packets
		while (xQueueReceive(espnow_rx_queue, &pkt, 0) == pdTRUE) {
			rx_count++;
			hdlc_send(pkt.data, pkt.len);
		}
	}

	// --- Serial RX → HDLC parser → ESP-NOW TX ---
	while (Serial.available()) {
		hdlc_feed((uint8_t)Serial.read());
	}

	// --- Button: double-tap = toggle display ---
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
				Display::setPowerSave(!on);

				if (!on) {
					led_off();
					led_pwr_off();
				} else {
					led_pwr_on();
				}

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

	// --- Periodic display update ---
	static unsigned long lastDisp = 0;
	if (millis() - lastDisp > 2000) {
		updateDisplay();
		Display::ensureBacklight();
		lastDisp = millis();
	}

	// --- LED: flash on packet activity ---
	if (!Display::isOn()) { led_off(); }
	else {
		static uint32_t last_tx = 0, last_rx = 0;
		if (tx_count != last_tx || rx_count != last_rx) {
			led_on();
			last_tx = tx_count;
			last_rx = rx_count;
		} else {
			led_off();
		}
	}
}
