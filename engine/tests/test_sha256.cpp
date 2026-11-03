#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "tradelab/sha256.hpp"

using namespace tradelab;

namespace {
std::string hash_str(const std::string& s) {
  const std::vector<std::uint8_t> v(s.begin(), s.end());
  return to_hex(sha256(v));
}
}  // namespace

TEST_CASE("SHA-256 FIPS 180-4 test vectors") {
  CHECK(hash_str("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(hash_str("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(hash_str("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(hash_str(std::string(1000000, 'a')) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("incremental updates equal one-shot hashing") {
  const std::string text(1000, 'x');
  const std::vector<std::uint8_t> v(text.begin(), text.end());
  Sha256 s;
  for (std::size_t i = 0; i < v.size(); i += 37) {
    const std::size_t n = std::min<std::size_t>(37, v.size() - i);
    s.update(std::span(v).subspan(i, n));
  }
  CHECK(to_hex(s.finish()) == to_hex(sha256(v)));
}
