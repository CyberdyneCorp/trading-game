// Built-in synthetic missions for the Phase 1 vertical slice. Content will move to signed
// scenario packs (add-scenarios-and-gameplay); texts live in client i18n catalogs by mission id.

#include <algorithm>
#include <cmath>  // floor: exact

#include "tradelab/generator.hpp"

namespace tradelab {
namespace {

struct OptionChainDef {
  std::uint32_t list_offset = 0;              // decision steps after warm-up
  std::vector<std::uint32_t> expiry_offsets;  // bars after listing
  std::int32_t strikes_each_side = 2;
  double strike_step = 5.0;
};

struct MissionDef {
  MissionInfo info;
  GeneratorSpec gen;
  std::uint32_t funding_interval = 8;
  bool options = false;
  bool allow_short_options = false;
  OptionChainDef chain;
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
  // A positive rumor right before the breakout; the market usually sells the news.
  m.gen.events = {{{72, NewsKind::InstitutionalAdoption, 1, 45, 1, 1}, 0.15, 0.25, 0.004, 6, 1.6, 1.2},
                  {{82, NewsKind::RumorDenied, -1, 85, 1, -1}, 0.2, 0.7, 0.003, 5, 1.3, 1.1}};
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
  // An inflow headline at the top of the squeeze; a rate decision that disappoints.
  m.gen.events = {{{100, NewsKind::LiquidityInflow, 1, 70, 1, 1}, 0.3, 0.5, 0.002, 4, 1.2, 1.0},
                  {{112, NewsKind::RateDecision, -1, 100, 0, -1}, 0.15, 0.7, 0.004, 8, 1.6, 1.3}};
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
  // The shock arrives with an exchange incident headline (credible, very negative surprise).
  m.gen.events = {{{79, NewsKind::ExchangeIncident, -1, 90, 0, -2}, 0.05, 0.85, 0.006, 6, 1.8, 1.5}};
  m.info = {"m03-volatility-shock", total_bars(m.gen), 60, 20, true};
  return m;
}

// Options I/II: buy calls/puts around a quiet range, a breakout with an IV spike, then an IV crush.
MissionDef options_buyer() {
  MissionDef m;
  m.gen.segments = {
      {70, 0.0000, 0.0045, 0.00, 0.0, 1.0, 0.0, 1.0},
      {12, 0.0000, 0.0050, 0.00, 0.0, 1.0, 0.0, 1.35},     // event approaching: implied vol bid up
      {20, 0.0045, 0.0110, 0.05, 0.02, 2.2, 0.0002, 1.2},  // breakout
      {38, 0.0002, 0.0050, 0.01, 0.0, 1.0, 0.0, 0.75},     // IV crush after the event
  };
  // The event the implied vol was pricing: a rate decision, announced before the breakout.
  m.gen.events = {{{81, NewsKind::RateDecision, 1, 100, 0, 1}, 0.2, 0.6, 0.004, 8, 1.4, 1.0}};
  m.info = {"m04-options-buyer", total_bars(m.gen), 60, 3, true};
  m.options = true;
  m.chain = {0, {36, 76}, 2, 5.0};
  return m;
}

// Options III: writing options. Rich premium in a range, until a jump tests the margin.
MissionDef options_writer() {
  MissionDef m;
  m.gen.iv_premium = 0.35;
  m.gen.segments = {
      {80, 0.0000, 0.0050, 0.00, 0.0, 1.0, 0.0, 1.0},
      {40, 0.0000, 0.0045, 0.00, 0.0, 0.9, 0.0, 1.0},
      {3, -0.0200, 0.0200, 0.70, -0.05, 3.5, -0.0008, 1.8},  // jump
      {27, 0.0005, 0.0120, 0.05, 0.0, 1.6, 0.0, 1.3},
  };
  // A network outage is announced as the jump hits.
  m.gen.events = {{{119, NewsKind::NetworkOutage, -1, 80, 0, -2}, 0.05, 0.8, 0.005, 5, 1.6, 1.4}};
  m.info = {"m05-options-writer", total_bars(m.gen), 60, 3, true};
  m.options = true;
  m.allow_short_options = true;
  m.chain = {0, {48, 88}, 3, 5.0};
  return m;
}

std::vector<MissionDef> all_missions() {
  return {false_breakout(), funding_squeeze(), volatility_shock(), options_buyer(), options_writer()};
}

OptionSpec default_option_spec(bool allow_short) {
  OptionSpec o;
  o.multiplier = Qty::from_raw(Qty::kScale);
  o.premium_tick = Price::from_raw(Price::kScale / 100);
  o.lot_size = Qty::from_raw(Qty::kScale / 10);
  o.fee_rate = Rate::from_raw(3'000'000);           // 0.03% of underlying notional
  o.spread = Rate::from_raw(300'000'000);           // 3% half-spread on the model mid
  o.skew = Vol::from_raw(-10'000'000);              // -0.10
  o.smile = Vol::from_raw(40'000'000);              // 0.40
  o.term_slope = Vol::from_raw(15'000'000);         // 0.15
  o.im_base = Rate::from_raw(1'500'000'000);        // 15%
  o.im_floor = Rate::from_raw(1'000'000'000);       // 10%
  o.mm_base = Rate::from_raw(750'000'000);          // 7.5%
  o.allow_short = allow_short;
  return o;
}

// Strike grid centered on the close at the listing step (known at listing: no lookahead).
void add_chain(Scenario& out, const OptionChainDef& def, std::uint32_t warmup) {
  const std::uint32_t list = warmup + def.list_offset;
  const double close = static_cast<double>(out.bars[list].close.raw) / Price::kScale;
  const double center = std::floor(close / def.strike_step + 0.5) * def.strike_step;
  std::uint32_t id = 1;
  for (const std::uint32_t exp : def.expiry_offsets) {
    for (std::int32_t k = -def.strikes_each_side; k <= def.strikes_each_side; ++k) {
      const auto cents = static_cast<std::int64_t>(std::floor((center + k * def.strike_step) * 100.0 + 0.5));
      const auto strike = Price::from_raw(cents * (Price::kScale / 100));
      out.options.series.push_back({id++, OptionKind::Call, strike, list, list + exp});
      out.options.series.push_back({id++, OptionKind::Put, strike, list, list + exp});
    }
  }
}

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
  if (it->options) out.options = default_option_spec(it->allow_short_options);
  generate_market(it->gen, seed, out);
  if (it->options) add_chain(out, it->chain, it->info.warmup_bars);
  else out.options.iv_base.clear();
  return validate(out) == ScenarioError::Ok;
}

}  // namespace tradelab
