#include "model/engine_run.hpp"

#include <tradelab/c_api.h>

namespace tradelab::desktop {

namespace {

json::Value parse_or_null(const char* text) {
  if (text == nullptr) return {};
  auto v = json::parse(text);
  return v ? *v : json::Value{};
}

int to_int(const json::Value& v) { return static_cast<int>(v.as_int()); }

std::vector<Bar> bars_from(const json::Value& bars) {
  std::vector<Bar> out;
  out.reserve(bars.size());
  for (const auto& b : bars.items()) {
    out.push_back(Bar{b[0].as_double(), b[1].as_double(), b[2].as_double(), b[3].as_double(), b[4].as_double()});
  }
  return out;
}

void read_contract(Snapshot& s, const json::Value& c) {
  s.leverage_cap = to_int(c["leverageCap"]);
  s.allow_short = c["allowShort"].as_bool(true);
  s.tick_size = c["tickSize"].as_string();
  s.lot_size = c["lotSize"].as_string();
  s.min_qty = c["minQty"].as_string();
}

void read_account(Snapshot& s, const json::Value& a, const json::Value& p) {
  s.equity = a["equity"].as_string();
  s.wallet = a["wallet"].as_string();
  s.available = a["available"].as_string();
  s.unrealized = a["unrealized"].as_string();
  s.mark = a["mark"].as_string();
  if (!a["liquidationPrice"].is_null()) s.liquidation_price = a["liquidationPrice"].as_string();
  s.position_qty = p["qty"].as_string();
  s.position_entry = p["entry"].as_string();
}

}  // namespace

std::vector<MissionInfo> engine_missions() {
  std::vector<MissionInfo> out;
  const json::Value list = parse_or_null(tl_missions());
  for (const auto& m : list.items()) {
    out.push_back(MissionInfo{m["id"].as_string(), to_int(m["totalBars"]), to_int(m["warmupBars"]),
                              to_int(m["leverageCap"]), m["allowShort"].as_bool(true)});
  }
  return out;
}

Snapshot snapshot_from_state(const json::Value& state) {
  Snapshot s;
  s.mission = state["mission"].as_string();
  s.alias = state["alias"].as_string();
  s.status = state["status"].as_string();
  s.step = to_int(state["step"]);
  s.warmup_bars = to_int(state["warmupBars"]);
  s.total_bars = to_int(state["totalBars"]);
  s.last_decision_step = to_int(state["lastDecisionStep"]);
  s.step_seconds = to_int(state["stepSeconds"]);
  s.initial_balance = state["initialBalance"].as_string();
  s.leverage = to_int(state["leverage"]);
  read_contract(s, state["contract"]);
  s.bars = bars_from(state["bars"]);
  read_account(s, state["account"], state["position"]);
  s.digest = state["digest"].as_string();
  s.state = state;
  return s;
}

Debrief debrief_from_json(const json::Value& d) {
  Debrief out;
  out.final_equity = d["finalEquity"].as_string();
  out.net_pnl = d["netPnl"].as_string();
  out.buy_and_hold_pnl = d["buyAndHoldPnl"].as_string();
  out.score_total = static_cast<int>(d["score"]["total"].as_int(-1));
  out.lesson = d["lesson"].as_string();
  out.entries = to_int(d["entries"]);
  out.advisor_points_earned = to_int(d["advisorPointsEarned"]);
  out.raw = d;
  return out;
}

// tl_run_create(m, s) is tl_run_create_ex(m, s, 0, 0, 0, NULL, 0) (c_api.h), so default options start the
// same run.
std::unique_ptr<EngineRun> EngineRun::create(const std::string& mission, std::uint32_t seed, const RunOptions& o) {
  const char* balance = o.starting_balance.empty() ? nullptr : o.starting_balance.c_str();
  tl_run* run = tl_run_create_ex(mission.c_str(), seed, static_cast<std::uint32_t>(o.advice_allowance),
                                 static_cast<std::uint32_t>(o.news_lead), o.calendar ? 1 : 0, balance,
                                 o.leverage_cap);
  if (run == nullptr) return nullptr;
  auto out = std::unique_ptr<EngineRun>(new EngineRun(run, seed));
  const json::Value lesson = parse_or_null(tl_lesson(mission.c_str()));
  if (!lesson.has("error")) out->lesson_ = lesson_info_from_json(lesson);
  return out;
}

EngineRun::~EngineRun() { tl_run_destroy(run_); }

std::string EngineRun::state_json() const { return tl_state(run_); }

std::string EngineRun::debrief_json() const { return tl_debrief(run_); }

bool EngineRun::advance(int steps) {
  // The returned state starts with {"mission":..,"alias":..,"status":".."}: a substring test avoids
  // parsing every intermediate state of a batch.
  const std::string_view state = tl_advance(run_, steps);
  return state.substr(0, 256).find("\"status\":\"active\"") != std::string_view::npos;
}

CommandResult EngineRun::place_market(Side side, const std::string& qty) {
  const json::Value r =
      parse_or_null(tl_place_order(run_, next_client_id_++, 0, static_cast<int32_t>(side), qty.c_str(), nullptr, 0));
  return CommandResult{CommandKind::Market, r["accepted"].as_bool(false), r["reason"].as_string("invalid")};
}

Snapshot EngineRun::snapshot() const {
  const json::Value state = parse_or_null(tl_state(run_));
  Snapshot s = snapshot_from_state(state);
  s.seed = seed_;
  auto game = std::make_shared<GameState>(game_state_from_json(state));
  game->lesson = lesson_;
  s.game = std::move(game);
  if (!s.active()) {
    s.debrief = debrief_from_json(parse_or_null(tl_debrief(run_)));
    // The seeded reveal describes the run's future: asked for only once it has ended (design D6).
    const json::Value reveal = parse_or_null(tl_mission_reveal(s.mission.c_str(), seed_));
    if (!reveal.has("error")) s.debrief->reveal = reveal;
  }
  return s;
}

}  // namespace tradelab::desktop
