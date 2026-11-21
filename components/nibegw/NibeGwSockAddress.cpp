

#include <cstring>
#include <string>
#include <sstream>
#include <cstdint>
#include "esphome/components/socket/socket.h"
#include "NibeGwSockAddress.h"

namespace esphome {

namespace nibegw {

static constexpr size_t SOCKPORT_STR_LEN = 5;

std::string socket_address::str() const {
  std::string buf;
  uint16_t port = 0;
  buf.resize(socket::SOCKADDR_STR_LEN + SOCKPORT_STR_LEN + 1);

  // format address
  auto l = socket::format_sockaddr_to(reinterpret_cast<const struct sockaddr *>(&storage), len, std::span<char, socket::SOCKADDR_STR_LEN>(buf.data(), socket::SOCKADDR_STR_LEN));
  if (l == 0) {
    return "";
  }
  buf.resize(l);

  // append port
  if (storage.ss_family == AF_INET) {
    const auto *addr = reinterpret_cast<const struct sockaddr_in *>(&storage);
    port = addr->sin_port;
#if USE_NETWORK_IPV6
  } else if (storage.ss_family == AF_INET6) {
    const auto *addr = reinterpret_cast<const struct sockaddr_in6 *>(&storage);
    port = addr->sin6_port;
#endif
  }
  if (port != 0) {
    buf += ':';
    buf += std::to_string(ntohs(port));
  }
  return buf;
}

}  // namespace nibegw
}  // namespace esphome
