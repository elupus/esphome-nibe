#pragma once

#include <set>
#include <queue>
#include <vector>
#include <cstddef>
#include <map>

#include "esphome.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"

#include "NibeGw.h"

#ifdef USE_ESP32
#include <WiFi.h>
#include "AsyncUDP.h"
#endif

#ifdef USE_ESP8266
#include <ESP8266WiFi.h>
#include "ESPAsyncUDP.h"
#endif

namespace esphome {
namespace nibegw {

typedef std::tuple<byte, byte> request_key_type;
typedef std::vector<byte> request_data_type;
typedef std::function<request_data_type(void)> request_provider_type;
typedef std::tuple<network::IPAddress, int> target_type;
typedef std::function<void(const request_data_type &)> message_listener_type;

class NibeGwComponent : public esphome::Component, public esphome::uart::UARTDevice {
  float get_setup_priority() const override {
    return setup_priority::BEFORE_CONNECTION;
  }
  const char *TAG = "nibegw";
  const int requests_queue_max = 3;
  int udp_read_port_ = 9999;
  int udp_write_port_ = 10000;
  std::vector<network::IPAddress> udp_source_ip_;
  bool is_connected_ = false;

  std::vector<target_type> udp_targets_;
  std::map<request_key_type, std::queue<request_data_type>> requests_;
  std::map<request_key_type, request_provider_type> requests_provider_;
  std::map<request_key_type, message_listener_type> message_listener_;
  HighFrequencyLoopRequester high_freq_;

  NibeGw *gw_;

  AsyncUDP udp_read_;
  AsyncUDP udp_write_;

  void callback_msg_received(const byte *const data, int len);
  int callback_msg_token_received(eTokenType token, byte *data);
  void callback_debug(byte verbose, char *data);

  void token_request_cache(AsyncUDPPacket &udp, byte address, byte token);

 public:
  void set_read_port(int port) {
    udp_read_port_ = port;
  };
  void set_write_port(int port) {
    udp_write_port_ = port;
  };

  void add_target(const network::IPAddress &ip, int port) {
    auto target = target_type(ip, port);
    udp_targets_.push_back(target);
  }

  void add_source_ip(const network::IPAddress &ip) {
    udp_source_ip_.push_back(ip);
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
