#pragma once

// Ed25519 (RFC 8032) over the vendored Monocypher 4.0.2 implementation (third_party/monocypher).
// Signing is deterministic: the same seed and message always give the same signature.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace tradelab {

using PublicKey = std::array<std::uint8_t, 32>;
using SecretSeed = std::array<std::uint8_t, 32>;
using Signature = std::array<std::uint8_t, 64>;

struct KeyPair {
  std::array<std::uint8_t, 64> secret{};  // Monocypher layout: seed || public key
  PublicKey public_key{};
};

KeyPair ed25519_keypair(const SecretSeed& seed);
Signature ed25519_sign(const KeyPair& keys, std::span<const std::uint8_t> message);
bool ed25519_verify(const PublicKey& key, std::span<const std::uint8_t> message, const Signature& sig);

// Stable short id for a public key: "<prefix>-" + first 16 hex digits of SHA-256(key).
std::string key_id(std::string_view prefix, const PublicKey& key);

}  // namespace tradelab
