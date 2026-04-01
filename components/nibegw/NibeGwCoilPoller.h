#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "NibeGwComponent.h"

namespace esphome {
namespace nibegw {

class NibeGwCoilNumber;

enum CoilSize : uint8_t {
  COIL_SIZE_U8 = 0,
  COIL_SIZE_U16 = 1,
  COIL_SIZE_S16 = 2,
  COIL_SIZE_U32 = 3,
  COIL_SIZE_S32 = 4,
  COIL_SIZE_S8 = 5,
};

enum BufferMode : uint8_t {
  BUFFER_MODE_OFF = 0,
  BUFFER_MODE_LATEST_ONLY = 1,
  BUFFER_MODE_HISTORY = 2,
};

struct CoilRegistration {
  uint16_t address;
  CoilSize size;
  uint16_t factor;
  std::function<void(float)> callback;
};

struct BufferEntry {
  uint32_t timestamp;
  uint16_t address;
  float value;
};

struct PollGroup {
  std::string id;
  uint32_t interval_ms;
  std::vector<size_t> coil_indices;
  size_t poll_offset{0};
  uint32_t last_poll{0};
};

class NibeGwCoilPoller : public Component {
 public:
  void set_gw(NibeGwComponent *gw) { gw_ = gw; }
  void set_buffer_size(size_t bytes) { buffer_size_bytes_ = bytes; }
  void set_buffer_mode(uint8_t mode) { buffer_mode_ = static_cast<BufferMode>(mode); }
  void set_poll_interval(uint32_t ms) { default_poll_interval_ms_ = ms; }

  void add_poll_group(const std::string &id, uint32_t interval_ms);

  void register_coil(uint16_t address, CoilSize size, uint16_t factor,
                     const std::string &poll_group, std::function<void(float)> callback);

  // Register a number entity for write response dispatch
  void register_writable(NibeGwCoilNumber *number);

  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    return setup_priority::DATA;
  }

 protected:
  static float decode_coil_value(const uint8_t *data, CoilSize size, uint16_t factor);
  static uint8_t coil_data_bytes(CoilSize size);
  request_data_type build_read_request(PollGroup &group);
  void on_data_msg_received(const request_data_type &data);
  void on_read_response_received(const request_data_type &data);
  void on_write_response_received(const request_data_type &data);
  void buffer_value(uint16_t address, float value);
  void flush_buffer();
  bool is_mqtt_connected();

  static constexpr size_t BATCH_SIZE = 3;           // max coils per group per tick
  static constexpr size_t QUEUE_CAPACITY = NibeGwComponent::get_queue_capacity();

  NibeGwComponent *gw_{nullptr};
  uint32_t default_poll_interval_ms_{30000};
  size_t buffer_size_bytes_{4096};
  BufferMode buffer_mode_{BUFFER_MODE_LATEST_ONLY};

  std::vector<CoilRegistration> coils_;
  std::map<uint16_t, size_t> coil_index_;
  std::vector<PollGroup> poll_groups_;
  std::map<std::string, size_t> poll_group_index_;

  // Writable coil entities for write response dispatch
  std::vector<NibeGwCoilNumber *> writable_numbers_;

  // Buffer for offline storage
  std::vector<BufferEntry> history_buffer_;
  size_t history_head_{0};
  size_t history_count_{0};
  std::map<uint16_t, float> latest_buffer_;
  bool was_connected_{false};

  const char *TAG = "nibegw.poller";
};

}  // namespace nibegw
}  // namespace esphome
