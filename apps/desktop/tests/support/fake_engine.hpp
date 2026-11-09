// Fake engine and hand-made terminal states for viewmodel tests: commands are logged as text, the test
// answers with snapshots it builds.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "model/engine_host.hpp"
#include "model/game_command.hpp"
#include "model/snapshot.hpp"

namespace tradelab::desktop::testing {

inline std::string optional_part(const std::string& prefix, const std::string& value) {
  return value.empty() ? std::string() : prefix + value;
}

inline std::string describe_order(const GameCommand& c) {
  return "order " + std::to_string(c.type) + " " + (c.buy ? "buy" : "sell") + " " + c.qty + optional_part(" @", c.price) +
         (c.reduce_only ? " ro" : "") + optional_part(" sl ", c.stop) + optional_part(" tp ", c.tp) +
         optional_part(" trail ", c.trail);
}

inline std::string describe_untagged(const GameCommand& c);

inline std::string describe_tif(const GameCommand& c) {
  if (c.tif.empty() || c.tif == "gtc") return {};
  return " tif " + c.tif + (c.tif == "bars" ? ":" + std::to_string(c.tif_bars) : "");
}

inline std::string describe_tag(const GameCommand& c) {
  if (c.combo.empty()) return {};
  return " [" + c.combo + " " + c.strategy + optional_part(" roll ", c.roll) + optional_part(" adjust ", c.adjust) + "]";
}

// A strategy-builder leg names its combo: "option 9 buy 1.0 [c1 iron_condor]"; a time in force follows
// the order: "order 1 buy 10 @100 tif ioc".
inline std::string describe(const GameCommand& c) { return describe_untagged(c) + describe_tif(c) + describe_tag(c); }

inline std::string describe_untagged(const GameCommand& c) {
  const std::string side = c.buy ? "buy" : "sell";
  switch (c.kind) {
    case GameCommandKind::Order: return describe_order(c);
    case GameCommandKind::TrailingRate: return "trail-rate " + side + " " + c.qty + " " + c.trail;
    case GameCommandKind::Option:
      return "option " + std::to_string(c.id) + " " + side + " " + c.qty + (c.price.empty() ? "" : " @" + c.price);
    case GameCommandKind::Cancel: return "cancel " + std::to_string(c.id);
    case GameCommandKind::Move: return "move " + std::to_string(c.id) + " " + c.price;
    case GameCommandKind::Leverage: return "leverage " + std::to_string(c.number);
    case GameCommandKind::Close: return "close";
    case GameCommandKind::Margin: return "margin " + c.amount;
    case GameCommandKind::Plan: return std::string("plan ") + (c.skipped ? "skipped" : "inv " + c.stop + " risk " + c.amount);
    case GameCommandKind::Answer: return c.skipped ? "answer skipped" : "answer " + std::to_string(c.number);
    case GameCommandKind::Lesson: return "lesson " + std::to_string(c.id) + (c.skipped ? " skipped" : " " + std::to_string(c.number) + " " + c.amount);
    case GameCommandKind::Advice: return "advice";
    case GameCommandKind::LinkOco: return "oco " + std::to_string(c.id) + " " + std::to_string(c.number);
    case GameCommandKind::None: break;
  }
  return "?";
}

struct FakeEngine final : EngineCommands {
  std::vector<std::string> log;
  std::vector<GameCommand> commands;
  void start(const std::string& mission, std::uint32_t seed) override {
    log.push_back("start " + mission + " " + std::to_string(seed));
  }
  void start_with(const std::string& mission, std::uint32_t seed, const RunOptions& o) override {
    log.push_back("start " + mission + " " + std::to_string(seed) +
                  (o.advice_allowance || o.news_lead || o.calendar
                       ? " opts " + std::to_string(o.advice_allowance) + "/" + std::to_string(o.news_lead) + "/" +
                             (o.calendar ? "cal" : "-")
                       : ""));
  }
  void advance(int steps) override { log.push_back("advance " + std::to_string(steps)); }
  void market(Side side, const std::string& qty) override {
    log.push_back(std::string(side == Side::Buy ? "buy " : "sell ") + qty);
  }
  void game(const GameCommand& c) override {
    commands.push_back(c);
    log.push_back(describe(c));
  }
  RunLinks last_links;
  RunOptions last_options;
  void start_linked(const std::string& mission, std::uint32_t seed, const RunOptions& o, const RunLinks& links) override {
    last_links = links;
    last_options = o;
    start_with(mission, seed, o);
  }
  void start_pack(const std::string& digest, std::uint32_t seed) override {
    log.push_back("start-pack " + digest + " " + std::to_string(seed));
  }
  void rewind(int to_step, const RunLinks& links) override {
    last_links = links;
    log.push_back("rewind " + std::to_string(to_step));
  }
  void resume() override { log.push_back("resume"); }
  // Report-only engine calls wait here for the test to answer them (reply), in order.
  struct Analysis {
    AnalysisKind kind = AnalysisKind::WhatIf;
    std::string request;
    AnalysisReply reply;
  };
  std::vector<Analysis> analyses;
  // What-ifs of the run's holdings (the portfolio refreshed on every state, add-practice-and-advanced-
  // strategies D9) wait in their own list, so the tests of other analyses count only theirs.
  std::vector<Analysis> holdings;
  void analyze(AnalysisKind kind, const std::string& request, AnalysisReply reply) override {
    const bool held = request.find("\"holdings\":true") != std::string::npos;
    (held ? holdings : analyses).push_back({kind, request, std::move(reply)});
  }
};

// A terminal state at the first decision step of a 140-bar mission: price ~100, flat, 10,000 cr.
inline GameState base_state(int step = 60) {
  GameState g;
  g.mission = "m01-false-breakout";
  g.alias = "K2A";
  g.status = "active";
  g.step = step;
  g.warmup_bars = 60;
  g.total_bars = 140;
  g.last_decision_step = 138;
  g.initial_balance = 10'000;
  g.leverage = 1;
  g.contract.tick = 0.01;
  g.contract.lot = 0.1;
  g.contract.min_qty = 0.1;
  g.contract.leverage_cap = 5;
  g.contract.max_leverage = 50;
  g.contract.taker_fee = 0.0005;
  g.contract.maintenance_margin = 0.005;
  g.contract.half_spread = 0.0001;
  g.contract.slippage = 0.0002;
  g.contract.capacity_fraction = 0.1;
  g.rules.leverage_cap = 5;
  g.rules.confirm_stop_beyond_liq = true;
  g.rules.liq_warning_bp = 300;
  for (int i = 0; i <= step; ++i) {
    const double p = 100 + (i % 10) * 0.2;
    g.bars.push_back({p, p + 1, p - 1, p + 0.1, 50, true, false});
  }
  g.indicators.atr14.assign(g.bars.size(), 1.5);
  g.account.wallet = g.account.equity = g.account.available = 10'000;
  g.account.mark = g.bars.back().close;
  g.position.qty_text = "0.00000000";
  g.calibration.every = 15;
  g.calibration.horizon = 5;
  g.advisor.allowance = 3;
  g.advisor.left = 3;
  return g;
}

inline std::shared_ptr<const Snapshot> snapshot_of(const GameState& g, CommandResult r, std::uint32_t seed = 42) {
  auto s = std::make_shared<Snapshot>();
  s->mission = g.mission;
  s->alias = g.alias;
  s->status = g.status;
  s->seed = seed;
  s->step = g.step;
  s->warmup_bars = g.warmup_bars;
  s->total_bars = g.total_bars;
  s->last_decision_step = g.last_decision_step;
  s->initial_balance = "10000.00000000";
  s->leverage_cap = g.contract.leverage_cap;
  s->lot_size = "0.10000000";
  s->tick_size = "0.01000000";
  s->equity = std::to_string(g.account.equity);
  s->available = std::to_string(g.account.available);
  s->position_qty = g.position.qty_text;
  s->position_entry = std::to_string(g.position.entry);
  s->results.push_back(std::move(r));
  s->game = std::make_shared<GameState>(g);
  if (!g.active()) s->debrief = Debrief{"9998.98228227", "-1.01771773", "-85.79694750", 47, "risk", 1, 0};
  return s;
}

inline CommandResult game_result(GameCommandKind kind, bool accepted = true, std::string reason = "none", int order_id = 0) {
  CommandResult r{CommandKind::Game, accepted, std::move(reason)};
  r.game = kind;
  r.order_id = order_id;
  return r;
}

}  // namespace tradelab::desktop::testing
