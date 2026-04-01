#pragma once

#include <string>

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace nibegw {

class NibeGwCoilPoller;

class NibeGwCoilSensor : public sensor::Sensor, public Component {
 public:
  void set_poller(NibeGwCoilPoller *poller) { poller_ = poller; }
  void set_address(uint16_t address) { address_ = address; }
  void set_coil_size(uint8_t size) { coil_size_ = size; }
  void set_factor(uint16_t factor) { factor_ = factor; }
  void set_poll_group(const std::string &group) { poll_group_ = group; }

  void setup() override;
  void dump_config() override;

 protected:
  NibeGwCoilPoller *poller_{nullptr};
  uint16_t address_{0};
  uint8_t coil_size_{0};
  uint16_t factor_{1};
  std::string poll_group_{"default"};
};

}  // namespace nibegw
}  // namespace esphome
