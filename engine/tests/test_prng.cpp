#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <vector>

#include "tradelab/prng.hpp"
#include "tradelab/serialize.hpp"

using namespace tradelab;

TEST_CASE("PCG32 matches the reference implementation (seed 42, stream 54)") {
  Pcg32 g(42U, std::uint64_t{54});
  const std::array<std::uint32_t, 6> expected = {0xa15c02b7, 0x7b47f409, 0xba1d3330,
                                                 0x83d2f293, 0xbfa4784b, 0xcbed606e};
  for (const auto e : expected) CHECK(g.next_u32() == e);
}

TEST_CASE("streams with the same seed are independent and reproducible") {
  Pcg32 a(7, Stream::Market);
  Pcg32 b(7, Stream::Market);
  Pcg32 c(7, Stream::Events);
  bool any_diff = false;
  for (int i = 0; i < 100; ++i) {
    const auto va = a.next_u32();
    CHECK(va == b.next_u32());
    any_diff = any_diff || va != c.next_u32();
  }
  CHECK(any_diff);
}

TEST_CASE("state round-trips for checkpoints") {
  Pcg32 g(123, Stream::Slippage);
  for (int i = 0; i < 10; ++i) (void)g.next_u32();
  Pcg32 restored = Pcg32::from_state(g.state(), g.inc());
  for (int i = 0; i < 100; ++i) CHECK(g.next_u32() == restored.next_u32());
}

TEST_CASE("bounded, unit and open-unit ranges") {
  Pcg32 g(1, Stream::Sandbox);
  for (int i = 0; i < 10000; ++i) {
    CHECK(g.next_bounded(7) < 7U);
    const double u = g.next_unit();
    CHECK(u >= 0.0);
    CHECK(u < 1.0);
    const double o = g.next_unit_open();
    CHECK(o > 0.0);
    CHECK(o < 1.0);
  }
}

TEST_CASE("normals have plausible moments") {
  Pcg32 g(2024, Stream::Market);
  constexpr int kN = 200000;
  double sum = 0.0;
  double sum_sq = 0.0;
  for (int i = 0; i < kN; ++i) {
    const double z = g.next_normal();
    sum += z;
    sum_sq += z * z;
  }
  const double mean = sum / kN;
  const double var = sum_sq / kN - mean * mean;
  CHECK(std::fabs(mean) < 0.01);
  CHECK(std::fabs(var - 1.0) < 0.01);
}

TEST_CASE("normal stream is bit-frozen (cross-target golden)") {
  Pcg32 g(42, Stream::Market);
  Writer w;
  for (int i = 0; i < 4096; ++i) {
    const double z = g.next_normal();
    std::uint64_t bits = 0;
    static_assert(sizeof bits == sizeof z);
    __builtin_memcpy(&bits, &z, sizeof bits);
    w.u64(bits);
  }
  CHECK(to_hex(w.digest()) == "6a680e49f73be8428e8af44813a1c9e9440ab8990c49c745d281d7e3bfa09654");
}
