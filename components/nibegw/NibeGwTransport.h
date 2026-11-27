#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace esphome {
namespace nibegw {

using request_key_type = std::tuple<uint16_t, uint8_t>;
using request_data_type = std::vector<uint8_t>;
using request_provider_type = std::function<request_data_type(void)>;
using message_listener_type = std::function<void(const request_data_type &)>;
// Queues a request from a client, returns false if the request frame is invalid
using request_callback_type = std::function<bool(uint16_t address, uint8_t token, request_data_type request)>;

// Network side of the gateway. A transport forwards frames seen on RS485 to
// its clients and passes requests received from them to the request callback.
class NibeGwTransport {
 public:
  virtual ~NibeGwTransport() = default;

  void set_request_callback(request_callback_type callback) {
    request_callback_ = std::move(callback);
  }

  // Called from the component loop. connected tells if the network is up.
  virtual void loop(bool connected) = 0;

  // Forward a complete frame exchange received on RS485 to all clients.
  virtual void send_telegram(std::span<const uint8_t> telegram) = 0;

  virtual void dump_config() = 0;

 protected:
  request_callback_type request_callback_;
};

}  // namespace nibegw
}  // namespace esphome
