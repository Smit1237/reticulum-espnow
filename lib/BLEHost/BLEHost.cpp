#include "BLEHost.h"
#include <Arduino.h>

BLEHost::BLEHost() {}

bool BLEHost::begin(const char* deviceName) {
	NimBLEDevice::init(deviceName);
	NimBLEDevice::setMTU(517);  // Request max MTU for large Reticulum packets

	_pServer = NimBLEDevice::createServer();
	_pServer->setCallbacks(this);

	// Create Nordic UART Service
	NimBLEService* pService = _pServer->createService(NUS_SERVICE_UUID);

	// TX characteristic — we notify data TO the phone
	_pTxChar = pService->createCharacteristic(
		NUS_TX_UUID,
		NIMBLE_PROPERTY::NOTIFY
	);

	// RX characteristic — phone writes data TO us
	_pRxChar = pService->createCharacteristic(
		NUS_RX_UUID,
		NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
	);
	_pRxChar->setCallbacks(this);

	pService->start();

	// Configure advertising — must be visible on Android
	NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

	// Put service UUID in advertising data (fits in 31 bytes with flags)
	pAdvertising->addServiceUUID(NUS_SERVICE_UUID);
	pAdvertising->setName(deviceName);

	// Enable scan response — Android requires this to show the device name
	pAdvertising->enableScanResponse(true);

	// Connectable + general discoverable (required for Android visibility)
	pAdvertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);

	// Advertising interval: 100-200ms — gives WiFi/ESP-NOW enough radio time
	// Too aggressive (20-40ms) starves WiFi and BLE packets get dropped
	pAdvertising->setMinInterval(160);   // 100ms (units of 0.625ms)
	pAdvertising->setMaxInterval(320);   // 200ms

	pAdvertising->start();

	Serial.printf("BLE: advertising as '%s'\n", deviceName);
	return true;
}

bool BLEHost::send(const uint8_t* data, size_t len) {
	if (!_connected || !_pTxChar) return false;

	// NimBLE handles fragmentation if len > negotiated MTU
	_pTxChar->setValue(data, len);
	_pTxChar->notify();
	return true;
}

size_t BLEHost::available() const {
	if (_rxHead >= _rxTail)
		return _rxHead - _rxTail;
	else
		return BLE_RX_BUFFER_SIZE - _rxTail + _rxHead;
}

int BLEHost::read() {
	if (_rxHead == _rxTail) return -1;
	uint8_t b = _rxBuf[_rxTail];
	_rxTail = (_rxTail + 1) % BLE_RX_BUFFER_SIZE;
	return b;
}

size_t BLEHost::readBytes(uint8_t* buf, size_t maxlen) {
	size_t count = 0;
	while (count < maxlen && _rxHead != _rxTail) {
		buf[count++] = _rxBuf[_rxTail];
		_rxTail = (_rxTail + 1) % BLE_RX_BUFFER_SIZE;
	}
	return count;
}

// --- NimBLE callbacks ---

void BLEHost::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
	_connected = true;
	Serial.printf("BLE: client connected [%s]\n", connInfo.getAddress().toString().c_str());
	// Allow further connections (for reconnect after disconnect)
	pServer->updateConnParams(connInfo.getConnHandle(), 12, 24, 0, 200);
}

void BLEHost::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
	_connected = false;
	Serial.printf("BLE: client disconnected (reason %d)\n", reason);
	// Restart advertising
	NimBLEDevice::startAdvertising();
}

void BLEHost::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
	NimBLEAttValue val = pCharacteristic->getValue();
	const uint8_t* data = val.data();
	size_t len = val.size();

	// Push received bytes into ring buffer
	for (size_t i = 0; i < len; i++) {
		size_t nextHead = (_rxHead + 1) % BLE_RX_BUFFER_SIZE;
		if (nextHead == _rxTail) break;  // Buffer full, drop
		_rxBuf[_rxHead] = data[i];
		_rxHead = nextHead;
	}
}
