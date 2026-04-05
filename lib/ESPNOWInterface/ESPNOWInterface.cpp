#include "ESPNOWInterface.h"

#include <Log.h>
#include <Utilities/OS.h>

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>

#include <cstring>

using namespace RNS;

// Static member initialization
QueueHandle_t ESPNOWInterface::_rx_queue = nullptr;
SemaphoreHandle_t ESPNOWInterface::_rx_notify = nullptr;
uint8_t ESPNOWInterface::_local_mac[6] = {0};
volatile uint32_t ESPNOWInterface::_rx_drops = 0;
uint32_t ESPNOWInterface::_rx_packets = 0;
uint32_t ESPNOWInterface::_tx_packets = 0;
ESPNOWInterface::tx_filter_entry_t ESPNOWInterface::_tx_filter[TX_FILTER_SLOTS] = {};
uint8_t ESPNOWInterface::_tx_filter_idx = 0;

// Broadcast MAC address
static const uint8_t BROADCAST_ADDR[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

ESPNOWInterface::ESPNOWInterface(const char* name /*= "ESPNOWInterface"*/, uint8_t channel /*= 1*/)
	: RNS::InterfaceImpl(name), _channel(channel)
{
	_IN  = true;
	_OUT = true;
	_bitrate  = 1000000;  // ESP-NOW theoretical max ~1 Mbps
	_HW_MTU   = ESPNOW_MAX_PAYLOAD;
}

/*virtual*/ ESPNOWInterface::~ESPNOWInterface() {
	stop();
}

bool ESPNOWInterface::start() {
	_online = false;
	INFO("ESP-NOW initializing...");

	// Create RX queue (hold up to 16 packets)
	if (!_rx_queue) {
		_rx_queue = xQueueCreate(16, sizeof(rx_packet_t));
		if (!_rx_queue) {
			ERROR("ESP-NOW: failed to create RX queue");
			return false;
		}
	}

	// Create notification semaphore for event-driven loops
	if (!_rx_notify) {
		_rx_notify = xSemaphoreCreateBinary();
	}

	// Initialize WiFi in STA mode (required for ESP-NOW)
	WiFi.mode(WIFI_STA);
	WiFi.disconnect();

	// Set WiFi channel
	esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);

	// Enable 802.11 LR mode if configured (doubles range, halves throughput)
#if ESPNOW_LONG_RANGE
	esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
	INFO("ESP-NOW: 802.11 LR mode enabled");
#endif

	// Disable WiFi power saving for lower latency
	esp_wifi_set_ps(WIFI_PS_NONE);

	// Initialize ESP-NOW
	esp_err_t result = esp_now_init();
	if (result != ESP_OK) {
		ERRORF("ESP-NOW: init failed, error 0x%X", result);
		return false;
	}

	// Check ESP-NOW version
	uint32_t version = 0;
	esp_now_get_version(&version);
	INFOF("ESP-NOW initialized, version: %lu", version);

	// Register callbacks
	esp_now_register_send_cb(on_data_sent);
	esp_now_register_recv_cb(on_data_recv);

	// Add broadcast peer
	esp_now_peer_info_t broadcast_peer = {};
	memcpy(broadcast_peer.peer_addr, BROADCAST_ADDR, ESP_NOW_ETH_ALEN);
	broadcast_peer.channel = _channel;
	broadcast_peer.ifidx   = WIFI_IF_STA;
	broadcast_peer.encrypt = false;

	result = esp_now_add_peer(&broadcast_peer);
	if (result != ESP_OK) {
		ERRORF("ESP-NOW: failed to add broadcast peer, error 0x%X", result);
		esp_now_deinit();
		return false;
	}

	// Store and print local MAC address
	esp_wifi_get_mac(WIFI_IF_STA, _local_mac);
	INFOF("ESP-NOW: local MAC %02X:%02X:%02X:%02X:%02X:%02X",
		_local_mac[0], _local_mac[1], _local_mac[2], _local_mac[3], _local_mac[4], _local_mac[5]);
	INFOF("ESP-NOW: channel %d, HW_MTU %u", _channel, _HW_MTU);

	_online = true;
	INFO("ESP-NOW init succeeded.");
	return true;
}

void ESPNOWInterface::stop() {
	if (_online) {
		INFO("ESP-NOW deinitializing...");
		esp_now_unregister_recv_cb();
		esp_now_unregister_send_cb();
		esp_now_deinit();
		_online = false;
	}
}

void ESPNOWInterface::loop() {
	if (!_online) return;

	// Process all queued received packets
	rx_packet_t pkt;
	while (xQueueReceive(_rx_queue, &pkt, 0) == pdTRUE) {
		// Check against recently sent packets — skip self-heard broadcasts.
		// This prevents Transport from allocating a full Packet object just
		// to discover the packet hash is already in _packet_hashlist.
		bool is_echo = false;
		for (uint8_t i = 0; i < TX_FILTER_SLOTS; i++) {
			auto& slot = _tx_filter[i];
			if (slot.len == pkt.len && slot.len > 0) {
				uint8_t n = pkt.len < TX_FILTER_PREFIX ? pkt.len : TX_FILTER_PREFIX;
				if (memcmp(slot.prefix, pkt.data, n) == 0) {
					is_echo = true;
					slot.len = 0;  // consume — only match once
					break;
				}
			}
		}
		if (is_echo) continue;

		_rx_packets++;
		Bytes data(pkt.data, pkt.len);
		on_incoming(data);
	}
}

/*virtual*/ void ESPNOWInterface::send_outgoing(const Bytes& data) {
	DEBUGF("%s.send_outgoing: %lu bytes", toString().c_str(), data.size());
	try {
		if (_online) {
			if (data.size() > ESPNOW_MAX_PAYLOAD) {
				ERRORF("ESP-NOW: packet too large (%lu > %u)", data.size(), ESPNOW_MAX_PAYLOAD);
				return;
			}

			esp_err_t result = esp_now_send(BROADCAST_ADDR, data.data(), data.size());
			if (result != ESP_OK) {
				ERRORF("ESP-NOW: send failed, error 0x%X", result);
			} else {
				TRACEF("ESP-NOW: sent %lu bytes", data.size());
				_tx_packets++;

				// Record prefix for self-send filter
				auto& slot = _tx_filter[_tx_filter_idx % TX_FILTER_SLOTS];
				slot.len = (uint16_t)data.size();
				uint8_t n = data.size() < TX_FILTER_PREFIX ? data.size() : TX_FILTER_PREFIX;
				memcpy(slot.prefix, data.data(), n);
				_tx_filter_idx++;
			}

			// Post-send housekeeping
			InterfaceImpl::handle_outgoing(data);
		}
	}
	catch (const std::exception& e) {
		ERRORF("Could not transmit on %s: %s", toString().c_str(), e.what());
	}
}

void ESPNOWInterface::on_incoming(const Bytes& data) {
	DEBUGF("%s.on_incoming: %lu bytes", toString().c_str(), data.size());
	InterfaceImpl::handle_incoming(data);
}

// Static callback: called from WiFi task context when data is sent
/*static*/ void ESPNOWInterface::on_data_sent(const wifi_tx_info_t* tx_info, esp_now_send_status_t status) {
	(void)tx_info;
	if (status != ESP_NOW_SEND_SUCCESS) {
		DEBUG("ESP-NOW: send callback reported failure");
	}
}

// Static callback: called from WiFi task context when data is received
/*static*/ void ESPNOWInterface::on_data_recv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int data_len) {
	if (!_rx_queue || data_len <= 0 || data_len > ESPNOW_MAX_PAYLOAD) return;

	// Drop packets from our own MAC (prevents broadcast storms)
	if (recv_info && recv_info->src_addr &&
	    memcmp(recv_info->src_addr, _local_mac, 6) == 0) {
		return;
	}

	rx_packet_t pkt;
	memcpy(pkt.src_mac, recv_info->src_addr, 6);
	pkt.len = (uint16_t)data_len;
	memcpy(pkt.data, data, data_len);

	// Non-blocking queue send — track drops if queue is full
	if (xQueueSendFromISR(_rx_queue, &pkt, nullptr) != pdTRUE) {
		_rx_drops = _rx_drops + 1;  // Avoid deprecated volatile++ in C++20
	}

	// Notify any task blocking on waitForData()
	if (_rx_notify) {
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xSemaphoreGiveFromISR(_rx_notify, &xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
}
