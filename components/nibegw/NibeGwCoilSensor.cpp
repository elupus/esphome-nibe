#include "NibeGwCoilSensor.h"
#include "NibeGwCoilPoller.h"
#include "esphome/core/log.h"

namespace esphome {
namespace nibegw {

static const char *TAG = "nibegw.sensor";

void NibeGwCoilSensor::setup() {
  if (poller_ == nullptr) {
    ESP_LOGE(TAG, "Poller not set for sensor at address %u", address_);
    this->mark_failed();
    return;
  }
  poller_->register_coil(address_, static_cast<CoilSize>(coil_size_), factor_, poll_group_,
                         [this](float value) { this->publish_state(value); });
}

void NibeGwCoilSensor::dump_config() {
  LOG_SENSOR("", "NibeGW Coil Sensor", this);
  ESP_LOGCONFIG(TAG, "  Address: %u", address_);
  ESP_LOGCONFIG(TAG, "  Size: %u", coil_size_);
  ESP_LOGCONFIG(TAG, "  Factor: %u", factor_);
}

}  // namespace nibegw
}  // namespace esphome
