#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "esphome/core/version.h"
#include "esphome/components/network/ip_address.h"
#include "esphome/components/socket/socket.h"

#include "NibeGwTransport.h"
#include "NibeGwSockAddress.h"

namespace esphome {
namespace nibegw {

#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 3, 0)
using socket_ptr_type = std::unique_ptr<socket::ListenSocket>;
#else
using socket_ptr_type = std::unique_ptr<socket::Socket>;
#endif

socket_ptr_type bind_udp_socket(int port);

// Plaintext UDP transport: one port per (address, token) request queue,
// telegrams are sent from the MODBUS40 read port to all known targets.
class NibeGwUdpTransport : public NibeGwTransport {
 public:
  void add_target(const network::IPAddress &ip, int port) {
    udp_targets_static_.push_back(socket_address(ip, port));
  }

  void add_source_ip(const network::IPAddress &ip) {
    udp_sources_.push_back(socket_address(ip, 0));
  }

  void add_socket_request(int address, int token, int port) {
    auto &handler = requests_sockets_[request_key_type(address, token)];
    handler.port = port;
  }

  void loop(bool connected) override;
  void send_telegram(std::span<const uint8_t> telegram) override;
  void dump_config() override;

 protected:
  static constexpr uint32_t TARGET_TIMEOUT_MS = 120000;

  struct request_socket_type {
    int port;
    socket_ptr_type socket;
  };

  void run_request_socket(const request_key_type &key, request_socket_type &data);
  void recv_local_socket(socket_ptr_type &fd, int address, int token);

  bool is_connected_ = false;
  std::vector<socket_address> udp_sources_;
  std::vector<socket_address> udp_targets_static_;
  std::map<socket_address, uint32_t> udp_targets_;
  std::map<request_key_type, request_socket_type> requests_sockets_;
};

}  // namespace nibegw
}  // namespace esphome
