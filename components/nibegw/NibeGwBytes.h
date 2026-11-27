#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace esphome {
namespace nibegw {

// Read a little endian integer from exactly sizeof(T) bytes
template<typename T> constexpr T get_le(std::span<const uint8_t, sizeof(T)> in) {
  T value = 0;
  for (size_t i = sizeof(T); i > 0; i--) {
    value = static_cast<T>((value << 8) | in[i - 1]);
  }
  return value;
}

// Write a little endian integer to exactly sizeof(T) bytes
template<typename T> constexpr void put_le(std::span<uint8_t, sizeof(T)> out, T value) {
  for (size_t i = 0; i < sizeof(T); i++) {
    out[i] = static_cast<uint8_t>(value);
    value = static_cast<T>(value >> 8);
  }
}

// Read a little endian integer at a fixed offset in a byte container. For
// fixed size containers the field is checked to be inside at compile time.
template<typename T, size_t Offset, typename Bytes> constexpr T get_le(const Bytes &in) {
  return get_le<T>(std::span(in).template subspan<Offset, sizeof(T)>());
}

// Write a little endian integer at a fixed offset in a byte container. The
// field size is taken from the type of value.
template<size_t Offset, typename T, typename Bytes> constexpr void put_le(Bytes &&out, T value) {
  put_le(std::span(out).template subspan<Offset, sizeof(T)>(), value);
}

}  // namespace nibegw
}  // namespace esphome
