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

void NibeGwComponent::token_request_cache(WiFiUDP& udp, byte address, byte token)
{
    int size = udp.parsePacket();
    if (size == 0) {
        return;
    }

    ESP_LOGD(TAG, "UDP Packet token data of %d bytes received", size);

    if (size > MAX_DATA_LEN) {
        ESP_LOGE(TAG, "UDP Packet too large: %d", size);
        return;
    }

    if (udp_source_ip_.size() && udp_source_ip_.count(udp.remoteIP()) != 0) {
        ESP_LOGW(TAG, "UDP Packet wrong wrong ip ignored");
        return;
    }

    request_data_type request;
    request.resize(size);
    udp.read(&request[0], size);
    add_queued_request(address, token, std::move(request));
}

static int copy_request(const request_data_type& request, byte* data)
{
    auto len = std::min(request.size(), (size_t)MAX_DATA_LEN);
    std::copy_n(request.begin(), len, data);
    return len;
}

int NibeGwComponent::callback_msg_token_received(eTokenType token, byte* data)
{

    request_key_type key {data[2], static_cast<byte>(token)};

    {
        const auto& it = requests_.find(key);
        if (it != requests_.end()) {
            auto& queue = it->second;
            if (!queue.empty()) {
                auto len = copy_request(queue.front(), data);
                queue.pop();
                ESP_LOGD(TAG, "Response to address: 0x%x token: 0x%x bytes: %d", std::get<0>(key), std::get<1>(key), len);
                return len;
            }
        }
    }

    {
        const auto& it = requests_const_.find(key);
        if (it != requests_const_.end()) {
            ESP_LOGD(TAG, "Constant to address: 0x%x token: 0x%x bytes: %d", std::get<0>(key), std::get<1>(key), it->second.size());
            return copy_request(it->second, data);
        }
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
    token_request_cache(udp_read_, MODBUS40, READ_TOKEN);
    token_request_cache(udp_write_, MODBUS40, WRITE_TOKEN);
    do {
        gw_->loop();
    } while(gw_->messageStillOnProgress());
}
