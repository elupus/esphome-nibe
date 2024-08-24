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
  void set_system(int system) {
    this->index_ = system - 1;
    this->address_ = 0x19 + this->index_;
  }

 protected:
  /// Override control to change settings of the climate device.
  void control(const climate::ClimateCall &call) override;
  void publish_current_temperature();
  void publish_current_temperature(float value);
  void publish_set_point(float value);
  void restart_timeout_on_data();
  void restart_timeout_on_sensor();

  typedef std::map<int, std::vector<uint8_t>> data_index_map_t;

  data_index_map_t::iterator next_data(); /* return next data index */

  /// Return the traits of this controller.
  climate::ClimateTraits traits() override;

  NibeGwComponent *gw_{nullptr};
  sensor::Sensor *sensor_{nullptr};
  int address_;
  int index_;
  int data_index_; /* last transmitted data index */
  data_index_map_t data_;
};

}  // namespace nibegw
}  // namespace esphome
