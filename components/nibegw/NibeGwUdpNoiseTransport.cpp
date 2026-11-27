#include "NibeGwUdpNoiseTransport.h"

#ifdef USE_NOISE

#include <algorithm>
#include <memory>

#include <noise/protocol.h>
#include <sodium.h>

#include "esphome/components/noise/noise_handshake.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "NibeGwBytes.h"

namespace esphome {
namespace nibegw {

static const char *const TAG = "nibegw.noise";

static constexpr char PROLOGUE[] = "nibegw-udp-noise-v1";
static constexpr uint64_t KEY_DERIVE_NONCE = 0xFFFFFFFFFFFFFFFEULL;

using Transport = NibeGwUdpNoiseTransport;

static_assert(crypto_aead_chacha20poly1305_IETF_ABYTES == Transport::MAC_SIZE, "mac size");
static_assert(crypto_aead_chacha20poly1305_IETF_KEYBYTES == Transport::KEY_SIZE, "key size");

using Key = std::array<uint8_t, Transport::KEY_SIZE>;
using Nonce = std::array<uint8_t, crypto_aead_chacha20poly1305_IETF_NPUBBYTES>;

// Transport data nonce: 4 zero bytes followed by the counter
static Nonce counter_nonce(uint64_t counter) {
  Nonce nonce{};
  put_le<4>(nonce, counter);
  return nonce;
}

struct CipherStateDeleter {
  void operator()(NoiseCipherState *cipher) const {
    noise_cipherstate_free(cipher);
  }
};
using CipherStatePtr = std::unique_ptr<NoiseCipherState, CipherStateDeleter>;

// k' = ENCRYPT(k, 2^64-2, "", zeros[32])[0:32], since noise-c does not expose k.
// The cipher state is used up by this and must be discarded afterwards.
static bool derive_key(NoiseCipherState *cipher, Key &out) {
  std::array<uint8_t, Transport::KEY_SIZE + Transport::MAC_SIZE> buf{};
  NoiseBuffer mbuf;
  noise_buffer_init(mbuf);
  noise_buffer_set_inout(mbuf, buf.data(), out.size(), buf.size());

  if (noise_cipherstate_set_nonce(cipher, KEY_DERIVE_NONCE) != NOISE_ERROR_NONE) {
    return false;
  }
  if (noise_cipherstate_encrypt(cipher, &mbuf) != NOISE_ERROR_NONE) {
    return false;
  }

  std::copy_n(buf.begin(), out.size(), out.begin());
  return true;
}

// Encrypt into a transport data packet in buf. On success buf is shrunk to the
// packet, false if it does not fit.
static bool encrypt_data(const Key &key, uint32_t receiver_index, uint64_t counter, std::span<const uint8_t> data,
                         std::span<uint8_t> &buf) {
  auto packet_len = Transport::DATA_HEADER_SIZE + data.size() + Transport::MAC_SIZE;
  if (packet_len > buf.size()) {
    return false;
  }

  buf = buf.first(packet_len);
  put_le<0>(buf, Transport::TYPE_TRANSPORT_DATA);
  put_le<4>(buf, receiver_index);
  put_le<8>(buf, counter);

  auto ciphertext = buf.subspan(Transport::DATA_HEADER_SIZE);
  auto nonce = counter_nonce(counter);
  crypto_aead_chacha20poly1305_ietf_encrypt(ciphertext.data(), nullptr, data.data(), data.size(), nullptr, 0, nullptr,
                                            nonce.data(), key.data());
  return true;
}

// Decrypt the ciphertext of a transport data packet into buf. On success buf is
// shrunk to the plaintext, false if it does not authenticate or fit.
static bool decrypt_data(const Key &key, uint64_t counter, std::span<const uint8_t> ciphertext,
                         std::span<uint8_t> &buf) {
  if (ciphertext.size() < Transport::MAC_SIZE) {
    return false;
  }
  if (ciphertext.size() - Transport::MAC_SIZE > buf.size()) {
    return false;
  }

  auto nonce = counter_nonce(counter);
  unsigned long long plain_len = 0;
  auto err = crypto_aead_chacha20poly1305_ietf_decrypt(buf.data(), &plain_len, nullptr, ciphertext.data(),
                                                       ciphertext.size(), nullptr, 0, nonce.data(), key.data());
  if (err != 0) {
    return false;
  }

  buf = buf.first(plain_len);
  return true;
}

// Fill in the header of a handshake response, the noise message follows it
static void build_response(std::span<uint8_t, Transport::RESPONSE_SIZE> out, uint32_t sender_index,
                           uint32_t receiver_index) {
  put_le<0>(out, Transport::TYPE_HANDSHAKE_RESPONSE);
  put_le<4>(out, sender_index);
  put_le<8>(out, receiver_index);
}

// Drop the least recently active sessions until at most max remain
void Transport::limit_sessions(SessionMap &sessions, size_t max, uint32_t now) {
  // Compare idle times rather than timestamps, so it works across a millis() wrap
  auto idle = [now](const auto &item) { return now - item.second.last_rx; };
  auto more_idle = [&](const auto &a, const auto &b) { return idle(a) > idle(b); };

  while (sessions.size() > max) {
    auto oldest = std::min_element(sessions.begin(), sessions.end(), more_idle);
    ESP_LOGD(TAG, "Dropping session with %s", oldest->second.endpoint.str().c_str());
    sessions.erase(oldest);
  }
}

void Transport::handle_initiation(const socket_address &from, std::span<uint8_t> packet) {
  if (packet.size() != INITIATION_SIZE) {
    return;
  }

  auto failed = [&](int err) {
    if (err != 0) {
      auto reason = LOG_STR_ARG(noise::noise_err_to_logstr(err));
      ESP_LOGW(TAG, "Handshake with %s failed: %s", from.str().c_str(), reason);
    }
    return err != 0;
  };

  noise::NoiseResponderHandshake handshake;
  auto prologue = reinterpret_cast<const uint8_t *>(PROLOGUE);
  if (failed(handshake.init(ctx_, prologue, sizeof(PROLOGUE) - 1))) {
    return;
  }

  // Authenticates the client against the pre-shared key; cheap, no DH involved
  auto message = packet.subspan<8, NOISE_MESSAGE_SIZE>();
  if (failed(handshake.read_message(message.data(), message.size()))) {
    return;
  }

  auto now = millis();
  if (now - last_handshake_ < HANDSHAKE_INTERVAL_MS) {
    ESP_LOGW(TAG, "Handshake from %s dropped: too frequent", from.str().c_str());
    return;
  }
  last_handshake_ = now;

  std::array<uint8_t, RESPONSE_SIZE> out;
  auto reply = std::span(out).subspan<12, NOISE_MESSAGE_SIZE>();
  size_t reply_len = 0;
  if (failed(handshake.write_message(reply.data(), reply.size(), reply_len))) {
    return;
  }

  NoiseCipherState *send_cipher = nullptr;
  NoiseCipherState *recv_cipher = nullptr;
  if (failed(handshake.split(send_cipher, recv_cipher))) {
    return;
  }
  CipherStatePtr send_owner(send_cipher);
  CipherStatePtr recv_owner(recv_cipher);

  Session session;
  if (!derive_key(send_cipher, session.tx_key)) {
    failed(NOISE_ERROR_INVALID_STATE);
    return;
  }
  if (!derive_key(recv_cipher, session.rx_key)) {
    failed(NOISE_ERROR_INVALID_STATE);
    return;
  }

  auto index = new_session_index();
  session.remote_index = get_le<uint32_t, 4>(packet);
  session.endpoint = from;
  session.last_rx = now;
  pending_[index] = session;

  // The new session is the most recently active one, so it is never dropped
  limit_sessions(pending_, MAX_SESSIONS, now);

  build_response(out, index, session.remote_index);
  send_to(from, out);
  ESP_LOGD(TAG, "Handshake with %s, awaiting confirmation", from.str().c_str());
}

uint32_t Transport::new_session_index() const {
  uint32_t index;
  do {
    index = random_uint32();
  } while (pending_.count(index) || active_.count(index));
  return index;
}

Transport::Session *Transport::find_session(uint32_t index) {
  for (auto *sessions : {&active_, &pending_}) {
    auto it = sessions->find(index);
    if (it != sessions->end()) {
      return &it->second;
    }
  }
  return nullptr;
}

Transport::Session &Transport::activate_session(uint32_t index) {
  auto &session = active_.insert(pending_.extract(index)).position->second;
  ESP_LOGI(TAG, "Session established with %s", session.endpoint.str().c_str());

  // A reconnecting client replaces its previous session
  std::erase_if(active_, [&](const auto &item) {
    const auto &other = item.second;
    return &other != &session && other.endpoint == session.endpoint;
  });

  // The new session is the most recently active one, so it is never dropped
  limit_sessions(active_, MAX_SESSIONS, millis());
  return session;
}

void Transport::send_to(const socket_address &to, std::span<const uint8_t> data) {
  auto address = reinterpret_cast<const sockaddr *>(&to.storage);
  if (socket_->sendto(data.data(), data.size(), 0, address, to.len) < 0) {
    ESP_LOGW(TAG, "UDP sendto failed to %s, error: %d", to.str().c_str(), errno);
  }
}

void Transport::send_data(Session &session, std::span<const uint8_t> data) {
  std::array<uint8_t, MAX_PACKET_SIZE> buf;
  std::span<uint8_t> packet = buf;
  if (!encrypt_data(session.tx_key, session.remote_index, session.tx_counter, data, packet)) {
    return;
  }

  session.tx_counter++;
  send_to(session.endpoint, packet);
}

void Transport::handle_data(const socket_address &from, std::span<const uint8_t> packet) {
  if (packet.size() < DATA_HEADER_SIZE + MAC_SIZE) {
    return;
  }

  auto index = get_le<uint32_t, 4>(packet);
  auto counter = get_le<uint64_t, 8>(packet);
  auto ciphertext = packet.subspan(DATA_HEADER_SIZE);

  auto *session = find_session(index);
  if (session == nullptr) {
    return;
  }

  if (counter < session->rx_counter) {
    ESP_LOGD(TAG, "Dropped old or replayed packet from %s", from.str().c_str());
    return;
  }

  std::array<uint8_t, MAX_PACKET_SIZE> buf;
  std::span<uint8_t> plain = buf;
  if (!decrypt_data(session->rx_key, counter, ciphertext, plain)) {
    ESP_LOGD(TAG, "Dropped invalid packet from %s", from.str().c_str());
    return;
  }

  session->rx_counter = counter + 1;
  session->endpoint = from;  // follow the client if its address changed
  session->last_rx = millis();

  if (pending_.contains(index)) {
    session = &activate_session(index);
  }

  if (plain.empty()) {
    send_data(*session, {});  // echo keepalive
  } else {
    handle_request(from, plain);
  }
}

void Transport::handle_request(const socket_address &from, std::span<const uint8_t> request) {
  if (request.size() < 3) {
    return;
  }

  auto address = get_le<uint16_t, 0>(request);
  auto token = request[2];
  if (allowed_requests_.count(request_key_type(address, token)) == 0) {
    ESP_LOGW(TAG, "Request for 0x%x:0x%x from %s not allowed", address, token, from.str().c_str());
    return;
  }

  auto frame = request.subspan(3);
  if (!request_callback_(address, token, request_data_type(frame.begin(), frame.end()))) {
    ESP_LOGW(TAG, "Received invalid request from %s, %zu bytes", from.str().c_str(), frame.size());
  }
}

void Transport::receive_packet() {
  std::array<uint8_t, MAX_PACKET_SIZE> buf;
  socket_address from;
  auto address = reinterpret_cast<sockaddr *>(&from.storage);
  auto n = socket_->recvfrom(buf.data(), buf.size(), address, &from.len);
  if (n < 4) {
    return;
  }

  auto packet = std::span(buf).first(n);
  auto type = get_le<uint32_t, 0>(packet);
  if (type == TYPE_HANDSHAKE_INITIATION) {
    handle_initiation(from, packet);
  } else if (type == TYPE_TRANSPORT_DATA) {
    handle_data(from, packet);
  }
}

void Transport::send_telegram(std::span<const uint8_t> telegram) {
  if (!socket_) {
    return;
  }

  for (auto &[index, session] : active_) {
    send_data(session, telegram);
  }
}

void Transport::loop(bool connected) {
  if (connected != is_connected_) {
    ESP_LOGI(TAG, connected ? "Connecting network ports." : "Disconnecting network ports.");
    is_connected_ = connected;
  }

  if (!is_connected_) {
    if (socket_) {
      ESP_LOGI(TAG, "UDP socket released for port %d", port_);
      socket_.reset();
      pending_.clear();
      active_.clear();
    }
    return;
  }

  if (!socket_) {
    socket_ = bind_udp_socket(port_);
    if (!socket_) {
      return;
    }
  }

  auto now = millis();
  std::erase_if(pending_, [&](const auto &item) { return now - item.second.last_rx > PENDING_TIMEOUT_MS; });
  std::erase_if(active_, [&](const auto &item) { return now - item.second.last_rx > SESSION_TIMEOUT_MS; });

  if (socket_->ready()) {
    receive_packet();
  }
}

void Transport::dump_config() {
  ESP_LOGCONFIG(TAG, " Transport: udp_noise");
  ESP_LOGCONFIG(TAG, " Port: %d", port_);
  for (auto const &key : allowed_requests_) {
    ESP_LOGCONFIG(TAG, " Request %x:%x", std::get<0>(key), std::get<1>(key));
  }
}

}  // namespace nibegw
}  // namespace esphome

#endif  // USE_NOISE
