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

#include "NibeGw.h"
#include "NibeGwTransport.h"

namespace esphome {
namespace nibegw {

class NibeGwComponent : public esphome::Component, public esphome::uart::UARTDevice {
  float get_setup_priority() const override {
    return setup_priority::PROCESSOR;
  }
  static constexpr const char *TAG = "nibegw";
  static constexpr size_t REQUESTS_QUEUE_MAX = 3;
  std::map<request_key_type, std::deque<request_data_type>> requests_;
  std::map<request_key_type, request_provider_type> requests_provider_;
  std::map<request_key_type, std::vector<message_listener_type>> message_listeners_;
  HighFrequencyLoopRequester high_freq_;

  NibeGw *gw_;
  NibeGwTransport *transport_{nullptr};

  void callback_msg_received(const uint8_t *data, int len);
  bool queue_client_request(uint16_t address, uint8_t token, request_data_type request);
  int callback_msg_token_received(uint16_t address, uint8_t command, uint8_t *data);

 public:
  void set_transport(NibeGwTransport *transport) {
    transport_ = transport;
    transport_->set_request_callback([this](uint16_t address, uint8_t token, request_data_type request) {
      return queue_client_request(address, token, std::move(request));
    });
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

  static constexpr size_t get_queue_capacity() {
    return REQUESTS_QUEUE_MAX;
  }

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
