#include <doctest/doctest.h>

#include <limits>
#include <vector>

#include "tradelab/serialize.hpp"

using namespace tradelab;

TEST_CASE("writer emits little-endian fixed-width fields") {
  Writer w;
  w.u16(0x0102);
  w.u32(0x03040506);
  w.i64(-2);
  const std::vector<std::uint8_t> expected = {0x02, 0x01, 0x06, 0x05, 0x04, 0x03,
                                              0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  CHECK(w.data() == expected);
}

TEST_CASE("round-trip of every field type") {
  Writer w;
  w.u8(7);
  w.u64(std::numeric_limits<std::uint64_t>::max());
  w.i64(std::numeric_limits<std::int64_t>::min());
  w.boolean(true);
  w.str("Mercado K7");
  w.fixed(parse_decimal<Price>("100.5").value());

  Reader r(w.data());
  CHECK(r.u8().value() == 7);
  CHECK(r.u64().value() == std::numeric_limits<std::uint64_t>::max());
  CHECK(r.i64().value() == std::numeric_limits<std::int64_t>::min());
  CHECK(r.boolean().value());
  CHECK(r.str().value() == "Mercado K7");
  CHECK(r.fixed<Price>().value() == parse_decimal<Price>("100.5").value());
  CHECK(r.at_end());
}

TEST_CASE("reader rejects truncated and non-canonical input") {
  Writer w;
  w.str("hello");
  std::vector<std::uint8_t> cut(w.data().begin(), w.data().end() - 1);
  Reader r1(cut);
  CHECK(r1.str().error() == ErrorCode::TruncatedInput);

  const std::vector<std::uint8_t> bad_bool = {2};
  Reader r2(bad_bool);
  CHECK(r2.boolean().error() == ErrorCode::NonCanonicalEncoding);

  const std::vector<std::uint8_t> short_u32 = {1, 2};
  Reader r3(short_u32);
  CHECK(r3.u32().error() == ErrorCode::TruncatedInput);
}

TEST_CASE("digest depends on every byte") {
  Writer a;
  Writer b;
  a.i64(1);
  b.i64(2);
  CHECK(a.digest() != b.digest());
}
