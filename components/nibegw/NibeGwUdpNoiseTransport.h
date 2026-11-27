#pragma once

#include "esphome/core/defines.h"

#ifdef USE_NOISE

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <span>

#include "esphome/components/noise/noise.h"

#include "NibeGwTransport.h"
#include "NibeGwSockAddress.h"
#include "NibeGwUdpTransport.h"

namespace esphome {
namespace nibegw {

/* Encrypted UDP transport on a single port, modelled on the WireGuard data plane.
 *
 * Sessions are initiated by the client with a Noise_NNpsk0_25519_ChaChaPoly_SHA256
 * handshake (as the ESPHome native API) using a pre-shared key. All integers are
 * little endian, every message starts with its type as a u32 (a type byte and
 * three zero bytes, as in WireGuard).
 *
 *   1 HANDSHAKE_INITIATION  type | sender_index | noise message 1 (48 bytes)
 *   2 HANDSHAKE_RESPONSE    type | sender_index | receiver_index | noise message 2 (48 bytes)
 *   4 TRANSPORT_DATA        type | receiver_index | counter (u64) | ciphertext + tag
 *
 * Transport data uses ChaCha20-Poly1305 (IETF) with nonce = 4 zero bytes | counter
 * and no associated data. Each direction uses a key derived from the matching Noise
 * transport key k as ENCRYPT(k, 2^64 - 2, "", zeros[32])[0:32]. Counters must
 * increase, anything else is dropped.
 *
 * The plaintext is empty for a keepalive, which the gateway echoes. Otherwise it is
 * a request (address u16 | token | frame) from the client, or a telegram (frame as
 * received on RS485) from the gateway.
 *
 * A session receives telegrams only after the client has proven its keys with
 * transport data. Confirming a session drops older sessions from the same address.
 */
class NibeGwUdpNoiseTransport : public NibeGwTransport {
 public:
  void set_psk(const uint8_t *psk) {
    ctx_.set_psk(psk);
  }
  void set_port(uint16_t port) {
    port_ = port;
  }
  void add_allowed_request(int address, int token) {
    allowed_requests_.insert(request_key_type(address, token));
  }

  void loop(bool connected) override;
  void send_telegram(std::span<const uint8_t> telegram) override;
  void dump_config() override;

  static constexpr uint32_t TYPE_HANDSHAKE_INITIATION = 1;
  static constexpr uint32_t TYPE_HANDSHAKE_RESPONSE = 2;
  static constexpr uint32_t TYPE_TRANSPORT_DATA = 4;

  static constexpr size_t KEY_SIZE = 32;
  static constexpr size_t MAC_SIZE = 16;
  static constexpr size_t NOISE_MESSAGE_SIZE = 32 + MAC_SIZE;
  static constexpr size_t INITIATION_SIZE = 8 + NOISE_MESSAGE_SIZE;
  static constexpr size_t RESPONSE_SIZE = 12 + NOISE_MESSAGE_SIZE;
  static constexpr size_t DATA_HEADER_SIZE = 16;
  static constexpr size_t MAX_PACKET_SIZE = 512;

  static constexpr size_t MAX_SESSIONS = 4;
  static constexpr uint32_t SESSION_TIMEOUT_MS = 120000;
  static constexpr uint32_t PENDING_TIMEOUT_MS = 5000;
  // Limits the curve25519 work so RS485 timing is not disturbed
  static constexpr uint32_t HANDSHAKE_INTERVAL_MS = 200;

 protected:
  struct Session {
    std::array<uint8_t, KEY_SIZE> tx_key{};  // gateway -> client
    std::array<uint8_t, KEY_SIZE> rx_key{};  // client -> gateway
    uint32_t remote_index{0};
    socket_address endpoint;
    uint32_t last_rx{0};
    uint64_t tx_counter{0};
    uint64_t rx_counter{0};  // next acceptable counter
  };
  using SessionMap = std::map<uint32_t, Session>;  // keyed by our (receiver) index

  void receive_packet();
  void handle_initiation(const socket_address &from, std::span<uint8_t> packet);
  void handle_data(const socket_address &from, std::span<const uint8_t> packet);
  void handle_request(const socket_address &from, std::span<const uint8_t> request);
  uint32_t new_session_index() const;
  Session *find_session(uint32_t index);
  Session &activate_session(uint32_t index);
  void send_data(Session &session, std::span<const uint8_t> data);
  void send_to(const socket_address &to, std::span<const uint8_t> data);
  static void limit_sessions(SessionMap &sessions, size_t max, uint32_t now);

  noise::NoiseContext ctx_;
  uint16_t port_{9999};
  std::set<request_key_type> allowed_requests_;

  bool is_connected_{false};
  socket_ptr_type socket_;
  SessionMap pending_;  // handshake done, waiting for the client to confirm
  SessionMap active_;   // confirmed, receive telegrams
  uint32_t last_handshake_{0};
};

}  // namespace nibegw
}  // namespace esphome

#endif  // USE_NOISE
