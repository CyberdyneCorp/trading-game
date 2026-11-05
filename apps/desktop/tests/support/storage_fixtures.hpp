// Fixtures for the storage and pack tests: temporary folders, a deterministic CSV market (same walk as
// engine/tests/pack_fixtures.hpp) and the web's sample catalog (apps/web/static/catalog).
#pragma once

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace tradelab::desktop::testing {

class TempDir {
 public:
  TempDir() {
    static std::atomic<int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("tradelab-desktop-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }
  [[nodiscard]] std::string db() const { return (path_ / "tradelab.db").string(); }

 private:
  std::filesystem::path path_;
};

inline std::string cents(std::int64_t c) {
  return std::to_string(c / 100) + "." + (c % 100 < 10 ? "0" : "") + std::to_string(c % 100);
}

// Hourly OHLCV rows with a pseudo-random walk (engine test::make_csv).
inline std::string make_csv(std::size_t rows, std::int64_t start = 1'700'000'000, std::int64_t step = 3600) {
  std::string out = "timestamp,open,high,low,close,volume\n";
  std::int64_t prev_close = 3'000'000;
  for (std::size_t i = 0; i < rows; ++i) {
    const auto drift = static_cast<std::int64_t>((i * 7919) % 401) - 200;
    const std::int64_t open = prev_close;
    const std::int64_t close = open + drift * 10;
    const std::int64_t high = std::max(open, close) + 150 + static_cast<std::int64_t>(i % 7) * 20;
    const std::int64_t low = std::min(open, close) - 120 - static_cast<std::int64_t>(i % 5) * 30;
    out += std::to_string(start + static_cast<std::int64_t>(i) * step) + "," + cents(open) + "," + cents(high) + "," +
           cents(low) + "," + cents(close) + "," + std::to_string(100 + i % 37) + ".5\n";
    prev_close = close;
  }
  return out;
}

inline std::string hourly_meta(int leverage_cap = 5) {
  return R"({"asset":"BTC","pair":"BTC/USDT","venue":"test-venue","volumeUnit":"base","intervalSeconds":3600,)"
         R"("timestampUnit":"s","leverageCap":)" +
         std::to_string(leverage_cap) + "}";
}

inline std::filesystem::path web_catalog_dir() { return TRADELAB_WEB_CATALOG_DIR; }

}  // namespace tradelab::desktop::testing
