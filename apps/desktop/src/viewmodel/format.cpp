#include "viewmodel/format.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace tradelab::desktop::fmt {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr std::string_view kDash = "\xE2\x80\x94";  // —

// Rounds a plain decimal string ("123.4567", no sign) to `digits` fraction digits, ties away from zero.
std::string round_decimal(std::string s, int digits) {
  if (s.find('.') == std::string::npos) s += '.';
  const std::size_t dot = s.find('.');
  const std::size_t keep = dot + 1 + static_cast<std::size_t>(digits);
  if (s.size() <= keep) s.append(keep + 1 - s.size(), '0');
  const bool round_up = s[keep] >= '5';
  s.resize(digits == 0 ? dot : keep);
  if (!round_up) return s;
  for (std::size_t i = s.size(); i-- > 0;) {
    if (s[i] == '.') continue;
    if (s[i] != '9') {
      ++s[i];
      return s;
    }
    s[i] = '0';
  }
  return "1" + s;
}

// Intl.NumberFormat rounds the shortest decimal that round-trips the double (ICU), so 1.005 -> "1.01".
std::string intl_magnitude(double v, int digits) {
  char buf[400];
  const auto r = std::to_chars(buf, buf + sizeof buf, std::fabs(v), std::chars_format::fixed);
  return round_decimal(std::string(buf, r.ptr), digits);
}

// Number.prototype.toFixed rounds the exact binary value, so (1.005).toFixed(2) -> "1.00".
std::string exact_magnitude(double v, int digits) {
  char buf[512];
  std::snprintf(buf, sizeof buf, "%.*f", digits + 40, std::fabs(v));
  return round_decimal(buf, digits);
}

bool all_zero(std::string_view digits) {
  for (char c : digits) {
    if (c != '0' && c != '.') return false;
  }
  return true;
}

std::string group_integer(std::string_view int_part, char sep) {
  std::string out;
  const std::size_t n = int_part.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (i > 0 && (n - i) % 3 == 0) out += sep;
    out += int_part[i];
  }
  return out;
}

std::string localize(const std::string& magnitude, Locale locale) {
  const char group = locale == Locale::PtBR ? '.' : ',';
  const char decimal = locale == Locale::PtBR ? ',' : '.';
  const std::size_t dot = magnitude.find('.');
  std::string out = group_integer(std::string_view(magnitude).substr(0, dot), group);
  if (dot != std::string::npos) {
    out += decimal;
    out += magnitude.substr(dot + 1);
  }
  return out;
}

std::string strip_spaces(std::string_view input) {
  std::string s;
  for (char c : input) {
    if (!std::isspace(static_cast<unsigned char>(c))) s += c;
  }
  return s;
}

// ^[+-]?(\d+\.?\d*|\.\d+)$
bool is_plain_number(std::string_view s) {
  if (!s.empty() && (s[0] == '+' || s[0] == '-')) s.remove_prefix(1);
  std::size_t int_digits = 0;
  std::size_t i = 0;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i, ++int_digits;
  if (i == s.size()) return int_digits > 0;
  if (s[i] != '.') return false;
  ++i;
  std::size_t frac_digits = 0;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i, ++frac_digits;
  return i == s.size() && (int_digits > 0 || frac_digits > 0);
}

// ^[+-]?\d{1,3}(<sep>\d{3})+$
bool is_grouped(std::string_view s, char sep) {
  if (!s.empty() && (s[0] == '+' || s[0] == '-')) s.remove_prefix(1);
  const std::size_t first = s.find(sep);
  if (first == std::string_view::npos || first == 0 || first > 3) return false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const bool sep_slot = i >= first && (i - first) % 4 == 0;
    const bool ok = sep_slot ? s[i] == sep : std::isdigit(static_cast<unsigned char>(s[i])) != 0;
    if (!ok) return false;
  }
  return (s.size() - first) % 4 == 0;
}

double to_number(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

int decimals_of(double step) { return std::max(0, static_cast<int>(std::lround(-std::log10(step)))); }

std::string to_fixed(double v, int digits) {
  const std::string mag = exact_magnitude(v, digits);
  return (v < 0 && !all_zero(mag) ? "-" : "") + mag;
}

std::string with_comma(std::string s, Locale locale) {
  if (locale == Locale::PtBR) {
    const std::size_t dot = s.find('.');
    if (dot != std::string::npos) s[dot] = ',';
  }
  return s;
}

}  // namespace

double num(std::string_view decimal) {
  if (decimal.empty()) return kNaN;
  const std::string s(decimal);
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  return end == s.c_str() + s.size() ? v : kNaN;
}

std::string number(double v, Locale locale, int digits, bool signed_display) {
  if (!std::isfinite(v)) return std::string(kDash);
  const std::string mag = intl_magnitude(v, digits);
  const bool zero = all_zero(mag);
  std::string sign;
  if (!zero && v < 0) sign = "-";
  if (!zero && v > 0 && signed_display) sign = "+";
  return sign + localize(mag, locale);
}

std::string fmt_price(std::string_view decimal, Locale locale) { return number(num(decimal), locale, 2); }

std::string fmt_qty(std::string_view decimal, Locale locale) { return number(num(decimal), locale, 1); }

std::string fmt_money(double v, Locale locale, bool signed_display) {
  return std::isfinite(v) ? number(v, locale, 2, signed_display) + " cr" : std::string(kDash);
}

std::string fmt_money(std::string_view decimal, Locale locale, bool signed_display) {
  return fmt_money(num(decimal), locale, signed_display);
}

std::string fmt_num(double v, Locale locale, int digits, bool signed_display) {
  return number(v, locale, digits, signed_display);
}

std::string fmt_pct(double fraction, Locale locale, int digits, bool signed_display) {
  if (!std::isfinite(fraction)) return std::string(kDash);
  return number(fraction * 100, locale, digits, signed_display) + "%";
}

double parse_decimal(std::string_view input) {
  const std::string s = strip_spaces(input);
  const auto last_comma = s.rfind(',');
  const auto last_dot = s.rfind('.');
  const bool comma_decimal = last_comma != std::string::npos && (last_dot == std::string::npos || last_comma > last_dot);
  const char decimal = comma_decimal ? ',' : '.';
  const char other = comma_decimal ? '.' : ',';
  std::string plain;
  for (char c : s) {
    if (c != other) plain += c;
  }
  const std::size_t first = plain.find(decimal);
  if (first != std::string::npos) plain[first] = '.';
  return is_plain_number(plain) ? to_number(plain) : kNaN;
}

double parse_amount(std::string_view input, Locale locale) {
  const std::string s = strip_spaces(input);
  if (!is_grouped(s, locale == Locale::PtBR ? '.' : ',')) return parse_decimal(s);
  std::string digits;
  for (char c : s) {
    if (c != '.' && c != ',') digits += c;
  }
  return to_number(digits);
}

std::string_view sign_mark(double v) {
  if (v > 0) return "\xE2\x96\xB2";  // ▲
  if (v < 0) return "\xE2\x96\xBC";  // ▼
  return "\xE2\x80\xA2";             // •
}

std::string to_fixed_text(double v, int digits) { return to_fixed(v, digits); }

int step_decimals(double step) { return decimals_of(step); }

std::string qty_to_lot(double qty, double lot) {
  if (!std::isfinite(qty) || qty <= 0) return "0";
  const double lots = std::floor(qty / lot + 1e-9);
  if (lots == 0) return "0";
  return to_fixed(lots * lot, decimals_of(lot));
}

std::string price_to_tick(double price, double tick) {
  if (!std::isfinite(price) || price <= 0) return "";
  const double ticks = std::floor(price / tick + 0.5);  // Math.round
  return to_fixed(ticks * tick, decimals_of(tick));
}

std::string price_input_text(double price, double tick, Locale locale) {
  return with_comma(price_to_tick(price, tick), locale);
}

std::string qty_input_text(double qty, double lot, Locale locale) { return with_comma(qty_to_lot(qty, lot), locale); }

std::string plan_headline(bool buy, std::string_view qty, int leverage, const std::optional<std::string>& option_label,
                          Locale locale) {
  const std::string arrow(sign_mark(buy ? 1 : -1));
  const double q = num(qty);
  if (option_label) {
    const int digits = std::floor(q) == q ? 0 : 1;
    return arrow + " " + *option_label + " \xC3\x97 " + fmt_num(q, locale, digits);  // ×
  }
  return arrow + (buy ? " LONG " : " SHORT ") + fmt_qty(qty, locale) + " \xC2\xB7 " + std::to_string(leverage) + "x";  // ·
}

}  // namespace tradelab::desktop::fmt
