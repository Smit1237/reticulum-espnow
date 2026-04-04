#pragma once

#include <Interface.h>
#include <Bytes.h>
#include <Type.h>

#include <esp_now.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <stdint.h>

class ESPNOWInterface : public RNS::InterfaceImpl {

public:
	ESPNOWInterface(const char* name = "ESPNOWInterface", uint8_t channel = 1);
	virtual ~ESPNOWInterface();

	virtual bool start();
	virtual void stop();
	virtual void loop();

private:
	virtual void send_outgoing(const RNS::Bytes& data);
	void on_incoming(const RNS::Bytes& data);

	// ESP-NOW callbacks (static, required by ESP-IDF API)
	static void on_data_sent(const wifi_tx_info_t* tx_info, esp_now_send_status_t status);
	static void on_data_recv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int data_len);

private:
	uint8_t _channel = 1;

	// FreeRTOS queue for passing received packets from ISR-context callback to loop()
	static QueueHandle_t _rx_queue;

	// Max ESP-NOW v2 payload
	static constexpr uint16_t ESPNOW_MAX_PAYLOAD = 1470;

	// RX queue item
	struct rx_packet_t {
		uint8_t  src_mac[6];
		uint8_t  data[ESPNOW_MAX_PAYLOAD];
		uint16_t len;
	};

	// Local MAC for filtering own packets
	static uint8_t _local_mac[6];

	// RX queue drop counter
	static volatile uint32_t _rx_drops;
public:
	static uint32_t rxDrops() { return _rx_drops; }
};
