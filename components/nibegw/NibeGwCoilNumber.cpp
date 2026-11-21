#include "NibeGwCoilNumber.h"
#include "NibeGwCoilPoller.h"
#include "NibeGwComponent.h"
#include "NibeGw.h"
#include "esphome/core/log.h"

namespace esphome {
namespace nibegw {

static const char *TAG = "nibegw.number";

void NibeGwCoilNumber::setup() {
  if (poller_ == nullptr || gw_ == nullptr) {
    ESP_LOGE(TAG, "Poller or gateway not set for number at address %u", address_);
    this->mark_failed();
    return;
  }
  // Register as listener to receive current value from pump (via read polling)
  poller_->register_coil(address_, static_cast<CoilSize>(coil_size_), factor_, poll_group_,
                         [this](float value) { this->publish_state(value); });

  // Flag to read initial value on first loop iteration
  needs_initial_read_ = true;
}

void NibeGwCoilNumber::loop() {
  // Queue initial read once gateway is connected
  if (needs_initial_read_ && millis() > 5000) {
    needs_initial_read_ = false;
    ESP_LOGD(TAG, "Queuing initial read for coil %u", address_);
    queue_read_request();
  }

  // Check for write timeout
  if (write_pending_ && (millis() - write_sent_at_ > WRITE_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "Write timeout for coil %u", address_);
    write_pending_ = false;
  }
}

void NibeGwCoilNumber::dump_config() {
  LOG_NUMBER("", "NibeGW Coil Number", this);
  ESP_LOGCONFIG(TAG, "  Address: %u", address_);
  ESP_LOGCONFIG(TAG, "  Size: %u", coil_size_);
  ESP_LOGCONFIG(TAG, "  Factor: %u", factor_);
}

void NibeGwCoilNumber::control(float value) {
  // Encode the value as raw integer
  int32_t raw;
  if (factor_ > 1) {
    raw = (int32_t) roundf(value * (float) factor_);
  } else {
    raw = (int32_t) roundf(value);
  }

  // Build payload: address (2 bytes LE) + value (ALWAYS 4 bytes LE per Nibe protocol)
  // Format: C0 6B 06 [addr_lo addr_hi val0 val1 val2 val3] checksum
  std::vector<uint8_t> payload;
  payload.push_back(address_ & 0xFF);
  payload.push_back((address_ >> 8) & 0xFF);
  payload.push_back(raw & 0xFF);
  payload.push_back((raw >> 8) & 0xFF);
  payload.push_back((raw >> 16) & 0xFF);
  payload.push_back((raw >> 24) & 0xFF);

  // Build the packet: start, cmd, len, payload, checksum
  request_data_type packet;
  packet.push_back(STARTBYTE_SLAVE);
  packet.push_back(WRITE_TOKEN);
  packet.push_back(payload.size());  // always 6 (2 addr + 4 value)
  for (auto b : payload) {
    packet.push_back(b);
  }

  uint8_t checksum = 0;
  for (auto b : packet) {
    checksum ^= b;
  }
  if (checksum == 0x5C) {
    checksum = 0xC5;
  }
  packet.push_back(checksum);

  write_pending_ = true;
  write_requested_value_ = value;
  write_sent_at_ = millis();

  ESP_LOGI(TAG, "Writing coil %u = %.2f (raw: %d)", address_, value, (int) raw);
  gw_->add_queued_request(MODBUS40, WRITE_TOKEN, std::move(packet));
}

void NibeGwCoilNumber::on_write_response(bool success) {
  if (!write_pending_) {
    return;
  }
  write_pending_ = false;

  if (success) {
    ESP_LOGI(TAG, "Write confirmed for coil %u = %.2f", address_, write_requested_value_);
    // Queue a read request to verify the value from the pump
    queue_read_request();
  } else {
    ESP_LOGW(TAG, "Write DENIED by pump for coil %u", address_);
  }
}

void NibeGwCoilNumber::queue_read_request() {
  // Build a read request to verify the written value
  // Format: C0 69 02 [addr_lo addr_hi] checksum
  request_data_type packet;
  packet.push_back(STARTBYTE_SLAVE);
  packet.push_back(READ_TOKEN);
  packet.push_back(0x02);
  packet.push_back(address_ & 0xFF);
  packet.push_back((address_ >> 8) & 0xFF);

  uint8_t checksum = 0;
  for (auto b : packet) {
    checksum ^= b;
  }
  if (checksum == 0x5C) {
    checksum = 0xC5;
  }
  packet.push_back(checksum);

  // Use priority request so verify-reads jump ahead of regular polling
  gw_->add_priority_request(MODBUS40, READ_TOKEN, std::move(packet));
}

}  // namespace nibegw
}  // namespace esphome
