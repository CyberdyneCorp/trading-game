// Port of apps/web/src/lib/game/format.ts (same names, same test vectors in
// tests/viewmodel/test_format.cpp). Display only: results never feed back into engine state.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "model/i18n.hpp"

namespace tradelab::desktop::fmt {

// Engine decimal string -> number (NaN when absent or invalid), like the web's num().
double num(std::string_view decimal);

// Intl.NumberFormat(locale, {min = max = digits, signDisplay: signed ? 'exceptZero' : 'negative'}):
// PT-BR "1.234,50", en "1,234.50"; ties round away from zero on the exact value; never "-0,00".
std::string number(double v, Locale locale, int digits, bool signed_display = false);

std::string fmt_price(std::string_view decimal, Locale locale);
std::string fmt_qty(std::string_view decimal, Locale locale);
std::string fmt_money(double v, Locale locale, bool signed_display = false);
std::string fmt_money(std::string_view decimal, Locale locale, bool signed_display = false);
std::string fmt_num(double v, Locale locale, int digits = 2, bool signed_display = false);
std::string fmt_pct(double fraction, Locale locale, int digits = 2, bool signed_display = false);

// "95,56" and "95.56" both read 95.56; with both separators the last one is the decimal mark. NaN when invalid.
double parse_decimal(std::string_view input);
// Like parse_decimal, but the locale's thousands separator followed by groups of exactly three digits
// groups thousands ("1.000" in PT-BR is 1000). NaN when invalid.
double parse_amount(std::string_view input, Locale locale);

std::string_view sign_mark(double v);

std::string qty_to_lot(double qty, double lot);
std::string price_to_tick(double price, double tick);
std::string price_input_text(double price, double tick, Locale locale);
std::string qty_input_text(double qty, double lot, Locale locale);

// "▲ LONG 49,9 · 3x", or "▼ C 100 · exp 96 × 20" for an option.
std::string plan_headline(bool buy, std::string_view qty, int leverage, const std::optional<std::string>& option_label,
                          Locale locale);

}  // namespace tradelab::desktop::fmt
