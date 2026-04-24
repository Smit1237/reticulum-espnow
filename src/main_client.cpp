#include <Arduino.h>
#include "board_config.h"
#include "Display.h"
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>

// Dual serial output for boards with both USB CDC and UART (e.g. ESP32-S3)
#ifdef DUAL_SERIAL
  #define DualSerial(x)  do { Serial.x; Serial0.x; } while(0)
  #define DualPrintf(...) do { Serial.printf(__VA_ARGS__); Serial0.printf(__VA_ARGS__); } while(0)
#else
  #define DualSerial(x)  Serial.x
  #define DualPrintf(...) Serial.printf(__VA_ARGS__)
#endif

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
inline void led_pairing() {
#ifdef LED_RGB_PIN
	neopixelWrite(LED_RGB_PIN, 0, 0, 8);
#elif LED_USER_PIN >= 0
	digitalWrite(LED_USER_PIN, LED_ON_STATE);
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
// KISS framing — RNodeInterface protocol
// ============================================================
#define FEND  0xC0
#define FESC  0xDB
#define TFEND 0xDC
#define TFESC 0xDD

// KISS commands
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

// NUS UUIDs
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// BLE name stored globally for display
static char bleName[20] = "RNode";

// Pairing state
static volatile bool showingPin = false;
static volatile uint32_t pairingPin = 0;
static volatile bool pairingMode = false;

// Display on/off state (persisted to NVS)
static Preferences prefs;

// --- BLE globals ---
static NimBLEServer*         pServer = nullptr;
static NimBLECharacteristic* pTxChar = nullptr;
static volatile bool bleConnected = false;
static uint16_t bleMTU = 20;         // Negotiated MTU (updated in callback)
static uint16_t bleConnHandle = 0;   // Connection handle for param updates

// BLE RX ring buffer
#define BLE_RX_BUF_SIZE 6144
static uint8_t  bleRxBuf[BLE_RX_BUF_SIZE];
static volatile size_t bleRxHead = 0;
static volatile size_t bleRxTail = 0;

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

// =============== BLE callbacks ===============

// Forward declaration
void showPairingPin(uint32_t pin);
void updateDisplay();

class ServerCB : public NimBLEServerCallbacks {
	void onConnect(NimBLEServer* s, NimBLEConnInfo& ci) override {
		bleConnected = true;
		bleConnHandle = ci.getConnHandle();
		Serial.printf("BLE: CONNECTED [%s] handle=%d\r\n", ci.getAddress().toString().c_str(), bleConnHandle);
		// Connection parameter and PHY updates deferred to onAuthenticationComplete
		// to avoid disrupting the pairing handshake
	}
	void onDisconnect(NimBLEServer* s, NimBLEConnInfo& ci, int reason) override {
		bleConnected = false;
		showingPin = false;
		bleMTU = 20;  // Reset to default
		Serial.printf("BLE: DISCONNECTED (reason=%d)\r\n", reason);
		NimBLEDevice::startAdvertising();
		Serial.println("BLE: re-advertising");
		updateDisplay();
	}
	void onMTUChange(uint16_t MTU, NimBLEConnInfo& ci) override {
		bleMTU = MTU;
		static uint16_t lastMTU = 0;
		if (MTU != lastMTU) {
			Serial.printf("BLE: MTU %u (max notify payload: %u)\r\n", MTU, MTU - 3);
			lastMTU = MTU;
		}
	}

	// Called when NimBLE needs to display a passkey for pairing
	uint32_t onPassKeyDisplay() override {
		// Generate random 6-digit PIN
		pairingPin = esp_random() % 1000000;
		showingPin = true;
		Serial.printf("BLE: PAIRING PIN: %06lu\r\n", pairingPin);
		showPairingPin(pairingPin);
		return pairingPin;
	}

	void onAuthenticationComplete(NimBLEConnInfo& ci) override {
		showingPin = false;
		pairingMode = false;
		led_off();
		Serial.printf("BLE: AUTH complete, encrypted=%d, bonded=%d\r\n", ci.isEncrypted(), ci.isBonded());
		if (!ci.isEncrypted()) {
			Serial.println("BLE: pairing FAILED, disconnecting");
			NimBLEDevice::getServer()->disconnect(ci.getConnHandle());
		} else {
			// Auth succeeded — now safe to optimize connection parameters
			NimBLEServer* s = NimBLEDevice::getServer();

			// Request faster connection interval for throughput
			// min=15ms, max=30ms, latency=0, timeout=400ms (in 1.25ms units)
			// Note: 7.5ms is too aggressive for ESP32 classic with WiFi+BLE coex
			s->updateConnParams(bleConnHandle, 12, 24, 0, 400);

			// Request Data Length Extension (DLE): 251 bytes vs default 27
			// Allows ~10x fewer link-layer packets per notification
			s->setDataLen(bleConnHandle, 251);

			// Request 2M PHY on BLE 5.0 chips (C3, S3, C6)
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S3) || \
    defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
			uint8_t phyMask = BLE_GAP_LE_PHY_1M_MASK | BLE_GAP_LE_PHY_2M_MASK;
			s->updatePhy(bleConnHandle, phyMask, phyMask, 0);
			Serial.println("BLE: optimized (fast params + DLE + 2M PHY)");
#else
			Serial.println("BLE: optimized (fast params + DLE)");
#endif
		}
		updateDisplay();
	}
};

class RxCB : public NimBLECharacteristicCallbacks {
	void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& ci) override {
		NimBLEAttValue val = pChar->getValue();
		const uint8_t* data = val.data();
		size_t len = val.size();
		Serial.printf("BLE RX: %u bytes [", len);
		for (size_t i = 0; i < len && i < 16; i++) Serial.printf("%02X ", data[i]);
		if (len > 16) Serial.printf("...");
		Serial.println("]");
		for (size_t i = 0; i < len; i++) {
			size_t next = (bleRxHead + 1) % BLE_RX_BUF_SIZE;
			if (next == bleRxTail) break;
			bleRxBuf[bleRxHead] = data[i];
			bleRxHead = next;
		}
	}
	void onSubscribe(NimBLECharacteristic* pChar, NimBLEConnInfo& ci, uint16_t subValue) override {
		Serial.printf("BLE: TX notifications %s\r\n", subValue ? "SUBSCRIBED" : "unsubscribed");
	}
};

static ServerCB serverCB;
static RxCB rxCB;

// =============== ESP-NOW ===============

static void espnow_recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
	if (!espnow_rx_queue || len <= 0 || len > ESPNOW_MAX_PAYLOAD) return;
	// Static to avoid 1472 bytes on the WiFi task stack (typically 4-8 KB).
	// Safe: ESP-NOW callbacks are serialized by the WiFi task, no reentrancy.
	static espnow_rx_pkt_t pkt;
	pkt.len = (uint16_t)len;
	memcpy(pkt.data, data, len);
	xQueueSendFromISR(espnow_rx_queue, &pkt, nullptr);
}

static void espnow_send_cb(const wifi_tx_info_t* ti, esp_now_send_status_t st) { (void)ti; }

// =============== BLE TX helpers ===============

// Send raw bytes to BLE with MTU-aware fragmentation.
// BLE notifications cannot exceed (MTU - 3) bytes. If the payload is
// larger, we split it into chunks. Each notify() is retried briefly
// on ENOMEM (NimBLE TX buffers full).
void ble_send(const uint8_t* data, size_t len) {
	if (!bleConnected || !pTxChar) return;

	uint16_t maxChunk = (bleMTU >= 23) ? (bleMTU - 3) : 20;
	size_t offset = 0;

	while (offset < len && bleConnected) {
		size_t chunk = len - offset;
		if (chunk > maxChunk) chunk = maxChunk;

		pTxChar->setValue(data + offset, chunk);

		// Retry notify() up to 10 times on failure (BLE TX buffers full)
		bool sent = false;
		for (int retry = 0; retry < 10; retry++) {
			if (pTxChar->notify()) {
				sent = true;
				break;
			}
			// BLE TX buffers full — brief delay to let controller drain
			delay(2);
		}

		if (!sent) {
			Serial.println("BLE TX: notify failed, dropping remainder");
			break;
		}

		offset += chunk;

		// Yield between fragments to let BLE controller schedule the packet
		if (offset < len) vTaskDelay(1);
	}

	// Yield after every send to prevent back-to-back notify() overload.
	// Without this, rapid successive ble_send() calls (e.g. 4 handshake
	// responses parsed from one BLE write) overflow NimBLE TX buffers.
	// vTaskDelay(1) = one RTOS tick (~1ms) — minimal but sufficient.
	vTaskDelay(1);
}

// Build and send a simple KISS response: FEND CMD [escaped bytes...] FEND
// FEND (0xC0) and FESC (0xDB) inside the payload MUST be escaped — otherwise
// the host's parser terminates the frame early or mis-interprets the stream.
void kiss_respond(uint8_t cmd, const uint8_t* data, size_t len) {
	uint8_t buf[256];
	size_t pos = 0;
	buf[pos++] = FEND;
	buf[pos++] = cmd;
	for (size_t i = 0; i < len; i++) {
		uint8_t b = data[i];
		if (b == FEND) {
			if (pos + 3 > sizeof(buf)) break;
			buf[pos++] = FESC; buf[pos++] = TFEND;
		} else if (b == FESC) {
			if (pos + 3 > sizeof(buf)) break;
			buf[pos++] = FESC; buf[pos++] = TFESC;
		} else {
			if (pos + 2 > sizeof(buf)) break;
			buf[pos++] = b;
		}
	}
	buf[pos++] = FEND;
	ble_send(buf, pos);
}

void kiss_respond1(uint8_t cmd, uint8_t val) {
	kiss_respond(cmd, &val, 1);
}

// Build KISS DATA frame with escaping
void kiss_send_data(const uint8_t* payload, size_t len) {
	// CRITICAL: must be static — stack-allocating 2944 bytes here plus the
	// 1472-byte pkt in loop() would overflow the 8 KB Arduino task stack,
	// causing silent reboots. Safe because only called from the main loop
	// (single-threaded, no reentrancy).
	static uint8_t buf[ESPNOW_MAX_PAYLOAD * 2 + 4];
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
	ble_send(buf, pos);
}

// =============== KISS frame handler ===============

void handle_kiss_frame() {
	static bool handshake_logged = false;

	switch (kiss_cmd) {
	case CMD_DETECT:
		if (kiss_len >= 1 && kiss_buf[0] == DETECT_REQ) {
			if (!handshake_logged) Serial.println("KISS: handshake\r\n");
			kiss_respond1(CMD_DETECT, DETECT_RESP);
		}
		break;

	case CMD_DATA:
		if (kiss_len > 0) {
			handshake_logged = true;  // Stop logging handshake after first data
			esp_err_t err = esp_now_send(BROADCAST_ADDR, kiss_buf, kiss_len);
			if (err == ESP_OK) {
				tx_count++;
			} else {
				Serial.printf("ESP-NOW TX FAIL 0x%X\r\n", err);
			}
			// esp_now_send() is async — no delay needed before signaling READY
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
		// Radio config ACK — echo back (ESP-NOW has no tunable radio params)
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
	case 0x43:  // CMD_FB_WRITE (display data from Sideband)
	case 0x21:  // CMD_STAT_RSSI
	case 0x22:  // CMD_STAT_SNR
	case 0x23:  // CMD_STAT_CHTM
	case 0x24:  // CMD_STAT_PHYPRM
	case 0x25:  // CMD_STAT_BAT
	case 0x26:  // CMD_STAT_BATT
		// Silently accept — telemetry & display commands
		break;

	default:
		Serial.printf("KISS: unhandled cmd 0x%02X (%u bytes)\r\n", kiss_cmd, kiss_len);
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

	// First byte after FEND is the command
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

// =============== OLED ===============

void showPairingPin(uint32_t pin) {
	DualPrintf("\r\n*** PAIRING PIN: %06lu ***\r\n\r\n", (unsigned long)pin);
	Display::showPin(pin);
}

void updateDisplay() {
	if (showingPin) return;
	if (!Display::isOn()) return;

	if (pairingMode && !bleConnected) {
		Display::showPairingMode(bleName);
	} else {
		static float last_temp = 0;
		static unsigned long last_temp_read = 0;
		if (millis() - last_temp_read > 5000) {
			last_temp = temperatureRead();
			last_temp_read = millis();
		}
		Display::showStatus(bleConnected, bleName, tx_count, rx_count, last_temp);
	}
}

// =============== SETUP ===============

void setup() {
	Serial.begin(115200);
#ifdef DUAL_SERIAL
	Serial0.begin(115200);  // UART0 for boards with both USB CDC + UART
#endif
	delay(500);
#ifndef FIRMWARE_VERSION
	#define FIRMWARE_VERSION "dev"
#endif
	DualPrintf("\r\n=== ESP-NOW RNode BLE Bridge %s ===\r\n", FIRMWARE_VERSION);

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
	Display::showBootScreen("RNode", "booting...");

	// Step 1: WiFi + ESP-NOW
	espnow_rx_queue = xQueueCreate(ESPNOW_RX_QUEUE_SIZE, sizeof(espnow_rx_pkt_t));
	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
#if ESPNOW_LONG_RANGE
	esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
#endif
	esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

	if (esp_now_init() == ESP_OK) {
		uint32_t ver = 0;
		esp_now_get_version(&ver);
		DualPrintf("ESP-NOW v%lu OK\r\n", ver);
		esp_now_register_send_cb(espnow_send_cb);
		esp_now_register_recv_cb(espnow_recv_cb);
		esp_now_peer_info_t peer = {};
		memcpy(peer.peer_addr, BROADCAST_ADDR, 6);
		peer.channel = ESPNOW_CHANNEL;
		peer.ifidx = WIFI_IF_STA;
		peer.encrypt = false;
		esp_now_add_peer(&peer);
	} else {
		Serial.println("ESP-NOW FAILED");
	}

	// Step 2: NimBLE — name MUST start with "RNode " for Sideband discovery
	// Use last 2 bytes of MAC to make each node's BLE name unique
	uint8_t mac[6];
	esp_wifi_get_mac(WIFI_IF_STA, mac);
	snprintf(bleName, sizeof(bleName), "RNode %02X%02X", mac[4], mac[5]);

	NimBLEDevice::init(bleName);
	NimBLEDevice::setMTU(517);  // Max ATT_MTU for 512-byte payloads + 3-byte ATT header + 2

	// Enable bonding with passkey display — PIN shown on OLED during pairing
	NimBLEDevice::setSecurityAuth(true, true, true);  // bonding, MITM protection, secure connections
	NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);  // display passkey on OLED

	pServer = NimBLEDevice::createServer();
	pServer->setCallbacks(&serverCB);

	NimBLEService* pSvc = pServer->createService(NUS_SERVICE_UUID);
	pTxChar = pSvc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
	pTxChar->setCallbacks(&rxCB);  // Subscribe callback on TX char
	NimBLECharacteristic* pRxChar = pSvc->createCharacteristic(
		NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
	pRxChar->setCallbacks(&rxCB);
	// NimBLE 2.x: services auto-start with server, pSvc->start() is no-op

	NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
	pAdv->setName(bleName);
	pAdv->addServiceUUID(NUS_SERVICE_UUID);
	pAdv->enableScanResponse(true);
	pAdv->setMinInterval(160);  // 100ms
	pAdv->setMaxInterval(320);  // 200ms
	pAdv->start();

	DualPrintf("BLE: '%s' addr %s\r\n", bleName,
	           NimBLEDevice::getAddress().toString().c_str());
	DualPrintf("Bridge ready.\r\n");

	led_off();
	updateDisplay();
}

// =============== LOOP (event-driven) ===============
//
// Instead of polling in a tight loop, we block on the ESP-NOW RX queue
// with a short timeout. This gives zero CPU usage while waiting, with
// instant wake when mesh data arrives. The timeout handles periodic
// housekeeping (display, buttons, BLE data, LED).

// Housekeeping interval — how often we check buttons, display, BLE RX
// when no ESP-NOW data is arriving. Lower = more responsive buttons
// but slightly more CPU. 50ms = 20Hz housekeeping.
#define HOUSEKEEPING_MS 10

void loop() {
	// --- Event wait: block until ESP-NOW packet or timeout ---
	// Static to avoid 1472 bytes on the loop task stack (combined with
	// kiss_send_data's 2944-byte buffer, would overflow 8 KB stack).
	static espnow_rx_pkt_t pkt;
	bool gotPacket = (xQueueReceive(espnow_rx_queue, &pkt, pdMS_TO_TICKS(HOUSEKEEPING_MS)) == pdTRUE);

	// --- Process ESP-NOW data (instant wake path) ---
	if (gotPacket) {
		rx_count++;
		if (bleConnected) {
			kiss_send_data(pkt.data, pkt.len);
		}
		// Drain any additional queued packets
		while (xQueueReceive(espnow_rx_queue, &pkt, 0) == pdTRUE) {
			rx_count++;
			if (bleConnected) {
				kiss_send_data(pkt.data, pkt.len);
			}
		}
	}

	// --- Process BLE RX data (checked every housekeeping cycle) ---
	while (bleRxHead != bleRxTail) {
		uint8_t b = bleRxBuf[bleRxTail];
		bleRxTail = (bleRxTail + 1) % BLE_RX_BUF_SIZE;
		kiss_feed(b);
	}

	// --- Button handling ---
	static unsigned long btnDownAt = 0;
	static unsigned long lastTapAt = 0;
	static bool btnDown = false;
	static bool longPressHandled = false;

	bool pressed = (digitalRead(BUTTON_BOOT_PIN) == LOW);

	if (pressed && !btnDown) {
		btnDown = true;
		btnDownAt = millis();
		longPressHandled = false;
	}

	if (pressed && btnDown && !longPressHandled && millis() - btnDownAt > 2000) {
		longPressHandled = true;
		Serial.println("BOOT: entering pairing mode");
		pairingMode = true;

		if (bleConnected && pServer) {
			pServer->disconnect(pServer->getPeerInfo(0).getConnHandle());
		}
		NimBLEDevice::deleteAllBonds();
		Serial.println("BOOT: bonds cleared, advertising for new pairing");
		NimBLEDevice::startAdvertising();

		if (!Display::isOn()) {
			Display::setPowerSave(false);
		}
		updateDisplay();

		for (int i = 0; i < 6; i++) {
			if (i % 2) led_pairing(); else led_off();
			delay(100);
		}
		led_pairing();
	}

	if (!pressed && btnDown) {
		btnDown = false;
		unsigned long pressDur = millis() - btnDownAt;

		if (!longPressHandled && pressDur < 500) {
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

	// --- Periodic housekeeping (runs at HOUSEKEEPING_MS intervals) ---
	static unsigned long lastDisp = 0;
	if (millis() - lastDisp > 2000) {
		updateDisplay();
		Display::ensureBacklight();  // Detect if backlight was hijacked
		lastDisp = millis();
	}

	if (showingPin && pairingPin > 0) {
		static unsigned long lastPinPrint = 0;
		if (millis() - lastPinPrint > 10000) {
			DualPrintf("\r\n*** PAIRING PIN: %06lu ***\r\n\r\n", (unsigned long)pairingPin);
			lastPinPrint = millis();
		}
	}

	// --- LED management ---
	if (!pairingMode && Display::isOn()) {
		static unsigned long lastBlink = 0;
		if (millis() - lastBlink > 5000) {
			led_on();
			delay(50);
			led_off();
			lastBlink = millis();
		}
	}

	if (pairingMode && !bleConnected) {
		static unsigned long lastPairBlink = 0;
		if (millis() - lastPairBlink > 1000) {
			static bool pairLedOn = false;
			pairLedOn = !pairLedOn;
			if (pairLedOn) led_pairing(); else led_off();
			lastPairBlink = millis();
		}
	}
}
