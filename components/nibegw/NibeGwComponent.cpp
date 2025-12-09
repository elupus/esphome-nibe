#include "NibeGwComponent.h"

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
    const auto &it = message_listener_.find(key);
    if (it != message_listener_.end()) {
      it->second(dedup(data, len, STARTBYTE_MASTER));
    }
  }

  if (!is_connected_) {
    return;
  }

  if (!udp_read_) {
    ESP_LOGW(TAG, "UDP read socket not available");
    return;
  }

  // Send to all UDP targets
  for (auto &&[target, timestamp] : udp_targets_) {
    int result = udp_read_->sendto(data, len, 0, (sockaddr *) &target.storage, target.len);
    if (result < 0) {
      ESP_LOGW(TAG, "UDP sendto failed to %s, error: %d", target.str().c_str(), errno);
    }
  }
}

void NibeGwComponent::recv_local_socket(std::unique_ptr<socket::Socket> &fd, int address, int token) {
  request_data_type request(MAX_DATA_LEN);

  socket_address from;
  int n = fd->recvfrom(request.data(), request.size(), (sockaddr *) &from.storage, &from.len);
  if (n < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ESP_LOGW(TAG, "recvfrom error on read socket: %d", errno);
    }
    return;
  }
  request.resize(n);

  if (udp_sources_.size() &&
      none_of(udp_sources_.begin(), udp_sources_.end(), [&](auto &source) { return from.matches(source); })) {
    ESP_LOGW(TAG, "UDP Packet wrong ip ignored %s", from.str().c_str());
    return;
  }

  if (gw_->checkSlaveData(request.data(), request.size()) != PACKET_OK) {
    ESP_LOGW(TAG, "Received invalid packet from %s, %d bytes", from.str().c_str(), n);
    return;
  }

  /* store this as a new target */
  uint32_t now = millis();
  auto [it, inserted] = udp_targets_.insert_or_assign(from, now);
  if (inserted) {
    ESP_LOGI(TAG, "New target added %s", from.str().c_str());
  }

  add_queued_request(address, token, std::move(request));
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
  for (auto &&[address, timeout] : udp_targets_) {
    ESP_LOGCONFIG(TAG, " Target: %s", address.str().c_str());
  }
  for (auto &&address : udp_sources_) {
    ESP_LOGCONFIG(TAG, " Source: %s", address.str().c_str());
  }
  ESP_LOGCONFIG(TAG, " Read Port: %d", udp_read_port_);
  ESP_LOGCONFIG(TAG, " Write Port: %d", udp_write_port_);
}

std::unique_ptr<socket::Socket> NibeGwComponent::bind_local_socket(int port) {
  auto fd = socket::socket_ip_loop_monitored(SOCK_DGRAM, 0);
  if (fd) {
    // Set non-blocking
    fd->setblocking(true);

    // Bind to write port
    socket_address address(port);

    if (fd->bind((sockaddr *) &address.storage, address.len) < 0) {
      ESP_LOGE(TAG, "Failed to bind socket to port %d, error: %d", port, errno);
      fd.release();
    } else {
      ESP_LOGI(TAG, "UDP socket bound to port %d", port);
    }
  } else {
    ESP_LOGE(TAG, "Failed to create socket, error: %d", errno);
  }
  return fd;
}

void NibeGwComponent::loop() {
  // Handle network connection state

  if (network::is_connected()) {
    if (!is_connected_) {
      ESP_LOGI(TAG, "Connecting network ports.");
      is_connected_ = true;
    }

    // Create and bind read socket
    if (!udp_read_) {
      udp_read_ = bind_local_socket(udp_read_port_);
    }

    if (!udp_write_) {
      udp_write_ = bind_local_socket(udp_write_port_);
    }
  } else {
    if (is_connected_) {
      ESP_LOGI(TAG, "Disconnecting network ports.");
      is_connected_ = false;
    }

    if (udp_read_) {
      udp_read_.release();
    }
    if (udp_write_) {
      udp_write_.release();
    }
  }

  uint32_t now = millis();

  // Static targets are always active
  for (auto &target : udp_targets_static_) {
    udp_targets_[target] = now;
  }

  // Check for timeouts on targets
  std::erase_if(udp_targets_, [&](const auto &item) { return now - item.second > target_timeout_ms_; });

  // Poll sockets for incoming packets
  if (udp_read_ && udp_read_->ready()) {
    recv_local_socket(udp_read_, MODBUS40, READ_TOKEN);
  }

  if (udp_write_ && udp_write_->ready()) {
    recv_local_socket(udp_write_, MODBUS40, WRITE_TOKEN);
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
