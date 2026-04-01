#pragma once

#include <NimBLEDevice.h>
#include <stdint.h>
#include <stddef.h>

// Nordic UART Service UUIDs
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// Ring buffer size for incoming BLE data
#define BLE_RX_BUFFER_SIZE 2048

class BLEHost : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
public:
	BLEHost();

	// Initialize BLE with device name and start advertising
	bool begin(const char* deviceName);

	// Send data to connected BLE client (via TX notify)
	bool send(const uint8_t* data, size_t len);

	// Check if a client is connected
	bool connected() const { return _connected; }

	// Number of bytes available in RX buffer
	size_t available() const;

	// Read one byte from RX buffer (-1 if empty)
	int read();

	// Read up to maxlen bytes into buf, returns actual count
	size_t readBytes(uint8_t* buf, size_t maxlen);

private:
	// NimBLE server callbacks
	void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
	void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;

	// NimBLE characteristic callbacks (RX — host writes to us)
	void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;

	NimBLEServer*         _pServer = nullptr;
	NimBLECharacteristic* _pTxChar = nullptr;
	NimBLECharacteristic* _pRxChar = nullptr;

	volatile bool _connected = false;

	// Ring buffer for incoming BLE data
	uint8_t  _rxBuf[BLE_RX_BUFFER_SIZE];
	volatile size_t _rxHead = 0;
	volatile size_t _rxTail = 0;
};
