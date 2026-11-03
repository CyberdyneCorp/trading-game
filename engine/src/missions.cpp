// Built-in synthetic missions for the Phase 1 vertical slice. Content will move to signed
// scenario packs (add-scenarios-and-gameplay); texts live in client i18n catalogs by mission id.

#include <algorithm>

#include "tradelab/generator.hpp"

namespace tradelab {
namespace {

struct MissionDef {
  MissionInfo info;
  GeneratorSpec gen;
  std::uint32_t funding_interval = 8;
};

ContractSpec default_contract() {
  ContractSpec c;
  c.multiplier = Qty::from_raw(Qty::kScale);
  c.tick_size = Price::from_raw(Price::kScale / 100);  // 0.01
  c.lot_size = Qty::from_raw(Qty::kScale / 10);        // 0.1
  c.min_qty = c.lot_size;
  c.max_leverage = 50;
  c.maker_fee = Rate::from_raw(2'000'000);         // 0.02%
  c.taker_fee = Rate::from_raw(5'000'000);         // 0.05%
  c.maintenance_margin = Rate::from_raw(50'000'000);  // 0.5%
  return c;
}

CostModel default_costs() {
  CostModel k;
  k.half_spread = Rate::from_raw(1'000'000);   // 0.01%
  k.slippage = Rate::from_raw(2'000'000);      // 0.02%
  k.capacity_fraction = Rate::from_raw(1'000'000'000);  // 10% of bar volume
  return k;
}

std::uint32_t total_bars(const GeneratorSpec& g) {
  std::uint32_t n = 0;
  for (const Segment& s : g.segments) n += s.bars;
  return n;
}

// The spec's example: a range, a breakout that fails, then a sharp reversal.
MissionDef false_breakout() {
  MissionDef m;
  m.gen.segments = {
      {70, 0.0000, 0.0060, 0.00, 0.0, 1.0, 0.0},      // range (warm-up + early decisions)
      {6, 0.0060, 0.0070, 0.00, 0.0, 1.8, 0.0002},    // breakout
      {4, 0.0000, 0.0090, 0.00, 0.0, 1.5, 0.0003},    // stall, crowded longs pay funding
      {14, -0.0070, 0.0100, 0.08, -0.025, 2.0, 0.0},  // failure
      {46, 0.0002, 0.0070, 0.02, 0.0, 1.0, 0.0},      // chop
  };
  m.info = {"m01-false-breakout", total_bars(m.gen), 60, 5, true};
  return m;
}

// Strong uptrend where holding a long costs a lot of funding.
MissionDef funding_squeeze() {
  MissionDef m;
  m.gen.funding_base = 0.0003;
  m.gen.segments = {
      {60, 0.0005, 0.0060, 0.00, 0.0, 1.0, 0.0002},
      {50, 0.0018, 0.0070, 0.01, 0.02, 1.4, 0.0009},
      {30, -0.0010, 0.0110, 0.05, -0.03, 1.8, -0.0004},
      {20, 0.0000, 0.0080, 0.00, 0.0, 1.0, 0.0},
  };
  m.info = {"m02-funding-squeeze", total_bars(m.gen), 50, 10, true};
  m.funding_interval = 4;
  return m;
}

// Calm market, then a volatility shock with gaps: leverage and liquidation lesson.
MissionDef volatility_shock() {
  MissionDef m;
  m.gen.segments = {
      {80, 0.0003, 0.0035, 0.00, 0.0, 0.8, 0.0},
      {3, -0.0150, 0.0250, 0.60, -0.06, 4.0, -0.0010},
      {25, 0.0010, 0.0200, 0.15, 0.03, 2.5, 0.0},
      {42, 0.0001, 0.0080, 0.02, 0.0, 1.2, 0.0},
  };
  m.info = {"m03-volatility-shock", total_bars(m.gen), 60, 20, true};
  return m;
}

std::vector<MissionDef> all_missions() { return {false_breakout(), funding_squeeze(), volatility_shock()}; }

}  // namespace

std::vector<MissionInfo> mission_catalog() {
  std::vector<MissionInfo> out;
  for (const MissionDef& m : all_missions()) out.push_back(m.info);
  return out;
}

bool build_mission(const std::string& id, std::uint64_t seed, Scenario& out) {
  const auto defs = all_missions();
  const auto it = std::find_if(defs.begin(), defs.end(), [&id](const MissionDef& m) { return m.info.id == id; });
  if (it == defs.end()) return false;
  out = Scenario{};
  out.id = id;
  out.alias = "K" + std::to_string(1 + seed % 9) + static_cast<char>('A' + (seed / 9) % 26);
  out.contract = default_contract();
  out.contract.funding_interval = it->funding_interval;
  out.costs = default_costs();
  out.mission.initial_balance = Money::from_raw(10'000 * Money::kScale);
  out.mission.leverage_cap = it->info.leverage_cap;
  out.mission.warmup_bars = it->info.warmup_bars;
  out.mission.allow_short = it->info.allow_short;
  generate_market(it->gen, seed, out);
  return validate(out) == ScenarioError::Ok;
}

}  // namespace tradelab
