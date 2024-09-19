#include "esphome/core/log.h"

#include "NibeGwClimate.h"
#include "NibeGwComponent.h"
#include "NibeGw.h"

namespace esphome {
namespace nibegw {

static const char *const TAG = "nibegw";

const int int16_invalid = -0x8000;
const int int8_invalid = -0x80;
const int uint8_invalid = 0xFF;

enum RmuWriteIndex {
  RMU_WRITE_INDEX_TEMPORARY_LUX = 2,
  RMU_WRITE_INDEX_OPERATIONAL_MODE = 4,
  RMU_WRITE_INDEX_FUNCTIONS = 5,
  RMU_WRITE_INDEX_TEMPERATURE = 6,
  RMU_WRITE_INDEX_SETPOINT_S1 = 9,
  RMU_WRITE_INDEX_SETPOINT_S2 = 11,
  RMU_WRITE_INDEX_SETPOINT_S3 = 13,
  RMU_WRITE_INDEX_SETPOINT_S4 = 15,

  RMU_WRITE_INDEX_END = RMU_WRITE_INDEX_SETPOINT_S4 + 1
};

#define RMU_WRITE_INDEX_SETPOINT_SX(index) (RMU_WRITE_INDEX_SETPOINT_S1 + (index) * 2)

static const int RMU_DEVICE_VERSION = 0x0103;

enum RmuDataOffset {
  RMU_DATA_OFFSET_TARGET_TEMPERATURE_S1 = 4,
  RMU_DATA_OFFSET_TARGET_TEMPERATURE_S2 = 5,
  RMU_DATA_OFFSET_TARGET_TEMPERATURE_S3 = 6,
  RMU_DATA_OFFSET_TARGET_TEMPERATURE_S4 = 7,
  RMU_DATA_OFFSET_CURRENT_TEMPERATURE_SX = 8,
  RMU_DATA_OFFSET_FLAGS1 = 15,
  RMU_DATA_OFFSET_FLAGS0 = 16,
  RMU_DATA_OFFSET_MAX = 25,
};

#define RMU_DATA_OFFSET_TARGET_TEMPERATURE_SX(index) (RMU_DATA_OFFSET_TARGET_TEMPERATURE_S1 + (index))

enum RmuDataFlagsBits {
  RMU_DATA_FLAGS0_USE_ROOM_SENSOR_S4 = 0x80,
  RMU_DATA_FLAGS0_USE_ROOM_SENSOR_S3 = 0x40,
  RMU_DATA_FLAGS0_USE_ROOM_SENSOR_S2 = 0x20,
  RMU_DATA_FLAGS0_USE_ROOM_SENSOR_S1 = 0x10,
};

#define RMU_DATA_FLAGS0_USE_ROOM_SENSOR_SX(index) (RMU_DATA_FLAGS0_USE_ROOM_SENSOR_S1 << (index))

request_data_type build_request_data(byte token, request_data_type payload) {
  request_data_type data = {
      STARTBYTE_SLAVE,
      token,
      (byte) payload.size(),
  };

  for (auto &val : payload)
    data.push_back(val);

  byte checksum = 0;
  for (auto &val : data)
    checksum ^= val;
  data.push_back(checksum);
  return data;
}

request_data_type set_u16_index(int index, int value) {
  return {(byte) index, (byte) (value & 0xff), (byte) ((value >> 8) & 0xff)};
}

request_data_type set_u16(int value) {
  return {(byte) (value & 0xff), (byte) ((value >> 8) & 0xff)};
}

uint16_t get_u16(const byte data[2]) {
  return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

float get_s16_decimal(uint16_t data, float scale, int offset) {
  auto value = (int) (int16_t) data;
  float result;

  value += offset;
  if (value <= int16_invalid) {
    return NAN;
  }

  return value * scale;
}

float get_s16_decimal(const byte data[2], float scale, int offset) {
  return get_s16_decimal(get_u16(data), scale, offset);
}

request_data_type set_s16_decimal(float value, float scale, int offset) {
  int data;
  request_data_type result;
  if (isnan(value)) {
    data = int16_invalid;
  } else {
    data = (int) roundf(value / scale) - offset;
  }
  result = set_u16((uint16_t) (int16_t) data);
  return result;
}

float get_u8_decimal(const byte data[1], float scale, int offset) {
  int value = (int) data[0];
  value += offset;
  if (value >= uint8_invalid) {
    return NAN;
  }
  return value * scale;
}

request_data_type set_u8_decimal(float value, float scale, int offset) {
  int data;
  if (isnan(value)) {
    data = uint8_invalid;
  } else {
    data = (int) roundf(value / scale) - offset;
  }
  return {(uint8_t) data};
}

climate::ClimateTraits NibeGwClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(true);
  traits.set_supported_modes({climate::CLIMATE_MODE_HEAT_COOL, climate::CLIMATE_MODE_AUTO});
  traits.set_supports_two_point_target_temperature(false);
  traits.set_visual_min_temperature(5.0);
  traits.set_visual_max_temperature(30.5);
  traits.set_visual_temperature_step(0.5);
  traits.set_visual_current_temperature_step(0.1);
  return traits;
}

void NibeGwClimate::publish_current_temperature() {
  float value = this->sensor_->state;
  if (this->mode == climate::CLIMATE_MODE_AUTO) {
    value = NAN;
  }
  publish_current_temperature(value);
  restart_timeout_on_sensor();
}

void NibeGwClimate::publish_current_temperature(float value) {
  auto data = set_s16_decimal(value, 0.1, -7);
  data_[RMU_WRITE_INDEX_TEMPERATURE] = data;
  ESP_LOGI(TAG, "Publishing to rmu: 0x%x temp: %f -> %s", address_, value, format_hex_pretty(data).c_str());
}

void NibeGwClimate::publish_set_point(float value) {
  auto data = set_s16_decimal(value, 0.1, 0);
  data_[RMU_WRITE_INDEX_SETPOINT_SX(this->index_)] = data;
  ESP_LOGI(TAG, "Publishing to rmu: 0x%x target: %f -> %s", address_, value, format_hex_pretty(data).c_str());
}

void NibeGwClimate::dump_config() {
  ESP_LOGCONFIG(TAG, "NibeGw Climate");
  ESP_LOGCONFIG(TAG, " Address: 0x%x", address_);
  ESP_LOGCONFIG(TAG, " Sensor: %s", sensor_->get_name());
  dump_traits_(TAG);
}

void NibeGwClimate::restart_timeout_on_data() {
  this->set_timeout("data", 10 * 1000, [this]() {
    this->target_temperature = NAN;
    this->current_temperature = NAN;
    this->publish_state();
  });
}

void NibeGwClimate::restart_timeout_on_sensor() {
  this->set_timeout("sensor", 10 * 1000, [this]() { this->publish_current_temperature(); });
}

NibeGwClimate::data_index_map_t::iterator NibeGwClimate::next_data() {
  int index = data_index_;
  do {
    index++;
    if (index == RMU_WRITE_INDEX_END)
      index = 0;

    auto it = data_.find(index);
    if (it == data_.end()) {
      continue;
    }
    data_index_ = index;
    return it;

  } while (index != data_index_);

  return data_.end();
}

void NibeGwClimate::setup() {
  /* hook up current temperature sensor */
  this->sensor_->add_on_state_callback([this](float state) { this->publish_current_temperature(); });
  this->publish_current_temperature();

  /* restore set points */
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
  } else {
    /* restore from defaults */
    this->mode = climate::CLIMATE_MODE_AUTO;
  }

  /* setup response to write requests */
  this->gw_->set_request(address_, RMU_WRITE_TOKEN, [this] {
    auto it = this->next_data();
    if (it == data_.end()) {
      return request_data_type{};
    }

    ESP_LOGD(TAG, "Responding to rmu: 0x%x index: 0x%x data: %s", address_, it->first,
             format_hex_pretty(it->second).c_str());

    std::vector<uint8_t> payload;
    payload.push_back(it->first);
    for (auto &val : it->second)
      payload.push_back(val);
    data_.erase(it);

    return build_request_data(RMU_WRITE_TOKEN, payload);
  });

  /* setup response to accessory information */
  this->gw_->set_request(address_, ACCESSORY_TOKEN,
                         build_request_data(ACCESSORY_TOKEN, set_u16_index(0xEE, RMU_DEVICE_VERSION)));

  /* setup response to something odd we don't know, copied from nibepi */
  this->gw_->set_request(address_, RMU_DATA_TOKEN, build_request_data(RMU_WRITE_TOKEN, {0x63, 0x00}));

  this->data_index_ = 0;
  this->restart_timeout_on_data();

  this->gw_->add_listener(address_, RMU_DATA_MSG, [this](const request_data_type &message) {
    if (message.size() < RMU_DATA_OFFSET_MAX) {
      ESP_LOGW(TAG, "Invalid data length: %d", message.size());
      return;
    }

    if (message[RMU_DATA_OFFSET_FLAGS0] & RMU_DATA_FLAGS0_USE_ROOM_SENSOR_SX(this->index_)) {
      this->target_temperature = get_u8_decimal(&message[RMU_DATA_OFFSET_TARGET_TEMPERATURE_SX(this->index_)], 0.1, 50);
    } else {
      this->target_temperature = NAN;
    }

    /* this field has an added 0.5 degrees, to trigger rounding in RMU, subtract off before checking invalid values */
    this->current_temperature = get_s16_decimal(&message[RMU_DATA_OFFSET_CURRENT_TEMPERATURE_SX], 0.1, -5);

    this->restart_timeout_on_data();
    this->publish_state();
  });

  this->gw_->gw().setAcknowledge(address_, true);
}

void NibeGwClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    this->mode = *call.get_mode();
    this->publish_current_temperature();
  }

  if (call.get_target_temperature().has_value()) {
    auto target = *call.get_target_temperature();
    this->publish_set_point(target);
  }
}

}  // namespace nibegw
}  // namespace esphome