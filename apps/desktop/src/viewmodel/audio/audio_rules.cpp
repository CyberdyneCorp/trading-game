#include "viewmodel/audio/audio_rules.hpp"

#include <algorithm>
#include <cmath>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::audio {

namespace {

bool near_liquidation(const GameState& g, double k) {
  const auto& liq = g.account.liquidation;
  const double mark = g.account.mark;
  if (!liq || g.position.qty == 0 || mark <= 0) return false;
  return std::abs(mark - *liq) / mark < kLiqDanger * k;
}

bool thin_option_margin(const GameState& g, double k) {
  const double mm = g.options.maintenance_margin;
  return mm > 0 && g.account.equity < mm * kMarginDanger * k;
}

bool recent_alert(const GameState& g) {
  return std::any_of(g.events.begin(), g.events.end(), [&](const EngineEvent& e) {
    return (e.type == "margin_call" || e.type == "liquidation") && g.step - e.step < kAlertBars;
  });
}

// A fill with realized P&L closed (part of) a position; settlements close option positions.
double realized(const EngineEvent& e) {
  if (e.type == "fill" || e.type == "option_settlement") return fmt::num(e.pnl);
  return 0;
}

}  // namespace

bool in_danger(const GameState& g, bool was_tense) {
  const double k = was_tense ? kRelief : 1;
  return g.status == "active" && (near_liquidation(g, k) || thin_option_margin(g, k) || recent_alert(g));
}

Mood mood_for(bool terminal, const GameState* g, std::optional<Mood> previous) {
  if (!terminal || g == nullptr) return Mood::Menu;
  return in_danger(*g, previous == Mood::Tense) ? Mood::Tense : Mood::Game;
}

std::optional<Sting> sting_for(const std::vector<EngineEvent>& events, int after_seq) {
  double pnl = 0;
  bool closed = false;
  for (const auto& e : events) {
    if (e.seq <= after_seq) continue;
    if (e.type == "liquidation") return Sting::Loss;
    const double r = realized(e);
    if (r != 0) {
      pnl += r;
      closed = true;
    }
  }
  if (!closed) return std::nullopt;
  return pnl > 0 ? Sting::Win : Sting::Loss;
}

std::optional<Sting> StingTracker::observe(const std::string& run_id, const std::vector<EngineEvent>& events) {
  const int last = events.empty() ? 0 : events.back().seq;
  if (run_id != run_) {
    run_ = run_id;
    heard_ = last;
    return std::nullopt;
  }
  const auto sting = sting_for(events, heard_);
  heard_ = std::max(heard_, last);
  return sting;
}

}  // namespace tradelab::desktop::audio
