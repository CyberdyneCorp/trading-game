#pragma once

// Fixed-point quantities (design D2). All engine state uses these; doubles never reach state.

#include <compare>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "tradelab/result.hpp"

namespace tradelab {

using i128 = __int128;

enum class Rounding : std::uint8_t {
  Floor,       // toward -infinity
  Ceil,        // toward +infinity
  TowardZero,  // truncate
  HalfEven,    // banker's rounding
};

// Rounding that always costs the player: amounts the player pays round up,
// amounts the player receives round down.
constexpr Rounding against_player(bool player_pays) {
  return player_pays ? Rounding::Ceil : Rounding::Floor;
}

constexpr std::int64_t pow10_i64(int n) {
  std::int64_t r = 1;
  for (int i = 0; i < n; ++i) r *= 10;
  return r;
}

constexpr i128 pow10_i128(int n) {
  i128 r = 1;
  for (int i = 0; i < n; ++i) r *= 10;
  return r;
}

// n / d rounded per mode. d != 0 is a precondition (checked by callers).
constexpr i128 div_round(i128 n, i128 d, Rounding mode) {
  if (d < 0) {
    n = -n;
    d = -d;
  }
  i128 q = n / d;  // truncates toward zero
  const i128 r = n % d;
  if (r == 0) return q;
  const bool negative = n < 0;
  switch (mode) {
    case Rounding::TowardZero:
      return q;
    case Rounding::Floor:
      return negative ? q - 1 : q;
    case Rounding::Ceil:
      return negative ? q : q + 1;
    case Rounding::HalfEven: {
      const i128 twice = (r < 0 ? -r : r) * 2;
      const bool round_away = twice > d || (twice == d && (q % 2 != 0));
      if (!round_away) return q;
      return negative ? q - 1 : q + 1;
    }
  }
  return q;
}

constexpr bool fits_i64(i128 v) {
  return v >= std::numeric_limits<std::int64_t>::min() && v <= std::numeric_limits<std::int64_t>::max();
}

template <typename Tag, int Decimals>
struct Fixed {
  static_assert(Decimals >= 0 && Decimals <= 18);
  static constexpr int kDecimals = Decimals;
  static constexpr std::int64_t kScale = pow10_i64(Decimals);

  std::int64_t raw = 0;

  static constexpr Fixed from_raw(std::int64_t r) { return Fixed{r}; }
  static constexpr Result<Fixed> from_units(std::int64_t units) {
    const i128 v = static_cast<i128>(units) * kScale;
    if (!fits_i64(v)) return ErrorCode::NumericOverflow;
    return Fixed{static_cast<std::int64_t>(v)};
  }

  friend constexpr auto operator<=>(Fixed, Fixed) = default;
  friend constexpr bool operator==(Fixed, Fixed) = default;
};

template <typename F>
constexpr Result<F> checked_add(F a, F b) {
  std::int64_t out = 0;
  if (__builtin_add_overflow(a.raw, b.raw, &out)) return ErrorCode::NumericOverflow;
  return F{out};
}

template <typename F>
constexpr Result<F> checked_sub(F a, F b) {
  std::int64_t out = 0;
  if (__builtin_sub_overflow(a.raw, b.raw, &out)) return ErrorCode::NumericOverflow;
  return F{out};
}

template <typename F>
constexpr Result<F> checked_neg(F a) {
  if (a.raw == std::numeric_limits<std::int64_t>::min()) return ErrorCode::NumericOverflow;
  return F{-a.raw};
}

// Portable checked 128-bit multiply. __builtin_mul_overflow on __int128 makes clang call
// __muloti4 (compiler-rt only), which libgcc-based Linux toolchains do not provide.
constexpr bool checked_mul_i128(i128 a, i128 b, i128* out) {
  using u128 = unsigned __int128;
  const u128 ua = a < 0 ? u128(0) - static_cast<u128>(a) : static_cast<u128>(a);
  const u128 ub = b < 0 ? u128(0) - static_cast<u128>(b) : static_cast<u128>(b);
  const bool negative = (a < 0) != (b < 0);
  const u128 limit = negative ? (u128(1) << 127U) : (u128(1) << 127U) - 1;
  if (ua != 0 && ub > limit / ua) return false;
  const u128 mag = ua * ub;
  *out = negative ? static_cast<i128>(u128(0) - mag) : static_cast<i128>(mag);
  return true;
}

// v * 10^exp (exp >= 0) or v / 10^-exp rounded, with 128-bit overflow detection.
constexpr bool shift_decimal(i128 v, int exp, Rounding mode, i128* out) {
  if (exp >= 0) return checked_mul_i128(v, pow10_i128(exp), out);
  *out = div_round(v, pow10_i128(-exp), mode);
  return true;
}

// out = a * b, converted to Out's scale with the given rounding.
template <typename Out, typename A, typename B>
constexpr Result<Out> mul(A a, B b, Rounding mode) {
  // |a.raw * b.raw| < 2^126, always representable in i128.
  const i128 prod = static_cast<i128>(a.raw) * static_cast<i128>(b.raw);
  i128 v = 0;
  if (!shift_decimal(prod, Out::kDecimals - A::kDecimals - B::kDecimals, mode, &v) || !fits_i64(v)) {
    return ErrorCode::NumericOverflow;
  }
  return Out{static_cast<std::int64_t>(v)};
}

// out = a / b, converted to Out's scale with the given rounding.
template <typename Out, typename A, typename B>
constexpr Result<Out> div(A a, B b, Rounding mode) {
  if (b.raw == 0) return ErrorCode::DivisionByZero;
  // a/b in Out units = a.raw * 10^(Out + B - A) / b.raw.
  constexpr int kExp = Out::kDecimals + B::kDecimals - A::kDecimals;
  i128 num = static_cast<i128>(a.raw);
  i128 den = static_cast<i128>(b.raw);
  if constexpr (kExp >= 0) {
    if (!checked_mul_i128(num, pow10_i128(kExp), &num)) return ErrorCode::NumericOverflow;
  } else {
    if (!checked_mul_i128(den, pow10_i128(-kExp), &den)) return ErrorCode::NumericOverflow;
  }
  const i128 v = div_round(num, den, mode);
  if (!fits_i64(v)) return ErrorCode::NumericOverflow;
  return Out{static_cast<std::int64_t>(v)};
}

// Converts between scales (e.g. Money -> Price-denominated) with explicit rounding.
template <typename Out, typename In>
constexpr Result<Out> rescale(In v, Rounding mode) {
  i128 out = 0;
  if (!shift_decimal(v.raw, Out::kDecimals - In::kDecimals, mode, &out) || !fits_i64(out)) {
    return ErrorCode::NumericOverflow;
  }
  return Out{static_cast<std::int64_t>(out)};
}

// Rounds v down/up/nearest to a multiple of step (tick or lot size). step > 0.
template <typename F>
constexpr Result<F> round_to_step(F v, F step, Rounding mode) {
  if (step.raw <= 0) return ErrorCode::InvalidScale;
  const i128 n = div_round(v.raw, step.raw, mode);
  const i128 out = n * step.raw;
  if (!fits_i64(out)) return ErrorCode::NumericOverflow;
  return F{static_cast<std::int64_t>(out)};
}

// Strict decimal parsing: [-]digits[.digits], at most `decimals` fractional digits.
// No exponent, no whitespace, no silent rounding.
Result<std::int64_t> parse_decimal_raw(std::string_view text, int decimals);
std::string format_decimal_raw(std::int64_t raw, int decimals);

template <typename F>
Result<F> parse_decimal(std::string_view text) {
  auto r = parse_decimal_raw(text, F::kDecimals);
  if (!r.ok()) return r.error();
  return F{r.value()};
}

template <typename F>
std::string to_decimal_string(F v) {
  return format_decimal_raw(v.raw, F::kDecimals);
}

// Engine-wide quantity types (execution-policy v1).
struct PriceTag {};
struct QtyTag {};
struct MoneyTag {};
struct RateTag {};
struct VolTag {};

using Price = Fixed<PriceTag, 8>;
using Qty = Fixed<QtyTag, 8>;
using Money = Fixed<MoneyTag, 8>;
using Rate = Fixed<RateTag, 10>;
using Vol = Fixed<VolTag, 8>;

}  // namespace tradelab
