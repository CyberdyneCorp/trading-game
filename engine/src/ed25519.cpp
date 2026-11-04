#include "tradelab/ed25519.hpp"

#include "monocypher-ed25519.h"
#include "tradelab/sha256.hpp"

namespace tradelab {

KeyPair ed25519_keypair(const SecretSeed& seed) {
  KeyPair k;
  SecretSeed copy = seed;  // Monocypher wipes the seed it is given
  crypto_ed25519_key_pair(k.secret.data(), k.public_key.data(), copy.data());
  return k;
}

Signature ed25519_sign(const KeyPair& keys, std::span<const std::uint8_t> message) {
  Signature sig{};
  crypto_ed25519_sign(sig.data(), keys.secret.data(), message.data(), message.size());
  return sig;
}

bool ed25519_verify(const PublicKey& key, std::span<const std::uint8_t> message, const Signature& sig) {
  return crypto_ed25519_check(sig.data(), key.data(), message.data(), message.size()) == 0;
}

std::string key_id(std::string_view prefix, const PublicKey& key) {
  return std::string(prefix) + "-" + to_hex(sha256(key)).substr(0, 16);
}

}  // namespace tradelab
