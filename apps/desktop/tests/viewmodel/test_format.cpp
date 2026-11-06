// The web's vectors (apps/web/src/lib/game/format.test.ts), case for case.
#include <doctest/doctest.h>

#include <cmath>

#include "viewmodel/format.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::fmt;

TEST_CASE("format: floors quantities to the lot and emits engine decimal strings") {
  CHECK(qty_to_lot(16.66, 0.1) == "16.6");
  CHECK(qty_to_lot(0.05, 0.1) == "0");
  CHECK(qty_to_lot(3, 1) == "3");
}

TEST_CASE("format: rounds prices to the tick") {
  CHECK(price_to_tick(100.123, 0.01) == "100.12");
  CHECK(price_to_tick(-1, 0.01) == "");
}

TEST_CASE("format: regression: a level written into a PT-BR input uses the decimal comma") {
  CHECK(price_input_text(99.957, 0.01, Locale::PtBR) == "99,96");
  CHECK(price_input_text(1234.5, 0.01, Locale::PtBR) == "1234,50");
  CHECK(price_input_text(99.957, 0.01, Locale::En) == "99.96");
  CHECK(price_input_text(-1, 0.01, Locale::PtBR) == "");
  CHECK(parse_decimal(price_input_text(106.594, 0.01, Locale::PtBR)) == 106.59);
}

TEST_CASE("format: regression: a quantity written into a PT-BR input uses the decimal comma") {
  CHECK(qty_input_text(16.66, 0.1, Locale::PtBR) == "16,6");
  CHECK(qty_input_text(16.66, 0.1, Locale::En) == "16.6");
  CHECK(qty_input_text(3, 1, Locale::PtBR) == "3");
  CHECK(parse_decimal(qty_input_text(120.44, 0.1, Locale::PtBR)) == 120.4);
}

TEST_CASE("format: regression: reads a level typed with a PT-BR decimal comma") {
  CHECK(parse_decimal("95,56") == 95.56);
  CHECK(parse_decimal("95.56") == 95.56);
  CHECK(parse_decimal(" 95,5 ") == 95.5);
  CHECK(parse_decimal("1.234,5") == 1234.5);
  CHECK(parse_decimal("1,234.5") == 1234.5);
  CHECK(parse_decimal("-2,5") == -2.5);
  CHECK(parse_decimal("100") == 100);
  CHECK(std::isnan(parse_decimal("")));
  CHECK(std::isnan(parse_decimal("abc")));
  CHECK(std::isnan(parse_decimal("9,5,1")));
}

TEST_CASE("format: regression: a credit amount typed with a thousands separator is read as thousands") {
  CHECK(parse_amount("1.000", Locale::PtBR) == 1000);
  CHECK(parse_amount("12.500.000", Locale::PtBR) == 12'500'000);
  CHECK(parse_amount("1.000,50", Locale::PtBR) == 1000.5);
  CHECK(parse_amount("100,5", Locale::PtBR) == 100.5);
  CHECK(parse_amount("100.5", Locale::PtBR) == 100.5);
  CHECK(parse_amount("1,000", Locale::En) == 1000);
  CHECK(parse_amount("1.000", Locale::En) == 1);
  CHECK(parse_amount("2,5", Locale::En) == 2.5);
  CHECK(std::isnan(parse_amount("", Locale::PtBR)));
}

TEST_CASE("format: formats plain numbers in the locale") {
  CHECK(fmt_num(63, Locale::PtBR, 1) == "63,0");
  CHECK(fmt_num(0.75, Locale::En) == "0.75");
  CHECK(fmt_num(-0.26, Locale::PtBR, 2, true) == "-0,26");
  CHECK(fmt_num(1.5, Locale::En, 2, true) == "+1.50");
  CHECK(fmt_num(NAN, Locale::En) == "\xE2\x80\x94");
}

TEST_CASE("format: never relies on color alone") {
  CHECK(sign_mark(1) == "\xE2\x96\xB2");
  CHECK(sign_mark(-1) == "\xE2\x96\xBC");
  CHECK(sign_mark(0) == "\xE2\x80\xA2");
}

TEST_CASE("planHeadline: regression: the plan dialog quantity uses the locale") {
  CHECK(plan_headline(true, "49.9", 3, std::nullopt, Locale::PtBR) == "\xE2\x96\xB2 LONG 49,9 \xC2\xB7 3x");
  CHECK(plan_headline(false, "1500", 1, std::nullopt, Locale::PtBR) == "\xE2\x96\xBC SHORT 1.500,0 \xC2\xB7 1x");
  CHECK(plan_headline(true, "49.9", 3, std::nullopt, Locale::En) == "\xE2\x96\xB2 LONG 49.9 \xC2\xB7 3x");
}

TEST_CASE("planHeadline: shows whole option contracts without decimals") {
  CHECK(plan_headline(false, "20", 1, std::string("P 100 \xC2\xB7 exp 96"), Locale::PtBR) ==
        "\xE2\x96\xBC P 100 \xC2\xB7 exp 96 \xC3\x97 20");
}

// Desktop-only vectors: Intl.NumberFormat behaviour the web relies on implicitly.
TEST_CASE("format: Intl-compatible money, grouping and rounding") {
  CHECK(fmt_money("10000.00000000", Locale::PtBR) == "10.000,00 cr");
  CHECK(fmt_money("10000.00000000", Locale::En) == "10,000.00 cr");
  CHECK(fmt_money("-1.01771773", Locale::PtBR, true) == "-1,02 cr");
  CHECK(fmt_money("0.004", Locale::PtBR, true) == "0,00 cr");   // exceptZero: no sign on zero
  CHECK(fmt_money("-0.004", Locale::PtBR) == "0,00 cr");        // never "-0,00"
  CHECK(fmt_money("", Locale::PtBR) == "\xE2\x80\x94");
  CHECK(fmt_num(0.125, Locale::En, 2) == "0.13");                // ties round away from zero
  CHECK(fmt_num(-0.125, Locale::En, 2) == "-0.13");
  CHECK(fmt_num(1.005, Locale::En, 2) == "1.01");                // Intl rounds the shortest decimal "1.005"
  CHECK(fmt_num(999.995, Locale::En, 2) == "1,000.00");
  CHECK(price_to_tick(1.005, 0.01) == "1.00");                   // Math.round(100.49999999999999) = 100
  CHECK(qty_to_lot(2.675, 0.001) == "2.675");
  CHECK(fmt_num(9999999.5, Locale::PtBR, 0) == "10.000.000");
  CHECK(fmt_pct(-0.0001017, Locale::PtBR, 2, true) == "-0,01%");
  CHECK(fmt_price("101.43000000", Locale::PtBR) == "101,43");
  CHECK(fmt_qty("1.00000000", Locale::En) == "1.0");
}
