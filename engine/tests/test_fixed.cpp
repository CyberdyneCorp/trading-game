#include <doctest/doctest.h>

#include <limits>

#include "tradelab/fixed.hpp"

using namespace tradelab;

namespace {
Price px(const char* s) { return parse_decimal<Price>(s).value(); }
Qty qty(const char* s) { return parse_decimal<Qty>(s).value(); }
Money money(const char* s) { return parse_decimal<Money>(s).value(); }
}  // namespace

TEST_CASE("div_round modes on positive and negative values") {
  CHECK(div_round(7, 2, Rounding::Floor) == 3);
  CHECK(div_round(7, 2, Rounding::Ceil) == 4);
  CHECK(div_round(7, 2, Rounding::TowardZero) == 3);
  CHECK(div_round(-7, 2, Rounding::Floor) == -4);
  CHECK(div_round(-7, 2, Rounding::Ceil) == -3);
  CHECK(div_round(-7, 2, Rounding::TowardZero) == -3);
  CHECK(div_round(5, 2, Rounding::HalfEven) == 2);
  CHECK(div_round(7, 2, Rounding::HalfEven) == 4);
  CHECK(div_round(-5, 2, Rounding::HalfEven) == -2);
  CHECK(div_round(-7, 2, Rounding::HalfEven) == -4);
  CHECK(div_round(8, 3, Rounding::HalfEven) == 3);
  CHECK(div_round(7, -2, Rounding::Floor) == -4);
}

TEST_CASE("against_player rounds payments up and receipts down") {
  CHECK(against_player(true) == Rounding::Ceil);
  CHECK(against_player(false) == Rounding::Floor);
}

TEST_CASE("decimal parse and format round-trip") {
  CHECK(px("100").raw == 100'0000'0000);
  CHECK(px("0.00000001").raw == 1);
  CHECK(px("-12.5").raw == -12'5000'0000);
  CHECK(to_decimal_string(px("100")) == "100.00000000");
  CHECK(to_decimal_string(px("-0.5")) == "-0.50000000");
  CHECK(to_decimal_string(Rate::from_raw(1)) == "0.0000000001");
  CHECK(format_decimal_raw(std::numeric_limits<std::int64_t>::min(), 8) == "-92233720368.54775808");
}

TEST_CASE("decimal parse rejects anything ambiguous (no silent rounding)") {
  CHECK(parse_decimal<Price>("1.123456789").error() == ErrorCode::InvalidDecimal);  // 9 > 8 decimals
  CHECK(parse_decimal<Price>("1e5").error() == ErrorCode::InvalidDecimal);
  CHECK(parse_decimal<Price>(" 1").error() == ErrorCode::InvalidDecimal);
  CHECK(parse_decimal<Price>("1.").error() == ErrorCode::InvalidDecimal);
  CHECK(parse_decimal<Price>(".5").error() == ErrorCode::InvalidDecimal);
  CHECK(parse_decimal<Price>("").error() == ErrorCode::InvalidDecimal);
  CHECK(parse_decimal<Price>("-").error() == ErrorCode::InvalidDecimal);
  CHECK(parse_decimal<Price>("1,5").error() == ErrorCode::InvalidDecimal);  // locale-independent
  CHECK_FALSE(parse_decimal<Price>("99999999999").ok());                   // > int64 at 1e-8
}

TEST_CASE("checked add/sub/neg detect overflow") {
  const auto max = Money::from_raw(std::numeric_limits<std::int64_t>::max());
  const auto min = Money::from_raw(std::numeric_limits<std::int64_t>::min());
  CHECK(checked_add(max, Money::from_raw(1)).error() == ErrorCode::NumericOverflow);
  CHECK(checked_sub(min, Money::from_raw(1)).error() == ErrorCode::NumericOverflow);
  CHECK(checked_neg(min).error() == ErrorCode::NumericOverflow);
  CHECK(checked_add(money("1.5"), money("2.25")).value() == money("3.75"));
}

TEST_CASE("notional = qty * price with explicit rounding") {
  const auto notional = mul<Money>(qty("16.6"), px("100.12345678"), Rounding::HalfEven);
  REQUIRE(notional.ok());
  CHECK(to_decimal_string(notional.value()) == "1662.04938255");  // 1662.049382548 -> half-even
  CHECK(mul<Money>(qty("16.6"), px("100.12345678"), Rounding::Ceil).value() == money("1662.04938255"));
  CHECK(mul<Money>(qty("16.6"), px("100.12345678"), Rounding::Floor).value() == money("1662.04938254"));
}

TEST_CASE("fee = notional * rate rounds against the player") {
  const Rate taker = parse_decimal<Rate>("0.0005").value();  // 5 bps
  const auto fee = mul<Money>(money("1662.04938255"), taker, against_player(true));
  REQUIRE(fee.ok());
  CHECK(to_decimal_string(fee.value()) == "0.83102470");  // 0.831024691275 -> ceil
}

TEST_CASE("funding example from spec FUT03 is exact") {
  const Rate rate = parse_decimal<Rate>("0.0001").value();  // +0.01%
  CHECK(mul<Money>(money("10000"), rate, Rounding::HalfEven).value() == money("1"));
}

TEST_CASE("div: quantity from risk budget (spec sizing example)") {
  // 100 credits of risk / 6 credits per unit = 16.666... units; sizing rounds down.
  const auto q = div<Qty>(money("100"), money("6"), Rounding::Floor);
  REQUIRE(q.ok());
  CHECK(to_decimal_string(q.value()) == "16.66666666");
  CHECK(round_to_step(q.value(), qty("0.1"), Rounding::Floor).value() == qty("16.6"));
  CHECK(div<Qty>(money("1"), Money{}, Rounding::Floor).error() == ErrorCode::DivisionByZero);
}

TEST_CASE("mul/div report overflow instead of wrapping") {
  const auto big_px = Price::from_raw(std::numeric_limits<std::int64_t>::max());
  const auto big_qty = Qty::from_raw(std::numeric_limits<std::int64_t>::max());
  CHECK(mul<Money>(big_qty, big_px, Rounding::Floor).error() == ErrorCode::NumericOverflow);
  CHECK(div<Qty>(Money::from_raw(std::numeric_limits<std::int64_t>::max()), Money::from_raw(1), Rounding::Floor)
            .error() == ErrorCode::NumericOverflow);
}

TEST_CASE("rescale between scales") {
  const Rate r = parse_decimal<Rate>("0.1234567891").value();
  CHECK(rescale<Vol>(r, Rounding::Floor).value().raw == 12345678);
  CHECK(rescale<Vol>(r, Rounding::Ceil).value().raw == 12345679);
  CHECK(rescale<Rate>(Vol::from_raw(1), Rounding::Floor).value().raw == 100);
}

TEST_CASE("round_to_step for ticks and lots") {
  CHECK(round_to_step(px("100.07"), px("0.05"), Rounding::Floor).value() == px("100.05"));
  CHECK(round_to_step(px("100.07"), px("0.05"), Rounding::Ceil).value() == px("100.1"));
  CHECK(round_to_step(px("1"), Price{}, Rounding::Floor).error() == ErrorCode::InvalidScale);
}

TEST_CASE("regression: checked 128-bit multiply detects overflow without runtime helpers") {
  i128 out = 0;
  const i128 max = static_cast<i128>((static_cast<unsigned __int128>(1) << 127U) - 1);
  const i128 min = -max - 1;
  CHECK(checked_mul_i128(3, -7, &out));
  CHECK(out == -21);
  CHECK(checked_mul_i128(min, 1, &out));
  CHECK(out == min);
  CHECK_FALSE(checked_mul_i128(min, -1, &out));
  CHECK_FALSE(checked_mul_i128(max, 2, &out));
  CHECK(checked_mul_i128(max / 2, 2, &out));
  CHECK_FALSE(checked_mul_i128(static_cast<i128>(1) << 64U, static_cast<i128>(1) << 63U, &out));
  CHECK(checked_mul_i128(-(static_cast<i128>(1) << 63U), static_cast<i128>(1) << 64U, &out));  // == min
  CHECK(out == min);
}
