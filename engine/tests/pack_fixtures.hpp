#pragma once

// Shared helpers for the pack, CSV, masking and codec tests.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tradelab/pack.hpp"

namespace tradelab::test {

inline std::vector<std::uint8_t> hex_bytes(std::string_view hex) {
  std::vector<std::uint8_t> out;
  const auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<std::uint8_t>(nib(hex[i]) * 16 + nib(hex[i + 1])));
  return out;
}

inline SecretSeed seed_of(std::uint8_t fill) {
  SecretSeed s{};
  for (std::size_t i = 0; i < s.size(); ++i) s[i] = static_cast<std::uint8_t>(fill + i);
  return s;
}

// A plausible hourly market: a slow sine around `base` with wicks; deterministic integer math.
inline std::string make_csv(std::size_t rows, std::int64_t start = 1'700'000'000, std::int64_t step = 3600, double base = 30'000.0,
                            bool funding = false) {
  std::string out = funding ? "timestamp,open,high,low,close,volume,funding\n" : "timestamp,open,high,low,close,volume\n";
  std::int64_t prev_close = static_cast<std::int64_t>(base * 100);
  for (std::size_t i = 0; i < rows; ++i) {
    const auto drift = static_cast<std::int64_t>((i * 7919) % 401) - 200;  // pseudo-random walk in cents
    const std::int64_t open = prev_close;
    const std::int64_t close = open + drift * 10;
    const std::int64_t high = std::max(open, close) + 150 + static_cast<std::int64_t>(i % 7) * 20;
    const std::int64_t low = std::min(open, close) - 120 - static_cast<std::int64_t>(i % 5) * 30;
    const auto cents = [](std::int64_t c) { return std::to_string(c / 100) + "." + (c % 100 < 10 ? "0" : "") + std::to_string(c % 100); };
    out += std::to_string(start + static_cast<std::int64_t>(i) * step) + "," + cents(open) + "," + cents(high) + "," + cents(low) + "," +
           cents(close) + "," + std::to_string(100 + i % 37) + ".5";
    if (funding) out += i % 8 == 0 ? ",0.0001" : ",";
    out += "\n";
    prev_close = close;
  }
  return out;
}

inline CsvMeta hourly_meta() {
  CsvMeta m;
  m.asset = "BTC";
  m.pair = "BTC/USDT";
  m.venue = "test-venue";
  return m;
}

}  // namespace tradelab::test
