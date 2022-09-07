#include "NibeGwComponent.h"

NibeGwComponent::NibeGwComponent(int uart_no, int dir_pin, int rx_pin, int tx_pin)
{
    HardwareSerial* serial = new HardwareSerial(uart_no);
    gw_ = new NibeGw(serial, dir_pin, rx_pin, tx_pin);
    gw_->setCallback(std::bind(&NibeGwComponent::callback_msg_received, this, std::placeholders::_1, std::placeholders::_2),
                     std::bind(&NibeGwComponent::callback_msg_token_received, this, std::placeholders::_1, std::placeholders::_2));
#ifdef ENABLE_NIBE_DEBUG
    gw_->setDebugCallback(std::bind(&NibeGwComponent::callback_debug_wrapper, this, std::placeholders::_1, std::placeholders::_2);
#endif
    gw_->setVerboseLevel(1);
}

void NibeGwComponent::callback_msg_received(const byte* const data, int len)
{
    udp_read_.beginPacket(udp_target_ip_, udp_target_port_);
    udp_read_.write(data, len);
    if (udp_read_.endPacket()) {
        ESP_LOGD(TAG, "UDP Packet send succeeded with %d bytes", len);
    } else {
        ESP_LOGW(TAG, "UDP Packet send failed");
    }
}

int NibeGwComponent::token_request(WiFiUDP& udp, byte* data)
{
    int size = udp.parsePacket();
    if (size == 0) {
        return 0;
    }
    if (size > MAX_DATA_LEN) {
        ESP_LOGE(TAG, "UDP Packet too large: %d", size);
        return 0;
    }

    if (udp.remoteIP() != udp_target_ip_) {
        ESP_LOGW(TAG, "UDP Packet wrong wrong ip ignored");
        return 0;
    }

    return udp.read(data, size);
}

int NibeGwComponent::callback_msg_token_received(eTokenType token, byte* data)
{
    if (token == READ_TOKEN) {
        return token_request(udp_read_, data);
    }

    if (token == WRITE_TOKEN) {
        return token_request(udp_write_, data);
    }

    return 0;
}

void NibeGwComponent::callback_debug(byte verbose, char* data)
{
    if(verbose == 1){
        ESP_LOGI(TAG, "GW: %s", data);
    } else {
        ESP_LOGD(TAG, "GW: %s", data);
    }
}

void NibeGwComponent::setup() {
    ESP_LOGCONFIG(TAG, "Starting up sending to: %s:%d", udp_target_ip_.toString().c_str(), udp_target_port_);
    gw_->connect();
    udp_read_.begin(udp_read_port_);
    udp_write_.begin(udp_write_port_);
}

void NibeGwComponent::loop()
{
    gw_->loop();
}