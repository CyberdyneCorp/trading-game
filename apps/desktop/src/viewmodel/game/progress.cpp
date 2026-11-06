#include "viewmodel/game/progress.hpp"

#include <algorithm>
#include <set>

namespace tradelab::desktop::game {

namespace {
constexpr std::size_t kKeepSettled = 100;
int clamp_points(int n) { return std::max(0, std::min(kMaxPoints, n)); }
}  // namespace

int allowance_for(const GamePrefs& p) { return clamp_points(p.advisor_points); }

bool settle_run(GamePrefs& p, const std::string& run_id, int used, int earned) {
  if (run_id.empty() || std::find(p.settled_runs.begin(), p.settled_runs.end(), run_id) != p.settled_runs.end()) {
    return false;
  }
  p.advisor_points = clamp_points(p.advisor_points - used + earned);
  p.settled_runs.push_back(run_id);
  if (p.settled_runs.size() > kKeepSettled) {
    p.settled_runs.erase(p.settled_runs.begin(), p.settled_runs.end() - static_cast<std::ptrdiff_t>(kKeepSettled));
  }
  return true;
}

int distinct_finished(const std::vector<std::string>& finished, const std::vector<std::string>& built_in) {
  std::set<std::string> seen;
  for (const auto& m : finished) {
    if (std::find(built_in.begin(), built_in.end(), m) != built_in.end()) seen.insert(m);
  }
  return static_cast<int>(seen.size());
}

std::vector<Perk> unlocked_perks(int finished_count) {
  std::vector<Perk> out;
  for (const auto& p : kPerks) {
    if (finished_count >= p.missions) out.push_back(p.id);
  }
  return out;
}

RunOptions perk_options(const std::vector<Perk>& enabled, int advice_allowance) {
  RunOptions o;
  o.advice_allowance = advice_allowance;
  for (const auto& p : kPerks) {
    if (std::find(enabled.begin(), enabled.end(), p.id) == enabled.end()) continue;
    if (p.id == Perk::Calendar) o.calendar = true;
    o.news_lead = std::max(o.news_lead, p.lead);
  }
  return o;
}

std::vector<Perk> perks_used(const RunOptions& o) {
  std::vector<Perk> used;
  if (o.calendar) used.push_back(Perk::Calendar);
  for (const auto& p : kPerks) {
    if (p.lead > 0 && p.lead == o.news_lead) used.push_back(p.id);
  }
  return used;
}

std::vector<Perk> new_perks(const std::vector<Perk>& unlocked, const std::vector<std::string>& introduced) {
  std::vector<Perk> out;
  for (const Perk id : unlocked) {
    const std::string name(kPerks[static_cast<std::size_t>(id)].name);
    if (std::find(introduced.begin(), introduced.end(), name) == introduced.end()) out.push_back(id);
  }
  return out;
}

std::string_view pause_reason_id(PauseReason r) {
  switch (r) {
    case PauseReason::Fill: return "fill";
    case PauseReason::Liquidation: return "liquidation";
    case PauseReason::MarginCall: return "margin_call";
    case PauseReason::OptionSettlement: return "option_settlement";
    case PauseReason::News: return "news";
    case PauseReason::Finished: break;
  }
  return "finished";
}

bool news_arrived(const GameState& g) {
  return std::any_of(g.news.begin(), g.news.end(), [&](const News& n) {
    return n.step == g.step || (n.early && n.step - g.advisor.news_lead == g.step);
  });
}

std::optional<PauseReason> notable(const GameState& g) {
  for (const auto& e : g.events) {
    if (e.step != g.step) continue;
    if (e.type == "fill") return PauseReason::Fill;
    if (e.type == "liquidation") return PauseReason::Liquidation;
    if (e.type == "margin_call") return PauseReason::MarginCall;
    if (e.type == "option_settlement") return PauseReason::OptionSettlement;
  }
  if (news_arrived(g)) return PauseReason::News;
  return std::nullopt;
}

int steps_until_next_question(const GameState& g, int n) {
  const int every = g.calibration.every;
  const int w = g.warmup_bars;
  if (g.step < w || every <= 0) return n;
  const int next = w + ((g.step - w) / every + 1) * every;
  if (next + g.calibration.horizon >= g.total_bars) return n;
  return std::min(n, next - g.step);
}

void AutoPlay::start(bool finished) {
  if (running || finished) return;
  running = true;
  paused.reset();
  elapsed_ = 0;
}

void AutoPlay::stop(std::optional<PauseReason> reason) {
  running = false;
  paused = reason;
  elapsed_ = 0;
}

void AutoPlay::toggle(bool finished) {
  if (running) stop();
  else start(finished);
}

void AutoPlay::set_speed(int index) {
  speed = std::min(std::max(0, index), static_cast<int>(kAutoSpeeds.size()) - 1);
}

bool AutoPlay::tick(double dt) {
  if (!running) return false;
  elapsed_ += dt;
  if (elapsed_ < kAutoSpeeds[static_cast<std::size_t>(speed)].seconds) return false;
  elapsed_ = 0;
  return true;
}

void AutoPlay::after_advance(const GameState& g) {
  if (!running) return;
  if (!g.active()) return stop(PauseReason::Finished);
  if (!pause_on_events) return;
  if (const auto reason = notable(g)) stop(reason);
}

}  // namespace tradelab::desktop::game
