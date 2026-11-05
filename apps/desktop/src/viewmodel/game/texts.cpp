#include "viewmodel/game/texts.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "viewmodel/format.hpp"
#include "viewmodel/game/options_math.hpp"

namespace tradelab::desktop::game {

namespace {

bool finite(const std::optional<double>& v) { return v && std::isfinite(*v); }

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
  return s;
}

std::string flags(const Texts& t, const EngineEvent& e) {
  std::string f;
  const auto add = [&](bool on, std::string_view key) {
    if (!on) return;
    f += (f.empty() ? "" : ", ") + t.tr(key);
  };
  add(e.gap, "feed.gap");
  add(e.ambiguous, "feed.ambiguous");
  add(e.capacity_limited, "feed.capacity");
  return f.empty() ? f : " [" + f + "]";
}

std::string fill_line(const Texts& t, const EngineEvent& e) {
  const Locale L = t.locale();
  const Params vars = {{"id", std::to_string(e.order_id)},
                       {"side", upper(e.side)},
                       {"qty", fmt::fmt_qty(e.qty, L)},
                       {"price", fmt::fmt_price(e.price, L)}};
  std::string out = t.tr(e.order_id ? "feed.filled" : "feed.forcedFill", vars);
  const double requested = fmt::num(e.requested);
  if (requested > 0 && e.requested != e.price) out += " \xC2\xB7 " + t.tr("feed.requested", {{"price", fmt::fmt_price(e.requested, L)}});
  out += " \xC2\xB7 " + t.tr("feed.fee", {{"fee", fmt::fmt_money(e.amount, L)}});
  if (fmt::num(e.pnl) != 0) out += " \xC2\xB7 " + t.tr("feed.pnl", {{"pnl", fmt::fmt_money(e.pnl, L, true)}});
  return out + flags(t, e);
}

std::optional<std::string> order_line(const Texts& t, const EngineEvent& e, const SeriesNames& names) {
  const Locale L = t.locale();
  const std::string id = std::to_string(e.order_id);
  if (e.reason == "order_moved") return t.tr("feed.moved", {{"id", id}, {"price", fmt::fmt_price(e.requested, L)}});
  if (e.status == "accepted") {
    const std::string type(e.order_type.empty() ? "MKT" : order_type_label(e.order_type));
    const int series = e.series ? e.series : names.series_of_order(e.order_id);
    const std::string prefix = series ? names.name(series, L) + " " : "";
    return t.tr("feed.accepted",
                {{"id", id}, {"type", prefix + type}, {"side", t.tr("side." + e.side)}, {"qty", fmt::fmt_qty(e.qty, L)}});
  }
  if (e.status == "cancelled") return t.tr("feed.cancelled", {{"id", id}, {"reason", t.reason(e.reason)}});
  if (e.status == "expired") return t.tr("feed.expired", {{"id", id}});
  return std::nullopt;
}

Line settlement_line(const Texts& t, const EngineEvent& e, const SeriesNames& names) {
  const Locale L = t.locale();
  std::string text = t.tr("feed.settlement", {{"series", names.name(e.series, L)},
                                              {"price", fmt::fmt_price(e.price, L)},
                                              {"value", fmt::fmt_price(e.requested, L)},
                                              {"amount", fmt::fmt_money(e.amount, L, true)}});
  if (e.approximate) text += " [" + t.tr("feed.approxSettle") + "]";
  return {text, fmt::num(e.amount) >= 0 ? Tone::Up : Tone::Down};
}

std::optional<Line> money_line(const Texts& t, const EngineEvent& e) {
  const Locale L = t.locale();
  if (e.type == "margin_adjusted") return Line{t.tr("feed.marginAdjusted", {{"amount", fmt::fmt_money(e.amount, L, true)}}), Tone::Info};
  if (e.type == "margin_call") return Line{"\xE2\x9A\xA0 " + t.tr("feed.marginCall", {{"mm", fmt::fmt_money(e.amount, L)}}), Tone::Down};
  if (e.type == "funding") {
    return Line{t.tr("feed.funding", {{"amount", fmt::fmt_money(e.amount, L, true)}}),
                fmt::num(e.amount) >= 0 ? Tone::Up : Tone::Down};
  }
  if (e.type == "liquidation") {
    return Line{"\xE2\x9C\x96 " +
                    t.tr("feed.liquidation", {{"price", fmt::fmt_price(e.price, L)}, {"amount", fmt::fmt_money(e.amount, L)}}) +
                    flags(t, e),
                Tone::Down};
  }
  return std::nullopt;
}

}  // namespace

std::string Texts::count(std::string_view key, long n, Params params) const {
  const std::string one = std::string(key) + "One";
  params.emplace_back("n", fmt::fmt_num(static_cast<double>(n), locale(), 0));
  return tr(n == 1 && has(one) ? std::string_view(one) : key, params);
}

std::string Texts::reason(std::string_view code) const {
  const std::string key = "reason." + std::string(code);
  return has(key) ? tr(key) : std::string(code);
}

std::string Texts::tr_or(std::string_view key, std::string_view fallback, const Params& params) const {
  return has(key) ? tr(key, params) : std::string(fallback);
}

std::string rejection_text(const Texts& t, std::string_view reason, const RejectionFacts& f) {
  const Locale L = t.locale();
  if (reason == "insufficient_margin" && finite(f.need) && finite(f.available) && *f.need > *f.available) {
    return t.tr("reasonWhy.insufficient_margin", {{"need", fmt::fmt_money(*f.need, L)}, {"avail", fmt::fmt_money(*f.available, L)}});
  }
  if (reason == "margin_below_initial" && finite(f.margin) && finite(f.initial) && *f.margin < *f.initial) {
    return t.tr("reasonWhy.margin_below_initial",
                {{"margin", fmt::fmt_money(*f.margin, L)}, {"initial", fmt::fmt_money(*f.initial, L)}});
  }
  if (reason == "leverage_above_cap" && f.leverage && f.cap && *f.leverage > *f.cap) {
    return t.tr("reasonWhy.leverage_above_cap", {{"lev", std::to_string(*f.leverage)}, {"cap", std::to_string(*f.cap)}});
  }
  return t.reason(reason);
}

namespace {
double position_notional(const GameState& g) {
  return std::fabs(g.position.qty) * g.position.entry * g.contract.multiplier;
}
}  // namespace

RejectionFacts order_facts(const GameState& g, double need, int leverage) {
  RejectionFacts f;
  f.need = need;
  f.available = g.account.available;
  f.leverage = leverage;
  f.cap = g.contract.leverage_cap;
  return f;
}

RejectionFacts relever_facts(const GameState& g, int leverage) {
  return order_facts(g, position_notional(g) / std::max(1, leverage) - g.position.margin, leverage);
}

RejectionFacts margin_facts(const GameState& g, double delta) {
  RejectionFacts f;
  if (delta > 0) {
    f.need = delta;
    f.available = g.account.available;
    return f;
  }
  f.margin = g.position.margin + delta;
  f.initial = position_notional(g) / std::max(1, g.leverage);
  return f;
}

std::string_view order_type_label(std::string_view type) {
  if (type == "market") return "MKT";
  if (type == "limit") return "LMT";
  if (type == "stop_market") return "STOP";
  if (type == "take_profit") return "TP";
  if (type == "trailing_stop") return "TRAIL";
  return type;
}

std::string order_price_text(const Texts& t, const Order& o) {
  if (o.type == "market") return "@open";
  const std::string price = fmt::fmt_price(o.price_text, t.locale());
  return o.series ? t.tr("orders.premium", {{"price", price}}) : price;
}

void SeriesNames::reset() {
  series_.clear();
  order_series_.clear();
}

void SeriesNames::observe(const GameState& s) {
  for (const auto& q : s.options.chain) series_.try_emplace(q.id, Key{q.kind, q.strike, q.expiry_step});
  for (const auto& o : s.orders) {
    if (o.series) order_series_[o.id] = o.series;
  }
  for (const auto& e : s.events) {
    if (e.type == "fill" && e.series && e.order_id) order_series_[e.order_id] = e.series;
  }
}

int SeriesNames::series_of_order(int order_id) const {
  const auto it = order_series_.find(order_id);
  return it == order_series_.end() ? 0 : it->second;
}

std::string SeriesNames::name(int series, Locale locale) const {
  const auto it = series_.find(series);
  if (it == series_.end()) return "OPT#" + std::to_string(series);
  return series_label(it->second.kind, it->second.strike, it->second.expiry, locale);
}

std::optional<Line> describe_event(const Texts& t, const EngineEvent& e, const SeriesNames& names) {
  if (e.type == "fill") {
    const std::string prefix = e.series ? names.name(e.series, t.locale()) + " " : "";
    return Line{prefix + fill_line(t, e), e.side == "buy" ? Tone::Info : Tone::Attention};
  }
  if (e.type == "option_settlement") return settlement_line(t, e, names);
  if (e.type == "order") {
    auto text = order_line(t, e, names);
    if (!text) return std::nullopt;
    return Line{*text, Tone::Muted};
  }
  if (auto money = money_line(t, e)) return money;
  if (e.type == "bankrupt") return Line{"\xE2\x98\xA0 " + t.tr("feed.bankrupt"), Tone::Down};  // ☠
  if (e.type == "run_finished") return Line{t.tr("feed.finished"), Tone::Info};
  if (e.type == "leverage_changed") {
    const long lev = std::lround(fmt::num(e.amount) * 1e8);
    return Line{t.tr("feed.leverage", {{"lev", std::to_string(lev)}}), Tone::Muted};
  }
  return std::nullopt;
}

std::string news_headline(const Texts& t, const News& n) {
  const char* tone = n.tone > 0 ? "pos" : n.tone < 0 ? "neg" : "neu";
  return t.tr_or("news.headline." + n.kind + "." + tone, n.kind);
}

std::string news_kind(const Texts& t, std::string_view kind) {
  return t.tr_or("news.kind." + std::string(kind), kind);
}

std::string news_level(const Texts& t, int value) {
  const int i = std::max(0, std::min(4, value + 2));
  return t.tr("news.level." + std::to_string(i));
}

std::string vote_mark(int vote) {
  if (vote == 0) return "\xC2\xB7";  // ·
  const std::string mark = vote > 0 ? "\xE2\x96\xB2" : "\xE2\x96\xBC";
  return std::abs(vote) >= 2 ? mark + mark : mark;
}

std::string advice_reason_line(const Texts& t, const AdviceReason& r, const std::vector<PatternHit>& patterns) {
  const std::string key = "adv.r." + r.code;
  if (r.code == "pattern") {
    const int sign = (r.vote > 0) - (r.vote < 0);
    std::string name = "?";
    for (const auto& p : patterns) {
      if (p.step == r.ref && p.bias == sign) {
        name = pattern_name(t, p.id);
        break;
      }
    }
    return t.tr(key, {{"name", name}, {"bar", std::to_string(r.ref)}});
  }
  if (r.code == "funding_crowded") return t.tr(key, {{"who", t.tr(r.ref > 0 ? "rail.longs" : "rail.shorts")}});
  return t.tr(key, {{"bar", std::to_string(r.ref)}});
}

int pct_from_bp(int bp) { return static_cast<int>(std::lround(bp / 100.0)); }

std::string pattern_name(const Texts& t, std::string_view id) {
  return t.tr_or("pattern." + std::string(id) + ".name", id);
}

std::string bp_pct(int bp, Locale locale) {
  const int digits = bp % 100 == 0 ? 0 : (bp % 10 == 0 ? 1 : 2);
  return fmt::fmt_pct(bp / 10'000.0, locale, digits);
}

std::string objective_text(const Texts& t, const Objective& o) {
  return t.tr_or("objective." + o.code, o.code, {{"pct", bp_pct(o.value_bp, t.locale())}});
}

std::string interval_label(int seconds) {
  static constexpr std::array<std::pair<int, std::string_view>, 6> kIntervals = {
      {{60, "1m"}, {300, "5m"}, {900, "15m"}, {3600, "1h"}, {14400, "4h"}, {86400, "1d"}}};
  for (const auto& [s, label] : kIntervals) {
    if (s == seconds) return std::string(label);
  }
  return std::to_string(seconds) + "s";
}

}  // namespace tradelab::desktop::game
