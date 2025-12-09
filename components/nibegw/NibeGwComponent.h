#pragma once

#include <set>
#include <queue>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <map>

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/network/ip_address.h"
#include "esphome/components/network/util.h"
#include "esphome/components/socket/socket.h"

#include "NibeGw.h"
#include "NibeGwSockAddress.h"

namespace esphome {
namespace nibegw {

using namespace std;

typedef std::tuple<uint16_t, uint8_t> request_key_type;
typedef std::vector<uint8_t> request_data_type;
typedef std::function<request_data_type(void)> request_provider_type;
typedef std::function<void(const request_data_type &)> message_listener_type;

class NibeGwComponent : public esphome::Component, public esphome::uart::UARTDevice {
  float get_setup_priority() const override {
    return setup_priority::PROCESSOR;
  }
  const char *TAG = "nibegw";
  const int requests_queue_max = 3;
  int udp_read_port_ = 9999;
  int udp_write_port_ = 10000;
  bool is_connected_ = false;

  std::vector<socket_address> udp_sources_;
  std::vector<socket_address> udp_targets_;
  std::map<request_key_type, std::queue<request_data_type>> requests_;
  std::map<request_key_type, request_provider_type> requests_provider_;
  std::map<request_key_type, message_listener_type> message_listener_;
  HighFrequencyLoopRequester high_freq_;

  NibeGw *gw_;

  std::unique_ptr<socket::Socket> udp_read_;
  std::unique_ptr<socket::Socket> udp_write_;

  void callback_msg_received(const uint8_t *data, int len);
  int callback_msg_token_received(uint16_t address, uint8_t command, uint8_t *data);
  void callback_debug(uint8_t verbose, char *data);

  void recv_local_socket(std::unique_ptr<socket::Socket> &fd, int address, int token);

  std::unique_ptr<socket::Socket> bind_local_socket(int port);

 public:
  void set_read_port(int port) {
    udp_read_port_ = port;
  };
  void set_write_port(int port) {
    udp_write_port_ = port;
  };

  void add_target(const network::IPAddress &ip, int port) {
    udp_targets_.push_back(socket_address(ip, port));
  }

  void add_source_ip(const network::IPAddress &ip) {
    udp_sources_.push_back(socket_address(ip, 0));
  };

  void set_request(int address, int token, request_data_type request) {
    set_request(address, token, [request] { return request; });
  }

  void set_request(int address, int token, request_provider_type provider) {
    requests_provider_[request_key_type(address, token)] = provider;
  }

  void add_listener(int address, int token, message_listener_type listener) {
    message_listener_[request_key_type(address, token)] = listener;
  }

  void add_queued_request(int address, int token, request_data_type request) {
    auto &queue = requests_[request_key_type(address, token)];
    if (queue.size() >= requests_queue_max) {
      queue.pop();
    }
    queue.push(std::move(request));
  }

  NibeGw &gw() {
    return *gw_;
  }

  NibeGwComponent(GPIOPin *dir_pin);

  void setup();
  void dump_config();
  void loop();
};

}  // namespace nibegw
}  // namespace esphome