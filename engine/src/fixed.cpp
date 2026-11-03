#include "tradelab/fixed.hpp"

namespace tradelab {
namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Appends digits of `part` into acc; returns false on non-digit or overflow.
bool accumulate_digits(std::string_view part, i128* acc) {
  for (const char c : part) {
    if (!is_digit(c)) return false;
    if (!checked_mul_i128(*acc, 10, acc)) return false;
    *acc += c - '0';
    if (*acc > static_cast<i128>(std::numeric_limits<std::int64_t>::max()) + 1) return false;
  }
  return true;
}

}  // namespace

Result<std::int64_t> parse_decimal_raw(std::string_view text, int decimals) {
  if (decimals < 0 || decimals > 18) return ErrorCode::InvalidScale;
  bool negative = false;
  if (!text.empty() && text.front() == '-') {
    negative = true;
    text.remove_prefix(1);
  }
  const auto dot = text.find('.');
  const std::string_view int_part = text.substr(0, dot);
  const std::string_view frac_part = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
  const bool has_dot = dot != std::string_view::npos;
  if (int_part.empty() || (has_dot && frac_part.empty())) return ErrorCode::InvalidDecimal;
  if (static_cast<int>(frac_part.size()) > decimals) return ErrorCode::InvalidDecimal;

  i128 acc = 0;
  if (!accumulate_digits(int_part, &acc) || !accumulate_digits(frac_part, &acc)) {
    return ErrorCode::InvalidDecimal;
  }
  i128 scaled = 0;
  if (!checked_mul_i128(acc, pow10_i128(decimals - static_cast<int>(frac_part.size())), &scaled)) {
    return ErrorCode::NumericOverflow;
  }
  if (negative) scaled = -scaled;
  if (!fits_i64(scaled)) return ErrorCode::NumericOverflow;
  return static_cast<std::int64_t>(scaled);
}

std::string format_decimal_raw(std::int64_t raw, int decimals) {
  const bool negative = raw < 0;
  // Work in unsigned 128-bit to handle INT64_MIN.
  unsigned __int128 mag = negative ? static_cast<unsigned __int128>(-static_cast<i128>(raw))
                                   : static_cast<unsigned __int128>(raw);
  std::string digits;
  do {
    digits.insert(digits.begin(), static_cast<char>('0' + static_cast<int>(mag % 10)));
    mag /= 10;
  } while (mag != 0);
  const auto d = static_cast<std::size_t>(decimals);
  if (digits.size() <= d) digits.insert(digits.begin(), d + 1 - digits.size(), '0');
  std::string out = negative ? "-" : "";
  out += digits.substr(0, digits.size() - d);
  if (d > 0) {
    out += '.';
    out += digits.substr(digits.size() - d);
  }
  return out;
}

}  // namespace tradelab
