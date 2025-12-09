#pragma once

#include <cstring>
#include <string>
#include <sstream>
#include <cstdint>
#include "esphome/components/socket/socket.h"

namespace esphome {

namespace socket {
extern std::string format_sockaddr(const struct sockaddr_storage &storage);
}

namespace nibegw {

// Thin wrapper around sockaddr_storage + length. Supports IPv4 matching and
// ordering (operator<) for use in sorted containers.
struct socket_address {
  sockaddr_storage storage{};
  socklen_t len = sizeof(sockaddr_storage);

  socket_address() = default;

  socket_address(const sockaddr *addr, socklen_t addr_len) {
    if (addr && addr_len <= (socklen_t) sizeof(storage)) {
      std::memcpy(&storage, addr, addr_len);
      len = addr_len;
    } else {
      len = 0;
    }
  }

  socket_address(const network::IPAddress &ip, int port) {
    len = socket::set_sockaddr((sockaddr *) &storage, sizeof(storage), ip.str(), port);
  }

  socket_address(int port) {
    len = socket::set_sockaddr_any((sockaddr *) &storage, sizeof(storage), port);
  }

  std::string str() const {
    std::string s = socket::format_sockaddr(storage);
    if (storage.ss_family == AF_INET) {
      const auto *a = reinterpret_cast<const sockaddr_in *>(&storage);
      uint16_t port = ntohs(a->sin_port);
      if (port != 0) {
        s += ":" + std::to_string(port);
      }
    }
    return s;
  }

  bool valid() const {
    return len != 0;
  }

  // Match semantics similar to compare_sockaddr(): same family, same address
  // bytes, and if both ports are non-zero they must match. Only IPv4 is
  // supported for now.
  bool matches(const socket_address &other) const {
    if (!valid() || !other.valid())
      return false;

    if (storage.ss_family != other.storage.ss_family)
      return false;

    if (storage.ss_family == AF_INET) {
      const auto *a = reinterpret_cast<const sockaddr_in *>(&storage);
      const auto *b = reinterpret_cast<const sockaddr_in *>(&other.storage);

      if (a->sin_port != 0 && b->sin_port != 0 && a->sin_port != b->sin_port)
        return false;

      if (a->sin_addr.s_addr != b->sin_addr.s_addr)
        return false;
      return true;
    }

    // Other families not supported yet
    return false;
  }

  // Strict equality: family, address bytes, and port must all match.
  bool operator==(const socket_address &other) const {
    if (!valid() || !other.valid())
      return false;

    if (storage.ss_family != other.storage.ss_family)
      return false;

    if (storage.ss_family == AF_INET) {
      const auto *a = reinterpret_cast<const sockaddr_in *>(&storage);
      const auto *b = reinterpret_cast<const sockaddr_in *>(&other.storage);

      if (a->sin_port != b->sin_port)
        return false;

      if (a->sin_addr.s_addr != b->sin_addr.s_addr)
        return false;
      return true;
    }
    return false;
  }

  bool operator<(const socket_address &other) const {
    if (storage.ss_family != other.storage.ss_family)
      return storage.ss_family < other.storage.ss_family;

    if (storage.ss_family == AF_INET) {
      const auto *a = reinterpret_cast<const sockaddr_in *>(&storage);
      const auto *b = reinterpret_cast<const sockaddr_in *>(&other.storage);

      uint32_t aa = ntohl(a->sin_addr.s_addr);
      uint32_t bb = ntohl(b->sin_addr.s_addr);
      if (aa != bb)
        return aa < bb;

      uint16_t pa = ntohs(a->sin_port);
      uint16_t pb = ntohs(b->sin_port);
      if (pa != pb)
        return pa < pb;
      return false;
    }

    // Fallback: compare raw bytes
    int cmp = std::memcmp(&storage, &other.storage, std::min((size_t) len, (size_t) other.len));
    if (cmp != 0)
      return cmp < 0;
    return len < other.len;
  }
};

}  // namespace nibegw
}  // namespace esphome
