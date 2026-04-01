#include <Arduino.h>
#include "board_config.h"
#include "Display.h"

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>

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
#define PLATFORM_ESP32  0x80
#define MCU_ESP32       0x81
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

// BLE RX ring buffer
#define BLE_RX_BUF_SIZE 2048
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
		Serial.printf("BLE: CONNECTED [%s] handle=%d\n", ci.getAddress().toString().c_str(), ci.getConnHandle());
	}
	void onDisconnect(NimBLEServer* s, NimBLEConnInfo& ci, int reason) override {
		bleConnected = false;
		showingPin = false;
		Serial.printf("BLE: DISCONNECTED (reason=%d)\n", reason);
		NimBLEDevice::startAdvertising();
		Serial.println("BLE: re-advertising");
		updateDisplay();
	}
	void onMTUChange(uint16_t MTU, NimBLEConnInfo& ci) override {
		Serial.printf("BLE: MTU changed to %u\n", MTU);
	}

	// Called when NimBLE needs to display a passkey for pairing
	uint32_t onPassKeyDisplay() override {
		// Generate random 6-digit PIN
		pairingPin = esp_random() % 1000000;
		showingPin = true;
		Serial.printf("BLE: PAIRING PIN: %06lu\n", pairingPin);
		showPairingPin(pairingPin);
		return pairingPin;
	}

	void onAuthenticationComplete(NimBLEConnInfo& ci) override {
		showingPin = false;
		pairingMode = false;
		Serial.printf("BLE: AUTH complete, encrypted=%d, bonded=%d\n", ci.isEncrypted(), ci.isBonded());
		if (!ci.isEncrypted()) {
			Serial.println("BLE: pairing FAILED, disconnecting");
			NimBLEDevice::getServer()->disconnect(ci.getConnHandle());
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
		Serial.printf("BLE: TX notifications %s\n", subValue ? "SUBSCRIBED" : "unsubscribed");
	}
};

static ServerCB serverCB;
static RxCB rxCB;

// =============== ESP-NOW ===============

static void espnow_recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
	if (!espnow_rx_queue || len <= 0 || len > ESPNOW_MAX_PAYLOAD) return;
	espnow_rx_pkt_t pkt;
	pkt.len = (uint16_t)len;
	memcpy(pkt.data, data, len);
	xQueueSendFromISR(espnow_rx_queue, &pkt, nullptr);
}

static void espnow_send_cb(const wifi_tx_info_t* ti, esp_now_send_status_t st) { (void)ti; }

// =============== BLE TX helpers ===============

// Send raw bytes to BLE (KISS frame already built)
void ble_send(const uint8_t* data, size_t len) {
	if (!bleConnected || !pTxChar) return;
	pTxChar->setValue(data, len);
	pTxChar->notify();
}

// Build and send a simple KISS response: FEND CMD [bytes...] FEND
void kiss_respond(uint8_t cmd, const uint8_t* data, size_t len) {
	uint8_t buf[256];
	size_t pos = 0;
	buf[pos++] = FEND;
	buf[pos++] = cmd;
	for (size_t i = 0; i < len && pos < sizeof(buf) - 1; i++) {
		buf[pos++] = data[i];
	}
	buf[pos++] = FEND;
	ble_send(buf, pos);
}

void kiss_respond1(uint8_t cmd, uint8_t val) {
	kiss_respond(cmd, &val, 1);
}

// Build KISS DATA frame with escaping
void kiss_send_data(const uint8_t* payload, size_t len) {
	uint8_t buf[ESPNOW_MAX_PAYLOAD * 2 + 4];
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
	switch (kiss_cmd) {
	case CMD_DETECT:
		if (kiss_len >= 1 && kiss_buf[0] == DETECT_REQ) {
			Serial.println("KISS: DETECT");
			kiss_respond1(CMD_DETECT, DETECT_RESP);
		}
		break;

	case CMD_DATA:
		if (kiss_len > 0) {
			Serial.printf("KISS->ESPNOW: %u bytes\n", kiss_len);
			esp_err_t err = esp_now_send(BROADCAST_ADDR, kiss_buf, kiss_len);
			if (err == ESP_OK) {
				tx_count++;
				Serial.printf("  TX OK (total %lu)\n", tx_count);
			} else {
				Serial.printf("  TX FAIL 0x%X\n", err);
			}
			delay(2);
			kiss_respond1(CMD_READY, 0x01);
		}
		break;

	case CMD_FW_VERSION:
		Serial.println("KISS: FW_VERSION query");
		{ uint8_t ver[] = {FW_MAJ, FW_MIN}; kiss_respond(CMD_FW_VERSION, ver, 2); }
		break;

	case CMD_PLATFORM:
		Serial.println("KISS: PLATFORM query");
		kiss_respond1(CMD_PLATFORM, PLATFORM_ESP32);
		break;

	case CMD_MCU:
		Serial.println("KISS: MCU query");
		kiss_respond1(CMD_MCU, MCU_ESP32);
		break;

	case CMD_BOARD:
		Serial.println("KISS: BOARD query");
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
		Serial.printf("KISS: radio 0x%02X -> ACK\n", kiss_cmd);
		delay(5);
		kiss_respond(kiss_cmd, kiss_buf, kiss_len);
		break;

	case CMD_RADIO_STATE:
		Serial.printf("KISS: RADIO_STATE %d\n", kiss_len > 0 ? kiss_buf[0] : -1);
		delay(5);
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
		Serial.printf("KISS: unhandled cmd 0x%02X (%u bytes)\n", kiss_cmd, kiss_len);
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
	Serial.printf("\n*** PAIRING PIN: %06lu ***\n\n", (unsigned long)pin);
	Display::showPin(pin);
}

void updateDisplay() {
	if (showingPin) return;
	if (!Display::isOn()) return;

	if (pairingMode && !bleConnected) {
		Display::showPairingMode(bleName);
	} else {
		Display::showStatus(bleConnected, bleName, tx_count, rx_count,
		                    (unsigned long)ESP.getFreeHeap());
	}
}

// =============== SETUP ===============

void setup() {
	Serial.begin(115200);
	delay(500);
	Serial.println("\n=== ESP-NOW RNode BLE Bridge ===");

	pinMode(LED_USER_PIN, OUTPUT);
	digitalWrite(LED_USER_PIN, HIGH);
	pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP);

	// Display init
	Display::init();
	prefs.begin("rnode", false);
	bool displayOn = prefs.getBool("disp", true);
	prefs.end();
	if (!displayOn) Display::setPowerSave(true);
	Display::showBootScreen("RNode", "booting...");

	// Step 1: WiFi + ESP-NOW
	espnow_rx_queue = xQueueCreate(ESPNOW_RX_QUEUE_SIZE, sizeof(espnow_rx_pkt_t));
	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
	esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

	if (esp_now_init() == ESP_OK) {
		uint32_t ver = 0;
		esp_now_get_version(&ver);
		Serial.printf("ESP-NOW v%lu OK\n", ver);
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
	NimBLEDevice::setMTU(512);

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
	pSvc->start();

	NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
	pAdv->setName(bleName);
	pAdv->addServiceUUID(NUS_SERVICE_UUID);
	pAdv->enableScanResponse(true);
	pAdv->setMinInterval(160);  // 100ms
	pAdv->setMaxInterval(320);  // 200ms
	pAdv->start();

	Serial.printf("BLE: '%s' addr %s\n", bleName,
	              NimBLEDevice::getAddress().toString().c_str());
	Serial.println("Bridge ready.");

	digitalWrite(LED_USER_PIN, LOW);
	updateDisplay();
}

// =============== LOOP ===============

void loop() {
	// BLE RX -> KISS parser -> ESP-NOW
	while (bleRxHead != bleRxTail) {
		uint8_t b = bleRxBuf[bleRxTail];
		bleRxTail = (bleRxTail + 1) % BLE_RX_BUF_SIZE;
		kiss_feed(b);
	}

	// ESP-NOW RX -> KISS DATA frame -> BLE TX
	espnow_rx_pkt_t pkt;
	while (xQueueReceive(espnow_rx_queue, &pkt, 0) == pdTRUE) {
		rx_count++;
		if (bleConnected) {
			kiss_send_data(pkt.data, pkt.len);
		}
	}

	// BOOT button: double-tap = toggle display, long press (2s) = pairing mode
	static unsigned long btnDownAt = 0;
	static unsigned long lastTapAt = 0;
	static bool btnDown = false;
	static bool longPressHandled = false;

	bool pressed = (digitalRead(BUTTON_BOOT_PIN) == LOW);

	if (pressed && !btnDown) {
		// Button just pressed
		btnDown = true;
		btnDownAt = millis();
		longPressHandled = false;
	}

	if (pressed && btnDown && !longPressHandled && millis() - btnDownAt > 2000) {
		// Long press — pairing mode
		longPressHandled = true;
		Serial.println("BOOT: entering pairing mode");
		pairingMode = true;

		if (bleConnected && pServer) {
			pServer->disconnect(pServer->getPeerInfo(0).getConnHandle());
		}
		NimBLEDevice::deleteAllBonds();
		Serial.println("BOOT: bonds cleared, advertising for new pairing");
		NimBLEDevice::startAdvertising();

		// Turn display on for pairing
		if (!Display::isOn()) {
			Display::setPowerSave(false);
		}
		updateDisplay();

		for (int i = 0; i < 6; i++) {
			digitalWrite(LED_USER_PIN, i % 2);
			delay(100);
		}
		digitalWrite(LED_USER_PIN, LOW);
	}

	if (!pressed && btnDown) {
		// Button released
		btnDown = false;
		unsigned long pressDur = millis() - btnDownAt;

		if (!longPressHandled && pressDur < 500) {
			// Short tap — check for double-tap
			if (millis() - lastTapAt < 400) {
				// Double-tap detected — toggle display
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

	// OLED
	static unsigned long lastDisp = 0;
	if (millis() - lastDisp > 2000) { updateDisplay(); lastDisp = millis(); }

	// Heartbeat (only when not in pairing mode)
	static unsigned long lastBlink = 0;
	if (!pairingMode && millis() - lastBlink > 5000) {
		digitalWrite(LED_USER_PIN, HIGH);
		delay(50);
		digitalWrite(LED_USER_PIN, LOW);
		lastBlink = millis();
	}
}
