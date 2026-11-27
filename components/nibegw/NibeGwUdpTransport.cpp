#include "NibeGwUdpTransport.h"

#include <algorithm>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "NibeGw.h"

namespace esphome {
namespace nibegw {

static const char *const TAG = "nibegw";

socket_ptr_type bind_udp_socket(int port) {
  auto fd = socket::socket_ip_loop_monitored(SOCK_DGRAM, 0);
  if (fd) {
    // Set non-blocking
    fd->setblocking(false);

    socket_address address(port);

    if (fd->bind((sockaddr *) &address.storage, address.len) < 0) {
      ESP_LOGE(TAG, "Failed to bind socket to port %d, error: %d", port, errno);
      fd.reset();
    } else {
      ESP_LOGI(TAG, "UDP socket bound to port %d", port);
    }
  } else {
    ESP_LOGE(TAG, "Failed to create socket, error: %d", errno);
  }
  return fd;
}

void NibeGwUdpTransport::send_telegram(std::span<const uint8_t> telegram) {
  if (!is_connected_) {
    return;
  }

  /* always sending standard data from modbus read token */
  auto it = requests_sockets_.find(request_key_type(MODBUS40, READ_TOKEN));
  if (it == requests_sockets_.end() || !it->second.socket) {
    return;
  }
  auto &udp_read = it->second.socket;

  // Send to all UDP targets
  for (auto &&[target, timestamp] : udp_targets_) {
    int result = udp_read->sendto(telegram.data(), telegram.size(), 0, (sockaddr *) &target.storage, target.len);
    if (result < 0) {
      ESP_LOGW(TAG, "UDP sendto failed to %s, error: %d", target.str().c_str(), errno);
    }
  }
}

void NibeGwUdpTransport::recv_local_socket(socket_ptr_type &fd, int address, int token) {
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
      std::none_of(udp_sources_.begin(), udp_sources_.end(), [&](auto &source) { return from.matches(source); })) {
    ESP_LOGW(TAG, "UDP Packet wrong ip ignored %s", from.str().c_str());
    return;
  }

  if (!request_callback_(address, token, std::move(request))) {
    ESP_LOGW(TAG, "Received invalid packet from %s, %d bytes", from.str().c_str(), n);
    return;
  }

  /* store this as a new target */
  uint32_t now = millis();
  auto [it, inserted] = udp_targets_.insert_or_assign(from, now);
  if (inserted) {
    ESP_LOGI(TAG, "New target added %s", from.str().c_str());
  }
}

void NibeGwUdpTransport::run_request_socket(const request_key_type &key, request_socket_type &data) {
  if (!is_connected_) {
    if (data.socket) {
      ESP_LOGI(TAG, "UDP socket released for port %d", data.port);
      data.socket.reset();
    }
    return;
  }

  if (!data.socket) {
    data.socket = bind_udp_socket(data.port);
  }

  if (!data.socket) {
    return;
  }

  if (!data.socket->ready()) {
    return;
  }

  auto &[address, token] = key;
  recv_local_socket(data.socket, address, token);
}

void NibeGwUdpTransport::loop(bool connected) {
  if (connected != is_connected_) {
    ESP_LOGI(TAG, connected ? "Connecting network ports." : "Disconnecting network ports.");
    is_connected_ = connected;
  }

  uint32_t now = millis();

  // Static targets are always active
  for (auto &target : udp_targets_static_) {
    udp_targets_[target] = now;
  }

  // Check for timeouts on targets
  std::erase_if(udp_targets_, [&](const auto &item) { return now - item.second > TARGET_TIMEOUT_MS; });

  // Poll sockets for incoming packets
  for (auto &[key, data] : requests_sockets_) {
    run_request_socket(key, data);
  }
}

void NibeGwUdpTransport::dump_config() {
  ESP_LOGCONFIG(TAG, " Transport: udp");
  for (auto &&[address, timeout] : udp_targets_) {
    ESP_LOGCONFIG(TAG, " Target: %s", address.str().c_str());
  }
  for (auto &&address : udp_sources_) {
    ESP_LOGCONFIG(TAG, " Source: %s", address.str().c_str());
  }
  for (auto const &x : requests_sockets_) {
    ESP_LOGCONFIG(TAG, " Handler %x:%x Port: %d", std::get<0>(x.first), std::get<1>(x.first), x.second.port);
  }
}

}  // namespace nibegw
}  // namespace esphome
