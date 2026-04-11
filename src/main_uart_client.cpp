#include <Arduino.h>
#include "board_config.h"
#include "Display.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
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
// KISS framing — RNodeInterface protocol over UART
// ============================================================
#define FEND  0xC0
#define FESC  0xDB
#define TFEND 0xDC
#define TFESC 0xDD

#define CMD_DATA        0x00
#define CMD_FREQUENCY   0x01
#define CMD_BANDWIDTH   0x02
#define CMD_TXPOWER     0x03
#define CMD_SF          0x04
#define CMD_CR          0x05
#define CMD_RADIO_STATE 0x06
#define CMD_RADIO_LOCK  0x07
#define CMD_DETECT      0x08
#define CMD_READY       0x0F
#define CMD_BOARD       0x47
#define CMD_PLATFORM    0x48
#define CMD_MCU         0x49
#define CMD_FW_VERSION  0x50

#define DETECT_REQ      0x73
#define DETECT_RESP     0x46
#define KISS_PLATFORM_ESP32  0x80
#define KISS_MCU_ESP32       0x81
#define FW_MAJ          0x01
#define FW_MIN          0x3C  // 1.60

// UART speed — 921600 baud to match ESP-NOW throughput
#define UART_BAUD       921600

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

// KISS parser state
static uint8_t  kiss_buf[ESPNOW_MAX_PAYLOAD];
static uint16_t kiss_len = 0;
static uint8_t  kiss_cmd = 0;
static bool     kiss_has_cmd = false;
static bool     kiss_in_frame = false;
static bool     kiss_escape = false;

// Display state
static Preferences prefs;

// =============== ESP-NOW callbacks ===============

static void espnow_recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
	if (!espnow_rx_queue || len <= 0 || len > ESPNOW_MAX_PAYLOAD) return;
	static espnow_rx_pkt_t pkt;  // static: 1472 B too much for WiFi task stack
	pkt.len = (uint16_t)len;
	memcpy(pkt.data, data, len);
	xQueueSendFromISR(espnow_rx_queue, &pkt, nullptr);
}

static void espnow_send_cb(const wifi_tx_info_t* ti, esp_now_send_status_t st) { (void)ti; }

// =============== KISS TX to host (UART) ===============

void kiss_send(const uint8_t* data, size_t len) {
	Serial.write(data, len);
}

void kiss_respond(uint8_t cmd, const uint8_t* data, size_t len) {
	uint8_t buf[256];
	size_t pos = 0;
	buf[pos++] = FEND;
	buf[pos++] = cmd;
	for (size_t i = 0; i < len && pos < sizeof(buf) - 1; i++) {
		buf[pos++] = data[i];
	}
	buf[pos++] = FEND;
	kiss_send(buf, pos);
}

void kiss_respond1(uint8_t cmd, uint8_t val) {
	kiss_respond(cmd, &val, 1);
}

void kiss_send_data(const uint8_t* payload, size_t len) {
	static uint8_t buf[ESPNOW_MAX_PAYLOAD * 2 + 4];  // static: avoid stack overflow
	size_t pos = 0;
	buf[pos++] = FEND;
	buf[pos++] = CMD_DATA;
	for (size_t i = 0; i < len; i++) {
		uint8_t b = payload[i];
		if (b == FEND) {
			buf[pos++] = FESC; buf[pos++] = TFEND;
		} else if (b == FESC) {
			buf[pos++] = FESC; buf[pos++] = TFESC;
		} else {
			buf[pos++] = b;
		}
		if (pos >= sizeof(buf) - 2) break;
	}
	buf[pos++] = FEND;
	kiss_send(buf, pos);
}

// =============== KISS frame handler ===============

void handle_kiss_frame() {
	switch (kiss_cmd) {
	case CMD_DETECT:
		if (kiss_len >= 1 && kiss_buf[0] == DETECT_REQ) {
			kiss_respond1(CMD_DETECT, DETECT_RESP);
		}
		break;

	case CMD_DATA:
		if (kiss_len > 0) {
			esp_err_t err = esp_now_send(BROADCAST_ADDR, kiss_buf, kiss_len);
			if (err == ESP_OK) tx_count++;
			kiss_respond1(CMD_READY, 0x01);
		}
		break;

	case CMD_FW_VERSION:
		{ uint8_t ver[] = {FW_MAJ, FW_MIN}; kiss_respond(CMD_FW_VERSION, ver, 2); }
		break;

	case CMD_PLATFORM:
		kiss_respond1(CMD_PLATFORM, KISS_PLATFORM_ESP32);
		break;

	case CMD_MCU:
		kiss_respond1(CMD_MCU, KISS_MCU_ESP32);
		break;

	case CMD_BOARD:
		kiss_respond1(CMD_BOARD, 0x40);
		break;

	case CMD_READY:
		kiss_respond1(CMD_READY, 0x01);
		break;

	case CMD_FREQUENCY:
	case CMD_BANDWIDTH:
	case CMD_TXPOWER:
	case CMD_SF:
	case CMD_CR:
		kiss_respond(kiss_cmd, kiss_buf, kiss_len);
		break;

	case CMD_RADIO_STATE:
		kiss_respond1(CMD_RADIO_STATE, kiss_len > 0 ? kiss_buf[0] : 0x01);
		break;

	case CMD_RADIO_LOCK:
	case 0x0A:  // CMD_LEAVE
	case 0x0B:  // CMD_ST_ALOCK
	case 0x0E:  // CMD_PROMISC
	case 0x41:  // CMD_FB_EXT
	case 0x42:  // CMD_FB_READ
	case 0x43:  // CMD_FB_WRITE
	case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
		break;  // Silently accept

	default:
		break;
	}
}

// Feed one byte into KISS parser
void kiss_feed(uint8_t byte) {
	if (byte == FEND) {
		if (kiss_in_frame && (kiss_len > 0 || kiss_has_cmd)) {
			handle_kiss_frame();
		}
		kiss_len = 0;
		kiss_cmd = 0;
		kiss_has_cmd = false;
		kiss_in_frame = true;
		kiss_escape = false;
		return;
	}

	if (!kiss_in_frame) return;

	if (!kiss_has_cmd) {
		kiss_cmd = byte;
		kiss_has_cmd = true;
		return;
	}

	if (kiss_escape) {
		if (byte == TFEND) byte = FEND;
		else if (byte == TFESC) byte = FESC;
		kiss_escape = false;
		if (kiss_len < ESPNOW_MAX_PAYLOAD) kiss_buf[kiss_len++] = byte;
		return;
	}

	if (byte == FESC) {
		kiss_escape = true;
		return;
	}

	if (kiss_len < ESPNOW_MAX_PAYLOAD) kiss_buf[kiss_len++] = byte;
}

// =============== Display ===============

static float chip_temp = 0;
static unsigned long last_temp_read = 0;

void updateDisplay() {
	if (!Display::isOn()) return;
	if (millis() - last_temp_read > 5000) {
		chip_temp = temperatureRead();
		last_temp_read = millis();
	}
	Display::showStatus(true, "UART", tx_count, rx_count, chip_temp);
}

// =============== SETUP ===============

void setup() {
	// UART for KISS protocol to host PC
	Serial.begin(UART_BAUD);
	delay(500);

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
	Display::showBootScreen("UART Node", "booting...");

	// ESP-NOW init
	espnow_rx_queue = xQueueCreate(ESPNOW_RX_QUEUE_SIZE, sizeof(espnow_rx_pkt_t));
	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
#if ESPNOW_LONG_RANGE
	esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
#endif
	esp_wifi_set_ps(WIFI_PS_NONE);  // No BLE, so full power for ESP-NOW

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

	Display::showBootScreen("UART Node", "ready");
	led_off();
	updateDisplay();
}

// =============== LOOP (event-driven) ===============
//
// Block on ESP-NOW RX queue with short timeout. CPU sleeps while idle,
// wakes instantly on mesh data. UART RX and housekeeping (buttons,
// display) run at the housekeeping rate during idle periods.

#define HOUSEKEEPING_MS 10  // 10ms — fast enough for 921600 baud UART

void loop() {
	// --- Event wait: block until ESP-NOW packet or timeout ---
	static espnow_rx_pkt_t pkt;  // static: avoid 1472 B on loop stack
	bool gotPacket = (xQueueReceive(espnow_rx_queue, &pkt, pdMS_TO_TICKS(HOUSEKEEPING_MS)) == pdTRUE);

	// --- Process ESP-NOW data (instant wake path) ---
	if (gotPacket) {
		rx_count++;
		kiss_send_data(pkt.data, pkt.len);
		// Drain any additional queued packets
		while (xQueueReceive(espnow_rx_queue, &pkt, 0) == pdTRUE) {
			rx_count++;
			kiss_send_data(pkt.data, pkt.len);
		}
	}

	// --- UART RX -> KISS parser -> ESP-NOW ---
	while (Serial.available()) {
		kiss_feed((uint8_t)Serial.read());
	}

	// --- Button handling: double-tap = toggle display ---
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

	// --- LED: flash on packet activity (only when display is on) ---
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
