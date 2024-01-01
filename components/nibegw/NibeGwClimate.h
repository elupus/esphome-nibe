#pragma once

#include <map>
#include <vector>
#include <cstddef>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace nibegw {

class NibeGwComponent;

class NibeGwClimate : public climate::Climate, public Component {
 public:
  void setup() override;
  void dump_config();
  void set_sensor(sensor::Sensor *sensor) {
    this->sensor_ = sensor;
  }
  void set_gw(NibeGwComponent *gw) {
    this->gw_ = gw;
  }
  void set_address(int address) {
    this->address_ = address;
    if (address_ >= 0x19 && address_ <= 0x1C) {
        this->index_ = address_ - 0x19;
    } else {
        this->index_ = 0;
    }
  }

 protected:
  /// Override control to change settings of the climate device.
  void control(const climate::ClimateCall &call) override;
  void publish_current(float value);
  void publish_target(float value);
  void restart_timeout();

  /// Return the traits of this controller.
  climate::ClimateTraits traits() override;

  NibeGwComponent *gw_{nullptr};
  sensor::Sensor *sensor_{nullptr};
  int address_;
  int index_;
  std::map<int, std::vector<uint8_t>> data_;
};

}  // namespace nibegw
}  // namespace esphome
