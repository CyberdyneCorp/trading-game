#include "viewmodel/practice/prng.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace tradelab::desktop::practice {

std::uint32_t fnv1a32(std::string_view text) {
  std::uint32_t h = 0x811c9dc5U;
  for (const char c : text) {
    h ^= static_cast<std::uint8_t>(c);
    h *= 0x01000193U;
  }
  return h;
}

std::uint32_t Mulberry32::operator()() {
  a_ += 0x6d2b79f5U;
  std::uint32_t t = a_;
  t = (t ^ (t >> 15)) * (t | 1U);
  t ^= t + (t ^ (t >> 7)) * (t | 61U);
  return t ^ (t >> 14);
}

namespace {

// Floor division (the web's Math.floor of a quotient), for negative years too.
std::int64_t floor_div(std::int64_t a, std::int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

// "YYYY-MM-DD" as {y, m, d}; none unless exactly four, two and two digits.
std::optional<std::array<int, 3>> civil(std::string_view s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return std::nullopt;
  std::array<int, 3> out{};
  const std::array<std::pair<std::size_t, std::size_t>, 3> parts{{{0, 4}, {5, 2}, {8, 2}}};
  for (std::size_t k = 0; k < parts.size(); ++k) {
    for (std::size_t i = parts[k].first; i < parts[k].first + parts[k].second; ++i) {
      if (s[i] < '0' || s[i] > '9') return std::nullopt;
      out[k] = out[k] * 10 + (s[i] - '0');
    }
  }
  return out;
}

}  // namespace

std::int64_t days_from_civil(std::int64_t y, int m, int d) {
  const std::int64_t yy = m <= 2 ? y - 1 : y;
  const std::int64_t era = floor_div(yy, 400);
  const std::int64_t yoe = yy - era * 400;
  const std::int64_t mp = (m + 9) % 12;
  const std::int64_t doy = (153 * mp + 2) / 5 + d - 1;
  const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

std::optional<std::int64_t> days_between(std::string_view from, std::string_view to) {
  const auto a = civil(from);
  const auto b = civil(to);
  if (!a || !b) return std::nullopt;
  return days_from_civil((*b)[0], (*b)[1], (*b)[2]) - days_from_civil((*a)[0], (*a)[1], (*a)[2]);
}

}  // namespace tradelab::desktop::practice
