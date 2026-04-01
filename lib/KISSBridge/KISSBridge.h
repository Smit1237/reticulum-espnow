#pragma once

#include <stdint.h>
#include <stddef.h>

// KISS framing constants (matches RNode Framing.h)
#define KISS_FEND    0xC0
#define KISS_FESC    0xDB
#define KISS_TFEND   0xDC
#define KISS_TFESC   0xDD

// KISS commands
#define KISS_CMD_DATA       0x00
#define KISS_CMD_FREQUENCY  0x01
#define KISS_CMD_BANDWIDTH  0x02
#define KISS_CMD_TXPOWER    0x03
#define KISS_CMD_SF         0x04
#define KISS_CMD_CR         0x05
#define KISS_CMD_RADIO_STATE 0x06
#define KISS_CMD_DETECT     0x08
#define KISS_CMD_READY      0x0F
#define KISS_CMD_BOARD      0x47
#define KISS_CMD_PLATFORM   0x48
#define KISS_CMD_MCU        0x49
#define KISS_CMD_FW_VERSION 0x50
#define KISS_CMD_ROM_READ   0x51

// RNode detection
#define DETECT_REQ   0x73
#define DETECT_RESP  0x46

// RNode identity — we present as ESP32 platform
#define RNODE_PLATFORM_ESP32  0x80
#define RNODE_MCU_ESP32       0x81
#define RNODE_BOARD_GENERIC   0x40
#define RNODE_MAJ_VERSION     0x01
#define RNODE_MIN_VERSION     0x55

// Max packet size (ESP-NOW v2)
#define KISS_MAX_PAYLOAD 1470

// Frame parser state
enum KISSParserState {
	KISS_STATE_IDLE,       // Waiting for FEND
	KISS_STATE_CMD,        // Next byte is command
	KISS_STATE_DATA,       // Accumulating payload
	KISS_STATE_ESCAPE      // Previous byte was FESC
};

// Parsed frame result
struct KISSFrame {
	uint8_t  cmd;
	uint8_t  data[KISS_MAX_PAYLOAD];
	uint16_t len;
	bool     complete;
};

class KISSBridge {
public:
	KISSBridge();

	// Feed bytes from BLE into parser (streaming, handles chunked data)
	// Returns true when a complete frame is available via getFrame()
	bool feed(uint8_t byte);

	// Get last completed frame
	const KISSFrame& getFrame() const { return _frame; }

	// Build a KISS CMD_DATA frame from raw payload
	// Returns number of bytes written to outbuf
	static size_t buildDataFrame(const uint8_t* payload, size_t payload_len,
	                              uint8_t* outbuf, size_t outbuf_size);

	// Build a simple command response frame: FEND CMD VAL FEND
	static size_t buildCmdFrame(uint8_t cmd, uint8_t value,
	                             uint8_t* outbuf, size_t outbuf_size);

	// Build detect response
	static size_t buildDetectResponse(uint8_t* outbuf, size_t outbuf_size);

private:
	KISSParserState _state;
	KISSFrame       _frame;
};
