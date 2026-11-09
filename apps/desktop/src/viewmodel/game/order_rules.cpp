#include "viewmodel/game/order_rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "viewmodel/format.hpp"
#include "viewmodel/game/sizing.hpp"

namespace tradelab::desktop::game {

namespace {

constexpr std::array<std::string_view, 5> kTifCodes = {"gtc", "day", "bars", "ioc", "fok"};

// ---- the engine's fixed point (Price/Qty 1e-8, Rate 1e-10) ----

using Wide = __int128;

Wide pow10(int n) {
  Wide v = 1;
  for (int i = 0; i < n; ++i) v *= 10;
  return v;
}

// A decimal string as a fixed-point integer with `decimals` decimals (truncated beyond them).
Wide to_raw(std::string_view d, int decimals) {
  const bool neg = !d.empty() && d.front() == '-';
  if (neg) d.remove_prefix(1);
  const std::size_t dot = d.find('.');
  const std::string_view whole = d.substr(0, dot);
  const std::string_view frac = dot == std::string_view::npos ? std::string_view() : d.substr(dot + 1);
  Wide raw = 0;
  for (const char c : whole) raw = raw * 10 + (c - '0');
  for (int i = 0; i < decimals; ++i) {
    const auto at = static_cast<std::size_t>(i);
    raw = raw * 10 + (at < frac.size() ? frac[at] - '0' : 0);
  }
  return neg ? -raw : raw;
}

std::string from_raw(Wide raw) {
  const bool neg = raw < 0;
  Wide v = neg ? -raw : raw;
  const Wide scale = pow10(8);
  std::string frac = std::to_string(static_cast<long long>(v % scale));
  frac.insert(0, 8 - frac.size(), '0');
  return (neg ? "-" : "") + std::to_string(static_cast<long long>(v / scale)) + "." + frac;
}

Wide ceil_div(Wide a, Wide b) { return (a + b - 1) / b; }

}  // namespace

std::string_view tif_choice_code(TifChoice c) { return kTifCodes[static_cast<std::size_t>(c)]; }

std::optional<TifChoice> tif_choice_from(std::string_view code) {
  for (const TifChoice c : kTifChoices) {
    if (tif_choice_code(c) == code) return c;
  }
  return std::nullopt;
}

int day_bars(int step_seconds) {
  if (step_seconds <= 0) return 1;
  return std::max(1, static_cast<int>(std::lround(86'400.0 / step_seconds)));
}

int bars_left(int total_bars, int step) { return std::max(0, total_bars - 1 - step); }

std::string tif_block(TifChoice choice, std::string_view type) {
  if (choice == TifChoice::Ioc || choice == TifChoice::Fok) return type == "limit" ? "" : "needs_limit";
  if (choice == TifChoice::Day || choice == TifChoice::Bars) return type == "market" ? "market_immediate" : "";
  return "";
}

TifOrder tif_order(TifChoice choice, double bars_input, int step_seconds, int left) {
  if (choice != TifChoice::Day && choice != TifChoice::Bars) return {std::string(tif_choice_code(choice)), 0, {}};
  const double n = choice == TifChoice::Day ? std::min(day_bars(step_seconds), left) : std::floor(bars_input);
  if (!(n >= 1) || n > left) return {{}, 0, "bars_range"};
  return {"bars", static_cast<int>(n), {}};
}

TifCell tif_cell(const Order& o) {
  const std::string kind = o.tif.empty() ? "gtc" : o.tif;
  if (kind == "gtc" || o.expire_step == 0) return {kind, std::nullopt};
  return {kind, o.expire_step};
}

std::string link_problem(const Order& a, const Order& b) {
  if (a.id == b.id) return "same";
  if (a.parent_id != 0 || b.parent_id != 0) return "child";
  if (a.link_group != 0 || b.link_group != 0) return "linked";
  return "";
}

const Order* link_partner(const Order& o, const std::vector<Order>& orders) {
  if (o.link_group == 0) return nullptr;
  const auto it = std::find_if(orders.begin(), orders.end(), [&](const Order& x) { return x.id != o.id && x.link_group == o.link_group; });
  return it == orders.end() ? nullptr : &*it;
}

std::string link_mark(const Order& o, const std::vector<Order>& orders) {
  const Order* p = link_partner(o, orders);
  return p != nullptr ? "\xE2\x9B\x93#" + std::to_string(p->id) : std::string();
}

std::optional<BestClose> best_close(const std::vector<double>& closes, int placed_step, bool exit_sell) {
  std::optional<BestClose> best;
  for (std::size_t i = static_cast<std::size_t>(std::max(0, placed_step)); i < closes.size(); ++i) {
    const double c = closes[i];
    if (!std::isfinite(c)) continue;
    const bool better = !best || (exit_sell ? c > best->close : c < best->close);
    if (better) best = BestClose{static_cast<int>(i), c};
  }
  return best;
}

std::string trail_trigger(bool exit_sell, std::string_view close, std::string_view distance, std::string_view rate,
                          std::string_view tick) {
  const Wide c = to_raw(close, 8);
  const Wide t = to_raw(tick, 8);
  const Wide r = rate.empty() ? 0 : to_raw(rate, 10);
  const Wide dist = r > 0 ? ceil_div(c * r, pow10(10)) : to_raw(distance.empty() ? "0" : distance, 8);
  const Wide raw = exit_sell ? c - dist : c + dist;
  Wide ticks = 0;
  if (exit_sell) ticks = raw >= 0 ? raw / t : -ceil_div(-raw, t);
  else ticks = ceil_div(raw, t);
  return from_raw((ticks > 1 ? ticks : 1) * t);
}

std::string trail_label(const Order& o, Locale locale) {
  const double rate = fmt::num(o.trail_rate_text.empty() ? "0" : o.trail_rate_text);
  std::string rule;
  if (rate > 0) {
    const double pct = rate * 100;
    rule = fmt::fmt_pct(rate, locale, pct >= 1 && pct == std::floor(pct) ? 0 : 2);
  } else {
    rule = fmt::fmt_price(o.trail_distance_text, locale);
  }
  return "TRAIL " + rule + " \xC2\xB7 " + fmt::fmt_price(o.price_text, locale);
}

std::optional<PlannedTrailOrder> planned_trail_order(const PlannedTrail& p, double tick, Locale /*locale*/) {
  const double v = fmt::parse_decimal(p.value);
  if (!(v > 0)) return std::nullopt;
  if (p.pct) {
    const auto rate = trail_rate_from_percent(v);
    if (!rate) return std::nullopt;
    return PlannedTrailOrder{{}, *rate};
  }
  const double ticks = std::round(v / tick);
  if (ticks < 1) return std::nullopt;
  return PlannedTrailOrder{fmt::to_fixed_text(ticks * tick, fmt::step_decimals(tick)), {}};
}

}  // namespace tradelab::desktop::game
