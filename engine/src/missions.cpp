// Built-in synthetic missions for the Phase 1 vertical slice. Content will move to signed
// scenario packs (add-scenarios-and-gameplay); texts live in client i18n catalogs by mission id.

#include <algorithm>
#include <cmath>  // floor: exact
#include <optional>

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
  // Data-only overrides (design D8.1): a mission may declare its own costs (m06's thin book) or
  // contract; absent = the defaults every other mission trades with.
  std::optional<CostModel> costs;
  std::optional<ContractSpec> contract;
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

// Segments use designated initializers in Segment's declaration order:
// bars, drift, vol, jump_prob, jump_mean, volume_mult, funding_bias, iv_mult, anchor, reversion,
// band, momentum, vol_decay, random_direction (generator v2, design D3a), label (v3: reveal only).

using enum RegimeLabel;

// A range with support/resistance, a breakout on a rumor that was already expected (no surprise,
// so no fundamental support), the breakout stalls and slips, then a credible denial gaps it back.
// The range lasts 70-82 bars depending on the seed.
MissionDef false_breakout() {
  MissionDef m;
  m.gen.lead_jitter_max = 12;
  m.gen.segments = {
      {.bars = 70, .vol = 0.0060, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.025, .label = Range},
      {.bars = 6, .drift = 0.0060, .vol = 0.0070, .volume_mult = 1.8, .funding_bias = 0.0002, .momentum = 0.1, .label = Trend},
      {.bars = 4, .vol = 0.0090, .volume_mult = 1.5, .funding_bias = 0.0003, .label = HighVol},
      {.bars = 3, .drift = -0.0030, .vol = 0.0090, .volume_mult = 1.4, .label = Trend},
      {.bars = 11, .vol = 0.0100, .jump_prob = 0.04, .jump_mean = -0.02, .volume_mult = 1.6, .label = HighVol},
      {.bars = 46, .drift = 0.0002, .vol = 0.0070, .jump_prob = 0.02, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03,
       .label = Range},
  };
  // The rumor matches expectations (surprise 0): it moves price briefly but not the fair value.
  // The denial is a credible negative surprise: most of it is priced in the next bar's gap.
  m.gen.events = {{{72, NewsKind::InstitutionalAdoption, 1, 45, 1, 1}, 0.15, 0.25, 0.004, 6, 1.6, 1.2},
                  {{82, NewsKind::RumorDenied, -1, 85, 1, -1}, 0.2, 0.6, 0.002, 5, 1.3, 1.1}};
  m.info = {"m01-false-breakout", total_bars(m.gen), 60, 5, true};
  return m;
}

// Strong uptrend with pullbacks to its average; crowded longs pay rising funding and become
// squeeze candidates (generator crowding), then the trend unwinds.
MissionDef funding_squeeze() {
  MissionDef m;
  m.gen.funding_base = 0.0003;
  m.gen.squeeze_prob = 0.04;
  m.gen.segments = {
      {.bars = 60, .drift = 0.0005, .vol = 0.0060, .funding_bias = 0.0002, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.05,
       .label = Trend},
      {.bars = 50, .drift = 0.0018, .vol = 0.0070, .jump_prob = 0.01, .jump_mean = 0.02, .volume_mult = 1.4, .funding_bias = 0.0009,
       .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.08, .label = Crowded},
      {.bars = 30, .drift = -0.0005, .vol = 0.0110, .jump_prob = 0.04, .jump_mean = -0.025, .volume_mult = 1.8, .funding_bias = -0.0004,
       .label = HighVol},
      {.bars = 20, .vol = 0.0080, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  // An inflow headline at the top of the squeeze; a rate decision that disappoints. The calm trend
  // lasts 56-70 bars depending on the seed.
  m.gen.lead_jitter_min = -4;
  m.gen.lead_jitter_max = 10;
  m.gen.events = {{{100, NewsKind::LiquidityInflow, 1, 70, 1, 1}, 0.3, 0.5, 0.002, 4, 1.2, 1.0},
                  {{112, NewsKind::RateDecision, -1, 100, 0, -1}, 0.15, 0.7, 0.004, 8, 1.6, 1.3}};
  m.info = {"m02-funding-squeeze", total_bars(m.gen), 50, 10, true};
  m.funding_interval = 4;
  return m;
}

// A calm, slowly compressing market (Bollinger squeeze), then a volatility shock with gaps:
// leverage and liquidation lesson.
MissionDef volatility_shock() {
  MissionDef m;
  m.gen.segments = {
      {.bars = 80, .drift = 0.0003, .vol = 0.0035, .volume_mult = 0.8, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02,
       .vol_decay = 0.995, .label = Calm},
      {.bars = 3, .vol = 0.0300, .jump_prob = 0.30, .jump_mean = -0.03, .volume_mult = 4.0, .funding_bias = -0.0010, .label = Shock},
      {.bars = 25, .drift = 0.0010, .vol = 0.0200, .jump_prob = 0.15, .jump_mean = 0.03, .volume_mult = 2.5, .label = HighVol},
      {.bars = 42, .drift = 0.0001, .vol = 0.0080, .jump_prob = 0.02, .volume_mult = 1.2, .anchor = Anchor::Level, .reversion = 0.006,
       .band = 0.03, .label = Range},
  };
  // The shock arrives with an exchange incident headline (credible, very negative surprise): the
  // next bar gaps down by most of it, then the aftermath is violent in both directions. The calm
  // lasts 70-90 bars depending on the seed.
  m.gen.lead_jitter_min = -10;
  m.gen.lead_jitter_max = 10;
  m.gen.events = {{{79, NewsKind::ExchangeIncident, -1, 90, 0, -2}, 0.05, 0.85, 0.003, 6, 1.8, 1.5, 0.05}};
  m.info = {"m03-volatility-shock", total_bars(m.gen), 60, 20, true};
  return m;
}

// Options I/II: a quiet range compresses before a rate decision (implied vol bid up), then a
// breakout whose direction is drawn per seed, tilted by the decision's surprise, then an IV crush.
MissionDef options_buyer() {
  MissionDef m;
  m.gen.segments = {
      {.bars = 70, .vol = 0.0045, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02, .label = Range},
      {.bars = 12, .vol = 0.0050, .iv_mult = 1.35, .anchor = Anchor::Level, .reversion = 0.01, .band = 0.015, .vol_decay = 0.97,
       .label = Calm},
      {.bars = 20, .drift = 0.0045, .vol = 0.0110, .jump_prob = 0.05, .jump_mean = 0.02, .volume_mult = 2.2, .funding_bias = 0.0002,
       .iv_mult = 1.2, .momentum = 0.1, .random_direction = true, .label = Trend},
      {.bars = 38, .drift = 0.0002, .vol = 0.0050, .jump_prob = 0.01, .iv_mult = 0.75, .anchor = Anchor::Level, .reversion = 0.006,
       .band = 0.03, .label = Range},
  };
  // The event the implied vol was pricing: a rate decision, announced before the breakout. The
  // range lasts 64-76 bars depending on the seed, so the breakout always comes before expiry 1.
  m.gen.lead_jitter_min = -6;
  m.gen.lead_jitter_max = 6;
  m.gen.events = {{{81, NewsKind::RateDecision, 1, 100, 0, 1}, 0.2, 0.6, 0.004, 8, 1.4, 1.0}};
  m.info = {"m04-options-buyer", total_bars(m.gen), 60, 3, true};
  m.options = true;
  m.chain = {0, {36, 76}, 2, 5.0};
  return m;
}

// Options III: writing options. Rich premium in a mean-reverting range, until a jump tests the margin.
MissionDef options_writer() {
  MissionDef m;
  m.gen.iv_premium = 0.35;
  m.gen.segments = {
      {.bars = 80, .vol = 0.0050, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.025, .label = Range},
      {.bars = 20, .vol = 0.0045, .volume_mult = 0.9, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.025, .label = Calm},
      {.bars = 3, .vol = 0.0300, .jump_prob = 0.30, .jump_mean = -0.03, .volume_mult = 3.5, .funding_bias = -0.0008, .iv_mult = 1.8,
       .label = Shock},
      {.bars = 47, .drift = 0.0005, .vol = 0.0120, .jump_prob = 0.05, .volume_mult = 1.6, .iv_mult = 1.3, .label = HighVol},
  };
  // A network outage is announced and the next bar gaps down by most of it. Depending on the seed
  // it lands on bars 85-101, always before the first expiry (bar 108), so the options written at
  // the start are still open.
  m.gen.lead_jitter_min = -14;
  m.gen.lead_jitter_max = 2;
  m.gen.events = {{{99, NewsKind::NetworkOutage, -1, 80, 0, -2}, 0.05, 0.8, 0.003, 5, 1.6, 1.4, 0.06}};
  m.info = {"m05-options-writer", total_bars(m.gen), 60, 3, true};
  m.options = true;
  m.allow_short_options = true;
  m.chain = {0, {48, 88}, 3, 5.0};
  return m;
}

// Structure and Orders (design D8.2 m06): a quiet range in a thin book (wide spread, slippage, 4% of
// bar volume per step), a market maker pulling quotes (an outflow headline that surprises nobody:
// costs change, value does not), a thinner and jumpier range, then liquidity returns with a modest
// positive surprise and the market leaves the range in a drawn direction, more often up. Long only
// at 2x: the campaign's first mission comes before Futures I and II.
MissionDef thin_book() {
  MissionDef m;
  m.gen.base_volume = 1500.0;
  m.gen.lead_jitter_min = -6;
  m.gen.lead_jitter_max = 8;
  m.gen.segments = {
      {.bars = 80, .vol = 0.0050, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.022, .label = Range},
      {.bars = 34, .vol = 0.0070, .jump_prob = 0.03, .jump_mean = 0.012, .volume_mult = 0.35, .anchor = Anchor::Level, .reversion = 0.008,
       .band = 0.022, .label = LowLiquidity},
      {.bars = 22, .drift = 0.0030, .vol = 0.0070, .volume_mult = 0.8, .momentum = 0.08, .random_direction = true, .label = Trend},
      {.bars = 34, .vol = 0.0055, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.025, .label = Range},
  };
  // The outflow is expected (surprise 0): it changes the book, not the fair value. The inflow is a
  // modest positive surprise of middling credibility, two bars before the market leaves the range.
  m.gen.events = {{{79, NewsKind::LiquidityOutflow, -1, 80, 0, 0}, 0.4, 0.5, 0.002, 4, 1.3, 1.1},
                  {{113, NewsKind::LiquidityInflow, 1, 60, 0, 1}, 0.3, 0.6, 0.003, 5, 1.2, 1.0}};
  m.info = {"m06-thin-book", total_bars(m.gen), 60, 2, false};
  CostModel thin;
  thin.half_spread = Rate::from_raw(10'000'000);           // 0.10%
  thin.slippage = Rate::from_raw(20'000'000);              // 0.20%
  thin.capacity_fraction = Rate::from_raw(400'000'000);    // 4% of bar volume
  m.costs = thin;
  return m;
}

// Risk sizing (design D8.2 m07): a range, a credible adoption headline that lifts the fair value,
// a trend whose direction is drawn per seed and tilted by that headline, with pullbacks toward its
// average, a choppy high-volatility extreme, then a range.
MissionDef trend_trailing() {
  MissionDef m;
  m.gen.lead_jitter_min = -6;
  m.gen.lead_jitter_max = 8;
  m.gen.segments = {
      {.bars = 72, .vol = 0.0050, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.022, .label = Range},
      {.bars = 44, .drift = 0.0025, .vol = 0.0070, .jump_prob = 0.02, .jump_mean = 0.012, .volume_mult = 1.4, .anchor = Anchor::Trend,
       .reversion = 0.03, .momentum = 0.06, .random_direction = true, .label = Trend},
      {.bars = 14, .vol = 0.0120, .jump_prob = 0.05, .jump_mean = -0.015, .volume_mult = 1.6, .label = HighVol},
      {.bars = 30, .vol = 0.0060, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  // Released at the close of the range's last bar: the trend's direction is drawn with the news in
  // the fair value (up in most seeds, never all).
  m.gen.events = {{{71, NewsKind::InstitutionalAdoption, 1, 75, 0, 1}, 0.25, 0.6, 0.003, 6, 1.3, 1.1, 0.02}};
  m.info = {"m07-trend-trailing", total_bars(m.gen), 60, 5, true};
  return m;
}

// Patterns & Indicators (design D8.2 m08): a Markov chain (G1) of quiet ranges, directional trends
// (direction drawn per visit) and choppy high volatility. No headline warns of a switch.
MissionDef regime_switch() {
  MissionDef m;
  RegimeChain c;
  c.states = {
      {.vol = 0.0060, .anchor = Anchor::Level, .reversion = 0.005, .band = 0.010, .label = Range},
      {.drift = 0.0025, .vol = 0.0060, .volume_mult = 1.3, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.05,
       .random_direction = true, .label = Trend},
      {.vol = 0.0100, .volume_mult = 1.5, .anchor = Anchor::Level, .reversion = 0.012, .band = 0.03, .label = HighVol},
  };
  c.transitions = {{0, 600, 400}, {550, 0, 450}, {550, 450, 0}};
  c.initial = {500, 300, 200};
  c.min_dwell = 25;
  c.max_dwell = 50;
  c.bars = 180;
  c.at = 0;
  m.gen.chain = c;
  m.info = {"m08-regime-switch", 180, 60, 5, true};
  return m;
}

std::vector<MissionDef> all_missions() {
  return {false_breakout(), funding_squeeze(), volatility_shock(), options_buyer(), options_writer(),
          thin_book(),      trend_trailing(),  regime_switch()};
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

std::string market_alias(std::uint64_t seed) {
  return "K" + std::to_string(1 + seed % 9) + static_cast<char>('A' + (seed / 9) % 26);
}

std::vector<MissionInfo> mission_catalog() {
  std::vector<MissionInfo> out;
  for (const MissionDef& m : all_missions()) out.push_back(m.info);
  return out;
}

namespace {

std::optional<MissionDef> find_mission(const std::string& id) {
  for (MissionDef& m : all_missions()) {
    if (m.info.id == id) return std::move(m);
  }
  return std::nullopt;
}

}  // namespace

bool build_mission(const std::string& id, std::uint64_t seed, Scenario& out) {
  const std::optional<MissionDef> it = find_mission(id);
  if (!it) return false;
  out = Scenario{};
  out.id = id;
  out.alias = market_alias(seed);
  out.contract = it->contract.value_or(default_contract());
  out.contract.funding_interval = it->funding_interval;
  out.costs = it->costs.value_or(default_costs());
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

// Regenerates the market (same spec, seed and streams as build_mission) to read back what the run
// could not see: the regime spans as played and each event's outcome and sampled reaction.
bool mission_reveal(const std::string& id, std::uint64_t seed, MissionReveal& out) {
  const std::optional<MissionDef> def = find_mission(id);
  if (!def) return false;
  out = MissionReveal{};
  Scenario sc;
  sc.contract = def->contract.value_or(default_contract());  // same tick grid as the played market
  generate_market(def->gen, seed, sc, &out.spans);
  const GeneratorSpec played = scheduled(expanded(def->gen, seed), seed);
  for (std::uint32_t e = 0; e < played.events.size(); ++e) {
    const NewsItem& n = played.events[e].item;
    const auto surprise = static_cast<std::int8_t>(n.actual - n.expectation);
    out.events.push_back({n.release_step, n.kind, n.actual, surprise, 100, static_cast<std::int8_t>(sample_reaction(played.events[e], seed, e))});
  }
  return true;
}

}  // namespace tradelab
