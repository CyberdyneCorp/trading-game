#include <doctest/doctest.h>

#include "pack_fixtures.hpp"
#include "tradelab/ed25519.hpp"
#include "tradelab/sha256.hpp"

using namespace tradelab;

namespace {

SecretSeed seed_hex(std::string_view hex) {
  SecretSeed s{};
  const auto b = test::hex_bytes(hex);
  std::copy(b.begin(), b.end(), s.begin());
  return s;
}

std::string hex(std::span<const std::uint8_t> b) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  for (const auto x : b) {
    out += kHex[x >> 4U];
    out += kHex[x & 15U];
  }
  return out;
}

}  // namespace

TEST_CASE("ed25519: RFC 8032 test vectors 1 and 2") {
  const KeyPair k1 = ed25519_keypair(seed_hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"));
  CHECK(hex(k1.public_key) == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  const Signature s1 = ed25519_sign(k1, {});
  CHECK(hex(s1) ==
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
  CHECK(ed25519_verify(k1.public_key, {}, s1));

  const KeyPair k2 = ed25519_keypair(seed_hex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb"));
  CHECK(hex(k2.public_key) == "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
  const std::vector<std::uint8_t> msg = {0x72};
  const Signature s2 = ed25519_sign(k2, msg);
  CHECK(hex(s2) ==
        "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
  CHECK(ed25519_verify(k2.public_key, msg, s2));
}

TEST_CASE("ed25519: a changed message, signature or key does not verify") {
  const KeyPair k = ed25519_keypair(test::seed_of(1));
  const std::vector<std::uint8_t> msg = {'p', 'a', 'c', 'k'};
  const Signature s = ed25519_sign(k, msg);
  CHECK(ed25519_verify(k.public_key, msg, s));
  auto other_msg = msg;
  other_msg[0] = 'P';
  CHECK_FALSE(ed25519_verify(k.public_key, other_msg, s));
  auto other_sig = s;
  other_sig[10] ^= 1U;
  CHECK_FALSE(ed25519_verify(k.public_key, msg, other_sig));
  CHECK_FALSE(ed25519_verify(ed25519_keypair(test::seed_of(2)).public_key, msg, s));
}

TEST_CASE("ed25519: key ids are stable and prefixed") {
  const KeyPair k = ed25519_keypair(test::seed_of(1));
  CHECK(key_id("device", k.public_key) == "device-" + to_hex(sha256(k.public_key)).substr(0, 16));
  CHECK(key_id("device", k.public_key) == key_id("device", ed25519_keypair(test::seed_of(1)).public_key));
}
