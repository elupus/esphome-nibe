#include "NibeGwComponent.h"

namespace esphome {
namespace nibegw {

NibeGwComponent::NibeGwComponent(esphome::GPIOPin *dir_pin) {
  gw_ = new NibeGw(this, dir_pin);
  gw_->setCallback(
      std::bind(&NibeGwComponent::callback_msg_received, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&NibeGwComponent::callback_msg_token_received, this, std::placeholders::_1, std::placeholders::_2));

  for (auto &x : requests_sockets_) {
    auto address = std::get<0>(x.first);
    auto token = std::get<1>(x.first);

    x.second.socket->onPacket(
        [this, address, token](AsyncUDPPacket packet) { token_request_cache(packet, address, token); });
  }
}

static request_data_type dedup(const byte *const data, int len, byte val) {
  request_data_type message;
  byte value = ~val;
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

void NibeGwComponent::callback_msg_received(const byte *const data, int len) {
  {
    request_key_type key{data[2] | (data[1] << 8), static_cast<byte>(data[3])};
    const auto &it = message_listener_.find(key);
    if (it != message_listener_.end()) {
      it->second(dedup(data, len, STARTBYTE_MASTER));
    }
  }

  if (!is_connected_) {
    return;
  }

  /* always sending standard data from modbus read token */
  auto &udp = requests_sockets_[request_key_type(MODBUS40, READ_TOKEN)].socket;
  if (!udp)
    return;

  ESP_LOGD(TAG, "UDP Packet with %d bytes to send", len);
  for (auto target = udp_targets_.begin(); target != udp_targets_.end(); target++) {
    ip_addr_t address = (ip_addr_t) std::get<0>(*target);
    if (!udp.writeTo(data, len, &address, std::get<1>(*target))) {
      ESP_LOGW(TAG, "UDP Packet send failed to %s:%d", std::get<0>(*target).str().c_str(), std::get<1>(*target));
    }
  }
}

void NibeGwComponent::token_request_cache(AsyncUDPPacket &udp, byte address, byte token) {
  if (!is_connected_) {
    return;
  }

  int size = udp.length();
  if (size == 0) {
    return;
  }

  ESP_LOGD(TAG, "UDP Packet token data of %d bytes received", size);

  if (size > MAX_DATA_LEN) {
    ESP_LOGE(TAG, "UDP Packet too large: %d", size);
    return;
  }

  network::IPAddress ip = udp.remoteIP();
  if (udp_source_ip_.size() && std::count(udp_source_ip_.begin(), udp_source_ip_.end(), ip) == 0) {
    ESP_LOGW(TAG, "UDP Packet wrong ip ignored %s", ip.str().c_str());
    return;
  }

  request_data_type request;
  request.assign(udp.data(), udp.data() + size);
  add_queued_request(address, token, std::move(request));
}

static int copy_request(const request_data_type &request, byte *data) {
  auto len = std::min(request.size(), (size_t) MAX_DATA_LEN);
  std::copy_n(request.begin(), len, data);
  return len;
}

int NibeGwComponent::callback_msg_token_received(eTokenType token, byte *data) {
  request_key_type key{data[2] | (data[1] << 8), static_cast<byte>(token)};

  {
    const auto &it = requests_.find(key);
    if (it != requests_.end()) {
      auto &queue = it->second;
      if (!queue.empty()) {
        auto len = copy_request(queue.front(), data);
        queue.pop();
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
  for (auto target = udp_targets_.begin(); target != udp_targets_.end(); target++) {
    ESP_LOGCONFIG(TAG, " Target: %s:%d", std::get<0>(*target).str().c_str(), std::get<1>(*target));
  }
  for (auto address = udp_source_ip_.begin(); address != udp_source_ip_.end(); address++) {
    ESP_LOGCONFIG(TAG, " Source: %s", address->str().c_str());
  }
  for (auto const &x : requests_sockets_) {
    ESP_LOGCONFIG(TAG, " Handler %x:%x Port: %d", std::get<0>(x.first), std::get<1>(x.first), x.second.port);
  }
}

void NibeGwComponent::loop() {
  if (network::is_connected() && !is_connected_) {
    ESP_LOGI(TAG, "Connecting network ports.");
    for (auto &x : requests_sockets_) {
      x.second.socket->listen(x.second.port);
    }
    is_connected_ = true;
  }

  if (!network::is_connected() && is_connected_) {
    ESP_LOGI(TAG, "Disconnecting network ports.");
    for (auto &x : requests_sockets_) {
      x.second.socket->close();
    }
    is_connected_ = false;
  }

  if (gw_->messageStillOnProgress()) {
    high_freq_.start();
  } else {
    high_freq_.stop();
  }
  gw_->loop();
}

}  // namespace nibegw
}  // namespace esphome