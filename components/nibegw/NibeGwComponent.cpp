#include "NibeGwComponent.h"

#include "esphome/components/network/util.h"

namespace esphome {

namespace nibegw {

NibeGwComponent::NibeGwComponent(esphome::GPIOPin *dir_pin) {
  gw_ = new NibeGw(this, dir_pin);
  gw_->setCallback(
      std::bind(&NibeGwComponent::callback_msg_received, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&NibeGwComponent::callback_msg_token_received, this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3));
}

static request_data_type dedup(const uint8_t *data, int len, uint8_t val) {
  request_data_type message;
  uint8_t value = ~val;
  for (int i = 5; i < len - 1; i++) {
    if (data[i] == val && value == val) {
      value = ~val;
      continue;
    }
    value = data[i];
    message.push_back(value);
  }
  return message;
}

void NibeGwComponent::callback_msg_received(const uint8_t *data, int len) {
  {
    request_key_type key{data[2] | (data[1] << 8), static_cast<uint8_t>(data[3])};
    const auto &it = message_listeners_.find(key);
    if (it != message_listeners_.end()) {
      auto deduped = dedup(data, len, STARTBYTE_MASTER);
      for (auto &listener : it->second) {
        listener(deduped);
      }
    }
  }

  if (transport_) {
    transport_->send_telegram(std::span<const uint8_t>(data, len));
  }
}

bool NibeGwComponent::queue_client_request(uint16_t address, uint8_t token, request_data_type request) {
  if (request.size() > MAX_DATA_LEN) {
    return false;
  }
  if (gw_->checkSlaveData(request.data(), request.size()) != PACKET_OK) {
    return false;
  }

  add_queued_request(address, token, std::move(request));
  return true;
}

static int copy_request(const request_data_type &request, uint8_t *data) {
  auto len = std::min(request.size(), (size_t) MAX_DATA_LEN);
  std::copy_n(request.begin(), len, data);
  return len;
}

int NibeGwComponent::callback_msg_token_received(uint16_t address, uint8_t command, uint8_t *data) {
  request_key_type key{address, command};

  {
    const auto &it = requests_.find(key);
    if (it != requests_.end()) {
      auto &queue = it->second;
      if (!queue.empty()) {
        auto len = copy_request(queue.front(), data);
        queue.pop_front();
        ESP_LOGD(TAG, "Response to address: 0x%x token: 0x%x bytes: %d", std::get<0>(key), std::get<1>(key), len);
        return len;
      }
    }
  }

  {
    const auto &it = requests_provider_.find(key);
    if (it != requests_provider_.end()) {
      auto len = copy_request(it->second(), data);
      ESP_LOGD(TAG, "Response to address: 0x%x token: 0x%x bytes: %d", std::get<0>(key), std::get<1>(key), len);
      return len;
    }
  }

  return 0;
}

void NibeGwComponent::setup() {
  ESP_LOGI(TAG, "Starting up");
  gw_->connect();
}

void NibeGwComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "NibeGw");
  if (transport_) {
    transport_->dump_config();
  }
  for (auto const &x : message_listeners_) {
    ESP_LOGCONFIG(TAG, " Listeners %x:%x Count: %zu", std::get<0>(x.first), std::get<1>(x.first), x.second.size());
  }
}

void NibeGwComponent::loop() {
  if (transport_) {
    transport_->loop(network::is_connected());
  }

  // Handle high frequency loop requirement
  if (gw_->messageStillOnProgress()) {
    high_freq_.start();
  } else {
    high_freq_.stop();
  }
  gw_->loop();
}

}  // namespace nibegw
}  // namespace esphome
