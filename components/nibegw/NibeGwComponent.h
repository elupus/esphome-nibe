#pragma once

#include <set>
#include <deque>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/network/ip_address.h"
#include "esphome/components/network/util.h"
#include "esphome/components/socket/socket.h"

#include "NibeGw.h"
#include "NibeGwSockAddress.h"

namespace esphome {
namespace nibegw {

using request_key_type = std::tuple<uint16_t, uint8_t>;
using request_data_type = std::vector<uint8_t>;
using request_provider_type = std::function<request_data_type(void)>;
using message_listener_type = std::function<void(const request_data_type &)>;

#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 3, 0)
using socket_ptr_type = std::unique_ptr<socket::ListenSocket>;
#else
using socket_ptr_type = std::unique_ptr<socket::Socket>;
#endif

struct request_socket_type {
  int port;
  socket_ptr_type socket;
};

class NibeGwComponent : public esphome::Component, public esphome::uart::UARTDevice {
  float get_setup_priority() const override {
    return setup_priority::PROCESSOR;
  }
  static constexpr const char *TAG = "nibegw";
  static constexpr size_t REQUESTS_QUEUE_MAX = 3;
  static constexpr uint32_t TARGET_TIMEOUT_MS = 120000;
  bool is_connected_ = false;

  std::vector<socket_address> udp_sources_;
  std::map<socket_address, uint32_t> udp_targets_;
  std::map<request_key_type, std::deque<request_data_type>> requests_;
  std::map<request_key_type, request_provider_type> requests_provider_;
  std::map<request_key_type, request_socket_type> requests_sockets_;
  std::map<request_key_type, std::vector<message_listener_type>> message_listeners_;
  HighFrequencyLoopRequester high_freq_;

  NibeGw *gw_;

  void callback_msg_received(const uint8_t *data, int len);
  int callback_msg_token_received(uint16_t address, uint8_t command, uint8_t *data);

  void run_request_socket(const request_key_type &key, request_socket_type &data);
  void recv_local_socket(socket_ptr_type &fd, int address, int token);

  socket_ptr_type bind_local_socket(int port);

 public:
  void add_target(const network::IPAddress &ip, int port) {
    udp_targets_.insert_or_assign(socket_address(ip, port), 0);
  }

  void add_source_ip(const network::IPAddress &ip) {
    udp_sources_.push_back(socket_address(ip, 0));
  }

  void add_socket_request(int address, int token, int port) {
    auto &handler = requests_sockets_[request_key_type(address, token)];
    handler.port = port;
  }

  void set_request(int address, int token, request_data_type request) {
    set_request(address, token, [request] { return request; });
  }

  void set_request(int address, int token, request_provider_type provider) {
    requests_provider_[request_key_type(address, token)] = provider;
  }

  void add_listener(int address, int token, message_listener_type listener) {
    message_listeners_[request_key_type(address, token)].push_back(std::move(listener));
  }

  void add_queued_request(int address, int token, request_data_type request) {
    auto &queue = requests_[request_key_type(address, token)];
    if (queue.size() >= REQUESTS_QUEUE_MAX) {
      ESP_LOGV(TAG, "Request queue full for 0x%x:0x%x, dropping oldest", address, token);
      queue.pop_front();
    }
    queue.push_back(std::move(request));
  }

  // Priority request: inserted at front of queue (for write-verify reads)
  void add_priority_request(int address, int token, request_data_type request) {
    auto &queue = requests_[request_key_type(address, token)];
    if (queue.size() >= REQUESTS_QUEUE_MAX) {
      ESP_LOGV(TAG, "Request queue full for 0x%x:0x%x, dropping oldest", address, token);
      queue.pop_back();  // drop newest (least important) to make room
    }
    queue.push_front(std::move(request));
  }

  const std::deque<request_data_type> &get_request_queue(int address, int token) const {
    static const std::deque<request_data_type> empty;
    auto it = requests_.find(request_key_type(address, token));
    return (it != requests_.end()) ? it->second : empty;
  }

  static constexpr size_t get_queue_capacity() { return REQUESTS_QUEUE_MAX; }

  void add_acknowledge(int address) {
    gw_->setAcknowledge(address, true);
  }

  NibeGw &gw() {
    return *gw_;
  }

  NibeGwComponent(GPIOPin *dir_pin);

  void setup() override;
  void dump_config() override;
  void loop() override;
};

}  // namespace nibegw
}  // namespace esphome
