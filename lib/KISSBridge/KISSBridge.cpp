#include "KISSBridge.h"
#include <cstring>

KISSBridge::KISSBridge() : _state(KISS_STATE_IDLE) {
	memset(&_frame, 0, sizeof(_frame));
}

bool KISSBridge::feed(uint8_t byte) {
	switch (_state) {
	case KISS_STATE_IDLE:
		if (byte == KISS_FEND) {
			_frame.len = 0;
			_frame.cmd = 0;
			_frame.complete = false;
			_state = KISS_STATE_CMD;
		}
		break;

	case KISS_STATE_CMD:
		if (byte == KISS_FEND) {
			// Double FEND — stay in CMD state (reset)
			break;
		}
		_frame.cmd = byte;
		_state = KISS_STATE_DATA;
		break;

	case KISS_STATE_DATA:
		if (byte == KISS_FEND) {
			// Frame complete
			_frame.complete = true;
			_state = KISS_STATE_IDLE;
			return true;
		}
		if (byte == KISS_FESC) {
			_state = KISS_STATE_ESCAPE;
			break;
		}
		if (_frame.len < KISS_MAX_PAYLOAD) {
			_frame.data[_frame.len++] = byte;
		}
		break;

	case KISS_STATE_ESCAPE:
		if (byte == KISS_TFEND) {
			if (_frame.len < KISS_MAX_PAYLOAD)
				_frame.data[_frame.len++] = KISS_FEND;
		} else if (byte == KISS_TFESC) {
			if (_frame.len < KISS_MAX_PAYLOAD)
				_frame.data[_frame.len++] = KISS_FESC;
		} else {
			// Invalid escape — store raw byte
			if (_frame.len < KISS_MAX_PAYLOAD)
				_frame.data[_frame.len++] = byte;
		}
		_state = KISS_STATE_DATA;
		break;
	}

	return false;
}

/*static*/ size_t KISSBridge::buildDataFrame(const uint8_t* payload, size_t payload_len,
                                              uint8_t* outbuf, size_t outbuf_size) {
	size_t pos = 0;
	if (pos >= outbuf_size) return 0;
	outbuf[pos++] = KISS_FEND;

	if (pos >= outbuf_size) return 0;
	outbuf[pos++] = KISS_CMD_DATA;

	for (size_t i = 0; i < payload_len; i++) {
		uint8_t b = payload[i];
		if (b == KISS_FEND) {
			if (pos + 2 > outbuf_size) return 0;
			outbuf[pos++] = KISS_FESC;
			outbuf[pos++] = KISS_TFEND;
		} else if (b == KISS_FESC) {
			if (pos + 2 > outbuf_size) return 0;
			outbuf[pos++] = KISS_FESC;
			outbuf[pos++] = KISS_TFESC;
		} else {
			if (pos >= outbuf_size) return 0;
			outbuf[pos++] = b;
		}
	}

	if (pos >= outbuf_size) return 0;
	outbuf[pos++] = KISS_FEND;

	return pos;
}

/*static*/ size_t KISSBridge::buildCmdFrame(uint8_t cmd, uint8_t value,
                                             uint8_t* outbuf, size_t outbuf_size) {
	if (outbuf_size < 4) return 0;
	outbuf[0] = KISS_FEND;
	outbuf[1] = cmd;
	outbuf[2] = value;
	outbuf[3] = KISS_FEND;
	return 4;
}

/*static*/ size_t KISSBridge::buildDetectResponse(uint8_t* outbuf, size_t outbuf_size) {
	return buildCmdFrame(KISS_CMD_DETECT, DETECT_RESP, outbuf, outbuf_size);
}
