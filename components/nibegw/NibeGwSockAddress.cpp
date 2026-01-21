

#include <cstring>
#include <string>
#include <sstream>
#include <cstdint>
#include "esphome/components/socket/socket.h"
#include "NibeGwSockAddress.h"

namespace esphome {

namespace nibegw {

// Platform-specific inet_ntop wrappers
#if defined(USE_SOCKET_IMPL_LWIP_SOCKETS)
// LWIP sockets (LibreTiny, ESP32 Arduino)
static inline const char *esphome_inet_ntop4(const void *addr, char *buf, size_t size) {
  return lwip_inet_ntop(AF_INET, addr, buf, size);
}
#if USE_NETWORK_IPV6
static inline const char *esphome_inet_ntop6(const void *addr, char *buf, size_t size) {
  return lwip_inet_ntop(AF_INET6, addr, buf, size);
}
#endif
#elif defined(USE_SOCKET_IMPL_BSD_SOCKETS)
// BSD sockets (host, ESP32-IDF)
static inline const char *esphome_inet_ntop4(const void *addr, char *buf, size_t size) {
  return inet_ntop(AF_INET, addr, buf, size);
}
#if USE_NETWORK_IPV6
static inline const char *esphome_inet_ntop6(const void *addr, char *buf, size_t size) {
  return inet_ntop(AF_INET6, addr, buf, size);
}
#endif
#else
#error Unsupported socket type
#endif

#if USE_NETWORK_IPV6
static constexpr size_t SOCKADDR_STR_LEN = 46;  // INET6_ADDRSTRLEN
#else
static constexpr size_t SOCKADDR_STR_LEN = 16;  // INET_ADDRSTRLEN
#endif
static constexpr size_t SOCKPORT_STR_LEN = 5;

std::string socket_address::str() const {
  std::string buf;
  uint16_t port;
  buf.resize(SOCKADDR_STR_LEN + SOCKPORT_STR_LEN + 1);

  if (storage.ss_family == AF_INET) {
    const auto *addr = reinterpret_cast<const struct sockaddr_in *>(&storage);
    if (esphome_inet_ntop4(&addr->sin_addr, buf.data(), buf.capacity()) != nullptr) {
      buf.resize(strlen(buf.data()));
      if (addr->sin_port != 0) {
        buf += ":" + std::to_string(ntohs(addr->sin_port));
      }
      return buf;
    }
  }
#if USE_NETWORK_IPV6
  else if (storage.ss_family == AF_INET6) {
    const auto *addr = reinterpret_cast<const struct sockaddr_in6 *>(&storage);
    if (esphome_inet_ntop6(&addr->sin6_addr, buf.data(), buf.capacity()) != nullptr)
      buf.resize(strlen(buf.data()));
    if (addr->sin_port != 0) {
      buf += ":" + std::to_string(ntohs(addr->sin_port));
    }
    return buf;
  }
#endif
  return "";
}

}  // namespace nibegw
}  // namespace esphome
