#include "NibeGwCoilPoller.h"
#include "NibeGwCoilNumber.h"
#include "NibeGw.h"
#include "esphome/core/log.h"

#ifdef USE_MQTT
#include "esphome/components/mqtt/mqtt_client.h"
#endif

namespace esphome {
namespace nibegw {

void NibeGwCoilPoller::add_poll_group(const std::string &id, uint32_t interval_ms) {
  poll_groups_.push_back({id, interval_ms, {}, 0, 0});
  poll_group_index_[id] = poll_groups_.size() - 1;
}

void NibeGwCoilPoller::register_coil(uint16_t address, CoilSize size, uint16_t factor,
                                     const std::string &poll_group,
                                     std::function<void(float)> callback) {
  size_t coil_idx = coils_.size();
  coils_.push_back({address, size, factor, std::move(callback)});
  coil_index_[address] = coil_idx;

  auto it = poll_group_index_.find(poll_group);
  if (it == poll_group_index_.end()) {
    add_poll_group(poll_group, default_poll_interval_ms_);
    it = poll_group_index_.find(poll_group);
  }
  poll_groups_[it->second].coil_indices.push_back(coil_idx);
}

void NibeGwCoilPoller::register_writable(NibeGwCoilNumber *number) {
  writable_numbers_.push_back(number);
}

void NibeGwCoilPoller::setup() {
  if (buffer_mode_ == BUFFER_MODE_HISTORY && buffer_size_bytes_ > 0) {
    size_t capacity = buffer_size_bytes_ / sizeof(BufferEntry);
    if (capacity > 0) {
      history_buffer_.resize(capacity);
    }
  }

  // Listen on MODBUS_DATA_MSG (0x68) - pump's periodic broadcast with 2-byte values
  gw_->add_listener(MODBUS40, MODBUS_DATA_MSG,
                     [this](const request_data_type &data) { this->on_data_msg_received(data); });

  // Listen on MODBUS_READ_RESP (0x6A) - response to our read requests with 4-byte values
  gw_->add_listener(MODBUS40, MODBUS_READ_RESP,
                     [this](const request_data_type &data) { this->on_read_response_received(data); });

  // Listen on MODBUS_WRITE_RESP (0x6C) - write confirmation from pump
  gw_->add_listener(MODBUS40, MODBUS_WRITE_RESP,
                     [this](const request_data_type &data) { this->on_write_response_received(data); });

  ESP_LOGI(TAG, "Coil poller set up with %zu coils in %zu poll groups, %zu writable",
           coils_.size(), poll_groups_.size(), writable_numbers_.size());
}

void NibeGwCoilPoller::loop() {
  uint32_t now = millis();

  bool connected = is_mqtt_connected();
  if (connected && !was_connected_) {
    ESP_LOGI(TAG, "MQTT reconnected, flushing buffer");
    flush_buffer();
  }
  was_connected_ = connected;

  // Queue coil read requests in batches per group. Each group gets up to
  // BATCH_SIZE requests per tick, but only if there's space in the shared
  // queue. This prevents fast groups from starving slower groups.
  auto &queue = gw_->get_request_queue(MODBUS40, READ_TOKEN);
  for (auto &group : poll_groups_) {
    if (group.coil_indices.empty()) {
      continue;
    }

    if (now - group.last_poll >= group.interval_ms) {
      group.last_poll = now;
      size_t available = (queue.size() < QUEUE_CAPACITY) ? QUEUE_CAPACITY - queue.size() : 0;
      size_t batch = std::min({group.coil_indices.size(), (size_t) BATCH_SIZE, available});
      for (size_t i = 0; i < batch; i++) {
        auto request = build_read_request(group);
        if (!request.empty()) {
          gw_->add_queued_request(MODBUS40, READ_TOKEN, std::move(request));
        }
      }
    }
  }
}

void NibeGwCoilPoller::dump_config() {
  ESP_LOGCONFIG(TAG, "NibeGW Coil Poller:");
  ESP_LOGCONFIG(TAG, "  Buffer mode: %s",
                buffer_mode_ == BUFFER_MODE_OFF           ? "off"
                : buffer_mode_ == BUFFER_MODE_LATEST_ONLY ? "latest_only"
                                                          : "history");
  if (buffer_mode_ == BUFFER_MODE_HISTORY) {
    ESP_LOGCONFIG(TAG, "  Buffer size: %zu bytes (%zu entries)", buffer_size_bytes_,
                  history_buffer_.size());
  }
  ESP_LOGCONFIG(TAG, "  Total coils: %zu", coils_.size());
  for (auto &group : poll_groups_) {
    ESP_LOGCONFIG(TAG, "  Poll group '%s': interval %u ms, %zu coils",
                  group.id.c_str(), group.interval_ms, group.coil_indices.size());
  }
}

request_data_type NibeGwCoilPoller::build_read_request(PollGroup &group) {
  if (group.coil_indices.empty()) {
    return {};
  }

  // Send ONE coil address per request (Nibe protocol: MODBUS_READ_REQ = C0 69 02 addr_lo addr_hi checksum)
  size_t coil_idx = group.coil_indices[group.poll_offset];
  group.poll_offset = (group.poll_offset + 1) % group.coil_indices.size();

  uint16_t addr = coils_[coil_idx].address;
  ESP_LOGD(TAG, "Polling coil %u (group '%s')", addr, group.id.c_str());

  request_data_type packet;
  packet.push_back(STARTBYTE_SLAVE);
  packet.push_back(READ_TOKEN);
  packet.push_back(0x02);  // length: 2 bytes (one address)
  packet.push_back(addr & 0xFF);
  packet.push_back((addr >> 8) & 0xFF);

  uint8_t checksum = 0;
  for (auto b : packet) {
    checksum ^= b;
  }
  if (checksum == 0x5C) {
    checksum = 0xC5;
  }
  packet.push_back(checksum);

  return packet;
}

void NibeGwCoilPoller::on_data_msg_received(const request_data_type &data) {
  // MODBUS_DATA_MSG (0x68): pump broadcast with 4-byte entries [addr_lo addr_hi val_lo val_hi]
  ESP_LOGV(TAG, "Data broadcast received: %zu bytes", data.size());
  size_t offset = 0;
  while (offset + 4 <= data.size()) {
    uint16_t address = data[offset] | (data[offset + 1] << 8);
    offset += 2;

    if (address == 0xFFFF) {
      offset += 2;  // skip padding
      continue;
    }

    auto it = coil_index_.find(address);
    if (it == coil_index_.end()) {
      offset += 2;  // skip 2-byte value for unknown coil
      continue;
    }

    auto &coil = coils_[it->second];

    // Broadcast values are always 2 bytes
    if (offset + 2 > data.size()) {
      break;
    }

    float value = decode_coil_value(&data[offset], coil.size, coil.factor);
    offset += 2;

    ESP_LOGD(TAG, "Broadcast coil %u = %.2f", address, value);
    coil.callback(value);
    buffer_value(address, value);
  }
}

void NibeGwCoilPoller::on_read_response_received(const request_data_type &data) {
  // MODBUS_READ_RESP (0x6A): response to our read request
  // Format: [addr_lo addr_hi val_b0 val_b1 val_b2 val_b3] = 6 bytes
  ESP_LOGV(TAG, "Read response received: %zu bytes", data.size());
  if (data.size() < 6) {
    ESP_LOGW(TAG, "Read response too short: %zu bytes", data.size());
    return;
  }

  uint16_t address = data[0] | (data[1] << 8);

  auto it = coil_index_.find(address);
  if (it == coil_index_.end()) {
    ESP_LOGD(TAG, "Read response for unknown coil %u", address);
    return;
  }

  auto &coil = coils_[it->second];

  // Read response always has 4 value bytes - decode based on coil size
  float value = decode_coil_value(&data[2], coil.size, coil.factor);

  ESP_LOGD(TAG, "Read response coil %u = %.2f", address, value);
  coil.callback(value);
  buffer_value(address, value);
}

void NibeGwCoilPoller::on_write_response_received(const request_data_type &data) {
  // MODBUS_WRITE_RESP (0x6C): pump confirms or denies write
  // Format after dedup: [result_byte] where 0x01 = success, 0x00 = denied
  bool success = !data.empty() && data[0] != 0x00;
  ESP_LOGD(TAG, "Write response: %s", success ? "OK" : "DENIED");

  // Dispatch to all writable number entities with pending writes
  for (auto *number : writable_numbers_) {
    number->on_write_response(success);
  }
}

float NibeGwCoilPoller::decode_coil_value(const uint8_t *data, CoilSize size, uint16_t factor) {
  float raw;
  switch (size) {
    case COIL_SIZE_U8:
      raw = (float) data[0];
      break;
    case COIL_SIZE_S8:
      raw = (float) (int8_t) data[0];
      break;
    case COIL_SIZE_U16:
      raw = (float) (uint16_t)(data[0] | (data[1] << 8));
      break;
    case COIL_SIZE_S16: {
      int16_t val = (int16_t)(data[0] | (data[1] << 8));
      if (val == -32768) return NAN;  // 0x8000 = Nibe "no sensor" for s16
      raw = (float) val;
      break;
    }
    case COIL_SIZE_U32:
      raw = (float) ((uint32_t) data[0] | ((uint32_t) data[1] << 8) | ((uint32_t) data[2] << 16) |
                      ((uint32_t) data[3] << 24));
      break;
    case COIL_SIZE_S32:
      raw = (float) (int32_t)((uint32_t) data[0] | ((uint32_t) data[1] << 8) | ((uint32_t) data[2] << 16) |
                               ((uint32_t) data[3] << 24));
      break;
    default:
      return NAN;
  }
  if (factor > 1) {
    return raw / (float) factor;
  }
  return raw;
}

uint8_t NibeGwCoilPoller::coil_data_bytes(CoilSize size) {
  switch (size) {
    case COIL_SIZE_U8:
    case COIL_SIZE_S8:
      return 2;
    case COIL_SIZE_U16:
    case COIL_SIZE_S16:
      return 2;
    case COIL_SIZE_U32:
    case COIL_SIZE_S32:
      return 4;
    default:
      return 2;
  }
}

void NibeGwCoilPoller::buffer_value(uint16_t address, float value) {
  if (buffer_mode_ == BUFFER_MODE_OFF) {
    return;
  }

  if (buffer_mode_ == BUFFER_MODE_LATEST_ONLY) {
    latest_buffer_[address] = value;
    return;
  }

  if (history_buffer_.empty()) {
    return;
  }

  size_t capacity = history_buffer_.size();
  size_t write_idx = (history_head_ + history_count_) % capacity;
  history_buffer_[write_idx] = {millis(), address, value};
  if (history_count_ < capacity) {
    history_count_++;
  } else {
    history_head_ = (history_head_ + 1) % capacity;
  }
}

void NibeGwCoilPoller::flush_buffer() {
  if (buffer_mode_ == BUFFER_MODE_LATEST_ONLY) {
    for (auto &[address, value] : latest_buffer_) {
      auto it = coil_index_.find(address);
      if (it != coil_index_.end()) {
        coils_[it->second].callback(value);
      }
    }
    latest_buffer_.clear();
    return;
  }

  if (buffer_mode_ == BUFFER_MODE_HISTORY) {
    ESP_LOGI(TAG, "Flushing %zu buffered entries", history_count_);
    for (size_t i = 0; i < history_count_; i++) {
      size_t idx = (history_head_ + i) % history_buffer_.size();
      auto &entry = history_buffer_[idx];
      auto it = coil_index_.find(entry.address);
      if (it != coil_index_.end()) {
        coils_[it->second].callback(entry.value);
      }
    }
    history_count_ = 0;
    history_head_ = 0;
    return;
  }
}

bool NibeGwCoilPoller::is_mqtt_connected() {
#ifdef USE_MQTT
  if (mqtt::global_mqtt_client != nullptr) {
    return mqtt::global_mqtt_client->is_connected();
  }
#endif
  return true;
}

}  // namespace nibegw
}  // namespace esphome
