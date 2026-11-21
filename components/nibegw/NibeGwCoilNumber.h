#pragma once

#include <string>

#include "esphome/core/component.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace nibegw {

class NibeGwComponent;
class NibeGwCoilPoller;

class NibeGwCoilNumber : public number::Number, public Component {
 public:
  void set_gw(NibeGwComponent *gw) { gw_ = gw; }
  void set_poller(NibeGwCoilPoller *poller) { poller_ = poller; }
  void set_address(uint16_t address) { address_ = address; }
  void set_coil_size(uint8_t size) { coil_size_ = size; }
  void set_factor(uint16_t factor) { factor_ = factor; }
  void set_poll_group(const std::string &group) { poll_group_ = group; }

  void setup() override;
  void loop() override;
  void dump_config() override;

  // Called by poller when write response (0x6C) arrives
  void on_write_response(bool success);

 protected:
  void control(float value) override;
  void queue_read_request();

  NibeGwComponent *gw_{nullptr};
  NibeGwCoilPoller *poller_{nullptr};
  uint16_t address_{0};
  uint8_t coil_size_{0};
  uint16_t factor_{1};
  std::string poll_group_{"default"};

  // Initial read flag
  bool needs_initial_read_{false};

  // Write state tracking
  bool write_pending_{false};
  float write_requested_value_{0};
  uint32_t write_sent_at_{0};
  static const uint32_t WRITE_TIMEOUT_MS = 10000;
};

}  // namespace nibegw
}  // namespace esphome
