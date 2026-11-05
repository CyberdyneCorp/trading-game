// libFuzzer target: pack validation on hostile bytes (spec scenario-packs "Pack safety limits").
// The trusted key is the fuzzer's fixed key, so mutated but re-signable inputs can reach the data
// decoders; seed the corpus with packs from tl_pack_build_csv for deeper coverage.

#include <cstdint>

#include "tradelab/pack.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  static const tradelab::KeyPair kKey = tradelab::ed25519_keypair(tradelab::SecretSeed{});
  static const std::vector<tradelab::TrustedKey> kKeys = {{tradelab::key_id("device", kKey.public_key), kKey.public_key}};
  tradelab::PackLimits limits;
  limits.max_pack_bytes = std::size_t{1} << 20U;
  limits.max_decompressed_file = std::size_t{1} << 20U;
  (void)tradelab::open_pack({data, size}, kKeys, tradelab::PackIntent::Publish, limits);
  return 0;
}
