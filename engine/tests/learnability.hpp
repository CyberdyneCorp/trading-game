#pragma once

// Scripted reference players for the learnability tests (design D8.3): a run driven through the
// engine (orders, stops, trails, options, fills, costs) and per-seed outcome tallies. Shared by
// test_mission_learnability.cpp (m01-m08) and test_mission_learnability_b.cpp (m09-m12).

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/indicators.hpp"
#include "tradelab/options.hpp"
#include "tradelab/scoring.hpp"

namespace tradelab::learn {

constexpr std::uint64_t kSeeds = 60;
constexpr std::uint32_t kHorizon = 5;

inline double num(Price p) { return static_cast<double>(p.raw) / Price::kScale; }
inline double num(Money m) { return static_cast<double>(m.raw) / Money::kScale; }
inline double num(Qty q) { return static_cast<double>(q.raw) / Qty::kScale; }
inline double num(Rate r) { return static_cast<double>(r.raw) / Rate::kScale; }
inline double num(const std::optional<Price>& v) { return v ? num(*v) : 0.0; }
inline Price ticks(double x) { return Price::from_raw(static_cast<std::int64_t>(std::llround(x * 100.0)) * (Price::kScale / 100)); }
inline Qty lots(double q) { return Qty::from_raw(static_cast<std::int64_t>(std::floor(q * 10.0)) * (Qty::kScale / 10)); }
inline int sign(double x) { return (x > 0.0) - (x < 0.0); }
inline double share(int part, int whole) { return whole > 0 ? static_cast<double>(part) / whole : 0.0; }

struct Hits {
  int n = 0;
  int wins = 0;
  void add(int direction, double move) {
    if (direction == 0 || move == 0.0) return;
    ++n;
    wins += (move > 0.0) == (direction > 0) ? 1 : 0;
  }
  [[nodiscard]] double rate() const { return share(wins, n); }
};

// A run driven by a scripted player.
struct Play {
  Scenario sc;
  MissionReveal reveal;
  RunState st;
  std::uint64_t seq = 1;
  Series atr14;

  Play(const std::string& id, std::uint64_t seed) {
    REQUIRE(build_mission(id, seed, sc));
    REQUIRE(mission_reveal(id, seed, reveal));
    st = start_run(sc, seed);
    atr14 = atr(sc, static_cast<std::uint32_t>(sc.bars.size() - 1), 14);
    leverage(sc.mission.leverage_cap);
  }
  CommandResult send(Command c) {
    c.seq = seq;
    c.client_id = seq++;
    c.decision_step = st.step;
    return apply_command(sc, st, c);
  }
  void leverage(std::int32_t x) {
    Command c;
    c.type = CommandType::SetLeverage;
    c.leverage = x;
    send(c);
  }
  CommandResult order(OrderType type, OrderSide side, Qty q, Price price = {}, Price stop = {}, Price tp = {}) {
    Command c;
    c.order_type = type;
    c.side = side;
    c.qty = q;
    c.price = price;
    c.attach_stop = stop;
    c.attach_tp = tp;
    return send(c);
  }
  void trail(Price distance) {
    Command c;
    c.order_type = OrderType::TrailingStop;
    c.side = st.position.is_long() ? OrderSide::Sell : OrderSide::Buy;
    c.qty = Qty::from_raw(std::abs(st.position.qty.raw));
    c.trail_distance = distance;
    c.reduce_only = true;
    send(c);
  }
  void cancel(std::uint32_t id) {
    Command c;
    c.type = CommandType::CancelOrder;
    c.order_id = id;
    send(c);
  }
  void close_position() {
    Command c;
    c.type = CommandType::ClosePosition;
    send(c);
  }
  void step(std::uint32_t n = 1) {
    for (std::uint32_t i = 0; i < n && active(); ++i) advance(sc, st);
  }
  void finish() { step(100000); }
  [[nodiscard]] bool active() const { return st.status == RunStatus::Active; }
  [[nodiscard]] bool flat() const { return st.position.flat(); }
  [[nodiscard]] double close(std::uint32_t i) const { return num(sc.bars[i].close); }
  [[nodiscard]] double open(std::uint32_t i) const { return num(sc.bars[i].open); }
  [[nodiscard]] double atr_at(std::uint32_t i) const { return num(atr14[i]); }
  [[nodiscard]] std::uint32_t last() const { return static_cast<std::uint32_t>(sc.bars.size() - 1); }
  [[nodiscard]] double equity() const { return num(account_view(sc, st).equity); }
  [[nodiscard]] double net() const { return equity() - num(sc.mission.initial_balance); }
  [[nodiscard]] double drawdown() const { return num(st.metrics.max_drawdown); }
  [[nodiscard]] RegimeLabel label_at(std::uint32_t i) const {
    const RegimeSpan* s = span_at(reveal.spans, i);
    return s != nullptr ? s->label : RegimeLabel::Unlabeled;
  }
  [[nodiscard]] const RegimeSpan* first_span(RegimeLabel label) const {
    for (const RegimeSpan& s : reveal.spans) {
      if (s.label == label) return &s;
    }
    return nullptr;
  }
  // ---- options (m09, m10, m12) ----
  CommandResult option(std::uint32_t series, OrderSide side, Qty q) {
    Command c;
    c.type = CommandType::PlaceOptionOrder;
    c.order_type = OrderType::Market;
    c.series = series;
    c.side = side;
    c.qty = q;
    return send(c);
  }
  // Distinct expiry steps of the chain, soonest first.
  [[nodiscard]] std::vector<std::uint32_t> expiries() const {
    std::vector<std::uint32_t> out;
    for (const OptionSeries& s : sc.options.series) {
      if (std::find(out.begin(), out.end(), s.expiry_step) == out.end()) out.push_back(s.expiry_step);
    }
    std::sort(out.begin(), out.end());
    return out;
  }
  // The series of this kind and expiry whose strike is nearest `price` (offset: strikes away from it).
  [[nodiscard]] const OptionSeries* strike_near(OptionKind kind, std::uint32_t expiry, double price, int offset = 0) const {
    std::vector<const OptionSeries*> grid;
    for (const OptionSeries& s : sc.options.series) {
      if (s.kind == kind && s.expiry_step == expiry) grid.push_back(&s);
    }
    std::sort(grid.begin(), grid.end(), [](const OptionSeries* a, const OptionSeries* b) { return a->strike < b->strike; });
    std::size_t best = 0;
    for (std::size_t i = 1; i < grid.size(); ++i) {
      if (std::abs(num(grid[i]->strike) - price) < std::abs(num(grid[best]->strike) - price)) best = i;
    }
    const auto at = static_cast<std::ptrdiff_t>(best) + offset;
    if (grid.empty() || at < 0 || at >= static_cast<std::ptrdiff_t>(grid.size())) return nullptr;
    return grid[static_cast<std::size_t>(at)];
  }
  [[nodiscard]] double iv_at(const OptionSeries& s, std::uint32_t i) const {
    return static_cast<double>(quote(sc, s, i, sc.bars[i].close).iv.raw) / Vol::kScale;
  }
  // Premium paid for option buys so far (fill price x quantity).
  [[nodiscard]] double premium_paid() const {
    double paid = 0.0;
    for (const Event& e : st.events) {
      if (e.type == EventType::Fill && e.series != 0 && e.side == OrderSide::Buy) paid += num(e.price) * num(e.qty);
    }
    return paid;
  }
  [[nodiscard]] int margin_calls() const {
    return static_cast<int>(std::count_if(st.events.begin(), st.events.end(), [](const Event& e) { return e.type == EventType::MarginCall; }));
  }
};

// Per-seed results of one reference strategy.
struct Outcomes {
  std::vector<double> net;
  std::vector<double> drawdown;
  void add(const Play& p) {
    net.push_back(p.net());
    drawdown.push_back(p.drawdown());
  }
  [[nodiscard]] double positive() const {
    return share(static_cast<int>(std::count_if(net.begin(), net.end(), [](double x) { return x > 0.0; })), static_cast<int>(net.size()));
  }
  [[nodiscard]] double losing() const {
    return share(static_cast<int>(std::count_if(net.begin(), net.end(), [](double x) { return x < 0.0; })), static_cast<int>(net.size()));
  }
  [[nodiscard]] double mean() const {
    double s = 0.0;
    for (const double x : net) s += x;
    return net.empty() ? 0.0 : s / static_cast<double>(net.size());
  }
  // Mean return over mean max drawdown (equity), the "return/drawdown" of design D8.3.
  [[nodiscard]] double return_to_drawdown() const {
    double dd = 0.0;
    for (const double x : drawdown) dd += x;
    dd /= std::max<double>(1.0, static_cast<double>(drawdown.size()));
    return dd > 0.0 ? mean() / dd : 0.0;
  }
};

// 1x long from the first eligible open, held to the end (leverage only frees margin for the fees).
inline Outcomes buy_and_hold(const std::string& id) {
  Outcomes out;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Play p(id, seed);
    p.order(OrderType::Market, OrderSide::Buy, lots(10'000.0 / p.close(p.st.step)));
    p.finish();
    out.add(p);
  }
  return out;
}

}  // namespace tradelab::learn
