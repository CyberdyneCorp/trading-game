// Built-in synthetic missions for the Phase 1 vertical slice. Content will move to signed
// scenario packs (add-scenarios-and-gameplay); texts live in client i18n catalogs by mission id.

#include <algorithm>
#include <cmath>  // floor: exact
#include <cstdlib>
#include <optional>

#include "tradelab/generator.hpp"
#include "tradelab/options.hpp"

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
  // Mission schema (task 4.1): objectives the briefing lists, the liquidation-distance warning and the
  // drawdown budget the risk score reads. Defaults: the common objectives, 3% and 20%.
  std::vector<Objective> objectives = common_objectives();
  std::uint32_t liq_warning_bp = 300;
  std::uint32_t drawdown_budget_bp = 2000;
  // Defined-risk spread margin (add-academy-and-strategies design D5): declared by m13, m14, m16 and
  // m17 only, so every earlier mission keeps per-leg margin.
  bool spread_margin = false;
  // Calendar margin and risk limits (add-practice-and-advanced-strategies D5, D9): per-mission data;
  // no mission before m18 declares them, so every earlier mission plays exactly as before.
  bool calendar_margin = false;
  RiskLimits risk_limits;
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
  c.maintenance_margin = Rate::from_raw(50'000'000);  // 0.5% below 50,000 notional
  // Policy v16: larger positions keep more maintenance margin and allow less leverage.
  const auto credits = [](std::int64_t n) { return Money::from_raw(n * Money::kScale); };
  c.margin_tiers = {{credits(50'000), Rate::from_raw(100'000'000), 20},     // 1%, 20x
                    {credits(250'000), Rate::from_raw(250'000'000), 10},    // 2.5%, 10x
                    {credits(1'000'000), Rate::from_raw(500'000'000), 5}};  // 5%, 5x
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
  m.info = {"m01-false-breakout", total_bars(m.gen), 60, 5, true, {}};
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
  m.info = {"m02-funding-squeeze", total_bars(m.gen), 50, 10, true, {}};
  m.objectives = {{ObjectiveCode::PlanEveryEntry, 0}, {ObjectiveCode::RiskPerIdea, 200}, {ObjectiveCode::LiqDistance, 1000}};
  m.liq_warning_bp = 1000;  // objective: keep the distance to liquidation above 10%
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
  m.info = {"m03-volatility-shock", total_bars(m.gen), 60, 20, true, {}};
  m.objectives = {{ObjectiveCode::LeverageFromStop, 0}, {ObjectiveCode::LiquidationLosesMargin, 0}, {ObjectiveCode::BankruptcyGameOver, 0}};
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
  m.info = {"m04-options-buyer", total_bars(m.gen), 60, 3, true, {}};
  m.objectives = {{ObjectiveCode::OptionLoseScenario, 0}, {ObjectiveCode::PremiumVsBreakeven, 0}, {ObjectiveCode::WatchIvEvent, 0}};
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
  m.info = {"m05-options-writer", total_bars(m.gen), 60, 3, true, {}};
  m.objectives = {{ObjectiveCode::WriterWorstCase, 0}, {ObjectiveCode::MaintenanceBuffer, 0}, {ObjectiveCode::ThetaForSeller, 0}};
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
  m.info = {"m06-thin-book", total_bars(m.gen), 60, 2, false, {}};
  m.objectives = {{ObjectiveCode::LimitOrdersAtZones, 0}, {ObjectiveCode::RiskPerIdea, 100}, {ObjectiveCode::UnfilledLimitOk, 0}};
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
  m.info = {"m07-trend-trailing", total_bars(m.gen), 60, 5, true, {}};
  m.objectives = {{ObjectiveCode::RiskFromStop, 100}, {ObjectiveCode::TrailNotTighterThanAtr, 0}, {ObjectiveCode::DrawdownBudget, 1000}};
  m.drawdown_budget_bp = 1000;  // objective: finish without a drawdown above 10%
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
  m.info = {"m08-regime-switch", 180, 60, 5, true, {}};
  m.objectives = {{ObjectiveCode::RegimeInPlan, 0}, {ObjectiveCode::OneToolPerRegime, 0}, {ObjectiveCode::RiskPerIdea, 100}};
  return m;
}

// Drawn-outcome weights (G2), per mille for the actual -2..+2.
using Weights = std::array<std::uint32_t, 5>;

// Options II (design D8.2 m09): a rate decision announced 20 bars ahead with a consensus of
// "unchanged" (G3). Implied vol is bid while the range compresses; the decision is drawn (G2): as
// expected in 40% of seeds, a mild surprise in 40%, a strong one in 20%, with a drawn size. The move
// after it leans on the surprise; IV collapses once it is out. Options buying only.
MissionDef event_straddle() {
  MissionDef m;
  m.gen.lead_jitter_min = -3;
  m.gen.lead_jitter_max = 3;
  m.gen.segments = {
      {.bars = 63, .vol = 0.0045, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02, .label = Range},
      {.bars = 12, .vol = 0.0045, .iv_mult = 1.45, .anchor = Anchor::Level, .reversion = 0.01, .band = 0.015, .vol_decay = 0.97,
       .label = Calm},
      {.bars = 20, .drift = 0.0008, .vol = 0.0055, .volume_mult = 1.4, .iv_mult = 0.9, .momentum = 0.05, .random_direction = true,
       .label = Trend},
      {.bars = 55, .vol = 0.0050, .iv_mult = 0.8, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  m.gen.events = {{.item = {74, NewsKind::RateDecision, 0, 90, 0, 0, 20},
                   .p_none = 0.3,
                   .p_align = 0.6,
                   .drift = 0.003,
                   .duration = 6,
                   .vol_mult = 1.3,
                   .iv_mult = 0.85,
                   .fair_impact = 0.03,
                   .actual_weights = Weights{100, 200, 400, 200, 100},
                   .intensity_min = 0.6,
                   .intensity_max = 1.4}};
  m.info = {"m09-event-straddle", total_bars(m.gen), 60, 3, true, {}};
  m.objectives = {{ObjectiveCode::StraddleBreakevens, 0}, {ObjectiveCode::StraddleLoseScenario, 0}, {ObjectiveCode::PremiumBudget, 200}};
  m.options = true;
  m.chain = {0, {25, 70}, 3, 2.5};
  return m;
}

// Options III (design D8.2 m10): a steady uptrend with crowded longs paying funding every 4 bars;
// an unannounced regulation rumor whose bar moves with the seed (sometimes after the first expiry)
// and whose severity is drawn (G2): in 20% of seeds it changes nothing. Then chop and a range.
MissionDef hedged_book() {
  MissionDef m;
  m.gen.funding_base = 0.0001;
  m.gen.squeeze_prob = 0.0;
  m.gen.lead_jitter_min = -8;
  m.gen.lead_jitter_max = 20;
  m.gen.segments = {
      {.bars = 88, .drift = 0.0010, .vol = 0.0060, .volume_mult = 1.2, .funding_bias = 0.0009, .anchor = Anchor::Trend, .reversion = 0.03,
       .momentum = 0.05, .label = Crowded},
      {.bars = 20, .vol = 0.0110, .jump_prob = 0.03, .jump_mean = 0.015, .volume_mult = 1.8, .iv_mult = 1.2, .label = HighVol},
      {.bars = 28, .vol = 0.0070, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  m.gen.events = {{.item = {87, NewsKind::RegulationRumor, -1, 85, 0, -2},
                   .p_none = 0.2,
                   .p_align = 0.7,
                   .drift = 0.003,
                   .duration = 6,
                   .vol_mult = 1.8,
                   .iv_mult = 1.8,
                   .fair_impact = 0.075,
                   .actual_weights = Weights{450, 350, 200, 0, 0},
                   .intensity_min = 0.7,
                   .intensity_max = 1.3}};
  m.info = {"m10-hedged-book", total_bars(m.gen), 60, 5, true, {}};
  m.objectives = {{ObjectiveCode::StructureMaxLoss, 0}, {ObjectiveCode::WrittenMarginBuffer, 0}, {ObjectiveCode::FundingVsTheta, 0}};
  m.funding_interval = 4;
  m.options = true;
  m.allow_short_options = true;
  m.chain = {0, {30, 75}, 3, 5.0};
  return m;
}

// The three regime states the late missions draw their path from (G1).
Segment range_state() { return {.vol = 0.0055, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.015, .label = Range}; }
Segment trend_state() {
  return {.drift = 0.0020, .vol = 0.0065, .volume_mult = 1.3, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.05,
          .random_direction = true, .label = Trend};
}
Segment chop_state() {
  return {.vol = 0.0100, .volume_mult = 1.5, .anchor = Anchor::Level, .reversion = 0.012, .band = 0.03, .label = HighVol};
}

// Events (design D8.2 m11): three scheduled releases announced 15-25 bars ahead with a consensus,
// landing in whatever regime the drawn path (G1) is in, and one unannounced exchange incident that
// is real in half the seeds. Actuals are drawn around the consensus (G2).
MissionDef news_calendar() {
  MissionDef m;
  RegimeChain c;
  c.states = {range_state(), trend_state(), chop_state()};
  c.transitions = {{0, 600, 400}, {550, 0, 450}, {550, 450, 0}};
  c.initial = {500, 300, 200};
  c.min_dwell = 30;
  c.max_dwell = 60;
  c.bars = 180;
  m.gen.chain = c;
  // A closing range after the chain absorbs the per-seed lead, so no release lands on a fixed bar.
  m.gen.segments = {{.bars = 20, .vol = 0.0055, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02, .label = Range}};
  m.gen.lead_jitter_min = -10;
  m.gen.lead_jitter_max = 10;
  m.gen.events = {
      {.item = {84, NewsKind::MacroData, 1, 90, 1, 1, 15},
       .p_none = 0.25, .p_align = 0.6, .drift = 0.002, .duration = 5, .vol_mult = 1.4, .iv_mult = 1.0, .fair_impact = 0.035,
       .actual_weights = Weights{0, 50, 250, 450, 250}, .intensity_min = 0.7, .intensity_max = 1.3},
      {.item = {112, NewsKind::ExchangeIncident, -1, 85, 0, -1},
       .p_none = 0.2, .p_align = 0.7, .drift = 0.002, .duration = 5, .vol_mult = 1.6, .iv_mult = 1.0, .fair_impact = 0.035,
       .actual_weights = Weights{250, 250, 500, 0, 0}, .intensity_min = 0.7, .intensity_max = 1.3},
      {.item = {134, NewsKind::RateDecision, 0, 100, 0, 0, 20},
       .p_none = 0.25, .p_align = 0.5, .drift = 0.002, .duration = 5, .vol_mult = 1.4, .iv_mult = 1.0, .fair_impact = 0.035,
       .actual_weights = Weights{100, 200, 400, 200, 100}, .intensity_min = 0.7, .intensity_max = 1.3},
      {.item = {176, NewsKind::NetworkUpgrade, 1, 85, 1, 1, 25},
       .p_none = 0.25, .p_align = 0.6, .drift = 0.002, .duration = 5, .vol_mult = 1.4, .iv_mult = 1.0, .fair_impact = 0.035,
       .actual_weights = Weights{0, 0, 350, 500, 150}, .intensity_min = 0.7, .intensity_max = 1.3},
  };
  m.info = {"m11-news-calendar", 200, 60, 5, true, {}};
  m.objectives = {{ObjectiveCode::DecideBeforeRelease, 0}, {ObjectiveCode::NoChasingHeadline, 0}, {ObjectiveCode::RiskWithGap, 100}};
  return m;
}

// Evaluation (design D8.2 m12): everything at once on a drawn path (G1) of ranges, trends, chop, a
// thin book and a crowded-short range where longs collect funding; an announced rate decision, an
// unannounced outage that changes nothing in 60% of seeds, and a liquidity outflow.
MissionDef capstone() {
  MissionDef m;
  RegimeChain c;
  Segment thin = range_state();
  thin.vol = 0.0070;
  thin.volume_mult = 0.3;
  thin.jump_prob = 0.03;
  thin.jump_mean = 0.012;
  thin.label = LowLiquidity;
  Segment crowded = range_state();
  crowded.funding_bias = -0.0008;
  crowded.band = 0.02;
  crowded.label = Crowded;
  Segment trend = trend_state();
  trend.drift = 0.0030;
  c.states = {range_state(), trend, chop_state(), thin, crowded};
  c.transitions = {{0, 400, 250, 150, 200}, {350, 0, 300, 150, 200}, {350, 300, 0, 150, 200}, {400, 300, 150, 0, 150},
                   {300, 400, 200, 100, 0}};
  c.initial = {400, 200, 100, 100, 200};
  c.min_dwell = 25;
  c.max_dwell = 50;
  c.bars = 180;
  c.at = 1;
  m.gen.segments = {{.bars = 30, .vol = 0.0055, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.015, .label = Range},
                    {.bars = 20, .vol = 0.0055, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02, .label = Range}};
  m.gen.lead_jitter_min = -10;
  m.gen.lead_jitter_max = 10;
  m.gen.chain = c;
  m.gen.events = {
      {.item = {118, NewsKind::RateDecision, 0, 100, 0, 0, 20},
       .p_none = 0.25, .p_align = 0.55, .drift = 0.002, .duration = 5, .vol_mult = 1.4, .iv_mult = 0.85, .fair_impact = 0.035,
       .actual_weights = Weights{100, 200, 400, 200, 100}, .intensity_min = 0.7, .intensity_max = 1.3},
      {.item = {150, NewsKind::LiquidityOutflow, -1, 80, 0, 0},
       .p_none = 0.4, .p_align = 0.5, .drift = 0.002, .duration = 4, .vol_mult = 1.3, .iv_mult = 1.1},
      {.item = {182, NewsKind::NetworkOutage, -1, 85, 0, -2},
       .p_none = 0.1, .p_align = 0.8, .drift = 0.003, .duration = 5, .vol_mult = 1.6, .iv_mult = 1.4, .fair_impact = 0.04,
       .actual_weights = Weights{250, 150, 600, 0, 0}, .intensity_min = 0.7, .intensity_max = 1.3},
  };
  m.info = {"m12-capstone", 230, 60, 10, true, {}};
  m.objectives = {{ObjectiveCode::FinishMission, 0}, {ObjectiveCode::PlanAndStopEveryEntry, 0}, {ObjectiveCode::NoLiquidationNoMarginCall, 0}, {ObjectiveCode::ReadCampaignReport, 0}};
  m.options = true;
  m.allow_short_options = true;
  m.chain = {0, {40, 100, 160}, 3, 5.0};
  return m;
}

// ---- Strategies (add-academy-and-strategies design D8): m13-m17 --------------------------------
// Common: hourly synthetic bars, warm-up 60, the perpetual at 3x, the default option spec with
// defined-risk spread margin where the mission writes (m13, m14, m16, m17), the chain listed at the
// first decision with 2.5 strike steps. Existing generator capabilities only (generator v5).

MissionDef strategy_base(const char* id, std::uint32_t bars) {
  MissionDef m;
  m.info = {id, bars, 60, 3, true, {}};
  m.options = true;
  m.allow_short_options = true;
  m.spread_margin = true;
  return m;
}

// m13 Spread: options priced rich in a quiet range; a credible adoption headline of drawn strength
// tilts a drift of drawn direction (up in most seeds), then a range. Bull call spread against a
// naked call. The headline lands on bars 67-77, the first expiry on bar 100.
MissionDef call_spread() {
  MissionDef m = strategy_base("m13-call-spread", 151);
  m.gen.lead_jitter_min = -4;
  m.gen.lead_jitter_max = 6;
  m.gen.segments = {
      {.bars = 72, .vol = 0.0045, .iv_mult = 1.3, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02, .label = Range},
      {.bars = 26, .drift = 0.0006, .vol = 0.0055, .jump_prob = 0.03, .jump_mean = 0.01, .volume_mult = 1.4, .momentum = 0.05,
       .random_direction = true, .label = Trend},
      {.bars = 53, .vol = 0.0055, .jump_prob = 0.02, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  m.gen.events = {{.item = {71, NewsKind::InstitutionalAdoption, 1, 70, 0, 1},
                   .p_none = 0.3,
                   .p_align = 0.55,
                   .drift = 0.0015,
                   .duration = 6,
                   .vol_mult = 1.3,
                   .iv_mult = 1.0,
                   .fair_impact = 0.02,
                   .intensity_min = 0.4,
                   .intensity_max = 1.8}};
  m.objectives = {{ObjectiveCode::SpreadBreakevens, 0}, {ObjectiveCode::PremiumBudget, 200}, {ObjectiveCode::CoverShortLeg, 0}};
  m.chain = {0, {40, 90}, 4, 2.5};
  return m;
}

// m14 Income: a market that mostly grinds up and ranges; in about a third of the seeds an
// unannounced adoption headline sets off a strong rally (G2: actual 0 in 65%, +2 in 35%), on a bar
// drawn per seed. Short expiries every 24 bars: a covered call against long only.
MissionDef covered_call() {
  MissionDef m = strategy_base("m14-covered-call", 181);
  m.gen.lead_jitter_min = -24;
  m.gen.lead_jitter_max = 24;
  m.gen.segments = {
      {.bars = 96, .drift = 0.00003, .vol = 0.0050, .funding_bias = 0.0002, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.03,
       .label = Trend},
      // No anchor right after the headline: a rally keeps the level the news gave it.
      {.bars = 24, .vol = 0.0050, .momentum = 0.03, .label = Range},
      {.bars = 61, .vol = 0.0050, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  // A reaction without surprise has no side: half of the non-neutral ones go each way.
  m.gen.events = {{.item = {95, NewsKind::InstitutionalAdoption, 1, 85, 0, 0},
                   .p_none = 0.5,
                   .p_align = 0.5,
                   .drift = 0.0015,
                   .duration = 12,
                   .vol_mult = 2.0,
                   .iv_mult = 0.6,
                   .fair_impact = 0.05,
                   .actual_weights = Weights{0, 0, 650, 0, 350},
                   .intensity_min = 0.8,
                   .intensity_max = 1.3}};
  m.objectives = {{ObjectiveCode::CappedMaxProfit, 0}, {ObjectiveCode::PremiumVsFunding, 0}, {ObjectiveCode::RiskFromStop, 100}};
  // Six strikes each side (not three): calls about 5% out of the money stay listed for every roll.
  m.chain = {0, {24, 48, 72, 96, 120}, 6, 2.5};
  return m;
}

// m15 Insurance: a rising market, then a range; an unannounced exchange incident of drawn severity
// (G2: severe 30%, mild 30%, harmless 40%) on a bar drawn per seed, always before the second expiry.
// Buying options only: the lesson is the buyer's.
MissionDef protective_put() {
  MissionDef m = strategy_base("m15-protective-put", 161);
  m.allow_short_options = false;
  m.spread_margin = false;
  m.gen.iv_premium = 0.25;  // insurance is priced above what the market usually realizes
  m.gen.lead_jitter_min = -22;
  m.gen.lead_jitter_max = 22;
  m.gen.segments = {
      {.bars = 100, .drift = 0.0009, .vol = 0.0055, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.04, .label = Trend},
      {.bars = 12, .vol = 0.0090, .volume_mult = 1.5, .label = HighVol},
      {.bars = 49, .vol = 0.0060, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  m.gen.events = {{.item = {99, NewsKind::ExchangeIncident, -1, 90, 0, -2},
                   .p_none = 0.1,
                   .p_align = 0.8,
                   .drift = 0.003,
                   .duration = 6,
                   .vol_mult = 1.8,
                   .iv_mult = 1.5,
                   .fair_impact = 0.04,
                   .actual_weights = Weights{300, 300, 400, 0, 0},
                   .intensity_min = 0.8,
                   .intensity_max = 1.3}};
  m.objectives = {{ObjectiveCode::FloorBeforeBuying, 0}, {ObjectiveCode::PremiumBudget, 200}, {ObjectiveCode::RecordTenor, 0}};
  m.chain = {0, {40, 100}, 4, 2.5};
  return m;
}

// m16 Premium: a long quiet range with implied volatility priced above what it realizes, then a
// breakout of drawn direction whose bar (78-103) falls before or after the first expiry (bar 90);
// half the seeds see an unannounced regulation rumor at the break (G2: harmless in the others).
MissionDef range_condor() {
  MissionDef m = strategy_base("m16-range-condor", 131);
  m.gen.lead_jitter_min = -10;
  m.gen.lead_jitter_max = 15;
  // The rumor lands as the breakout starts: a weak tilt keeps its direction from being a free read of the headline.
  m.gen.tilt = 8.0;
  m.gen.iv_premium = 0.35;  // options priced well above what the range realizes
  m.gen.segments = {
      {.bars = 88, .vol = 0.0040, .iv_mult = 1.35, .anchor = Anchor::Level, .reversion = 0.008, .band = 0.02, .label = Range},
      {.bars = 18, .drift = 0.0070, .vol = 0.0120, .jump_prob = 0.12, .jump_mean = 0.03, .volume_mult = 2.0, .iv_mult = 1.3,
       .momentum = 0.03, .random_direction = true, .label = Trend},
      {.bars = 25, .vol = 0.0070, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  m.gen.events = {{.item = {87, NewsKind::RegulationRumor, -1, 80, 0, -1},
                   .p_none = 0.3,
                   .p_align = 0.5,
                   .drift = 0.002,
                   .duration = 5,
                   .vol_mult = 1.5,
                   .iv_mult = 1.3,
                   .fair_impact = 0.04,
                   .actual_weights = Weights{200, 300, 500, 0, 0},
                   .intensity_min = 0.8,
                   .intensity_max = 1.2}};
  m.objectives = {{ObjectiveCode::SizeByMaxLoss, 200}, {ObjectiveCode::BreakoutExitRule, 0}, {ObjectiveCode::MaintenanceBuffer, 0}};
  m.chain = {0, {30, 70}, 5, 2.5};
  return m;
}

// m17 Expiry: a drawn path (G1) of ranges, trends and chop, with an expiry every 25 bars so a
// position always has a near and a next expiry: managing before expiry (close, roll or hold).
MissionDef before_expiry() {
  MissionDef m = strategy_base("m17-before-expiry", 156);
  RegimeChain c;
  c.states = {range_state(), trend_state(), chop_state()};
  c.transitions = {{0, 600, 400}, {550, 0, 450}, {550, 450, 0}};
  c.initial = {500, 300, 200};
  c.min_dwell = 20;
  c.max_dwell = 40;
  c.bars = 156;
  m.gen.chain = c;
  m.objectives = {{ObjectiveCode::DecideBeforeExpiry, 0}, {ObjectiveCode::NoRollWithoutThesis, 0}, {ObjectiveCode::RiskPerIdea, 100}};
  // Six strikes each side (not three): the drawn path can trend away from the listing price, and the
  // next expiry still needs a strike at the money for a roll.
  m.chain = {0, {20, 45, 70, 95}, 6, 2.5};
  return m;
}

// ---- Advanced options and carry, management, psychology (add-practice-and-advanced-strategies D11,
// D12): m18-m25. Common: hourly synthetic bars, warm-up 60, the perpetual at 3x (m19 and m22 at 5x),
// funding every 8 bars, existing generator capabilities only (generator v5).

MissionDef advanced_base(const char* id, std::uint32_t bars, std::int32_t leverage) {
  MissionDef m;
  m.info = {id, bars, 60, leverage, true, {}};
  return m;
}

// m18 Hedge: a drawn path (G1) of chop that moves more than its options price in (realized above
// implied: iv_mult 0.75) and quiet stretches priced above what they realize (iv_mult 1.25). Long
// visits, so most seeds sit mainly in one of them. Buying options only: a long straddle, hedged.
MissionDef gamma_scalp() {
  MissionDef m = advanced_base("m18-gamma-scalp", 181, 3);
  RegimeChain c;
  c.states = {{.vol = 0.0110, .volume_mult = 1.4, .iv_mult = 0.75, .label = HighVol},
              {.vol = 0.0045, .volume_mult = 0.8, .iv_mult = 1.25, .label = Calm}};
  c.transitions = {{0, 1000}, {1000, 0}};
  c.initial = {500, 500};
  c.min_dwell = 70;
  c.max_dwell = 140;
  c.bars = 121;
  c.at = 1;
  m.gen.chain = c;
  m.gen.segments = {{.bars = 60, .vol = 0.0070, .anchor = Anchor::Level, .reversion = 0.01, .band = 0.025, .label = Range}};
  m.objectives = {{ObjectiveCode::DeltaWithinLimit, 1500}, {ObjectiveCode::RehedgeRuleFirst, 0}, {ObjectiveCode::PremiumBudget, 300}};
  m.options = true;
  m.allow_short_options = false;
  m.risk_limits.delta_bp = 1500;
  m.chain = {0, {40, 80}, 4, 2.5};
  return m;
}

// m19 Carry: longs are crowded and pay funding (shorts collect it). A drawn path (G1) starts crowded;
// in some seeds the crowd unwinds before the end: funding turns negative (shorts pay) and squeezes
// hit the short side. Buying and writing, spread margin; the perpetual at 5x.
MissionDef funding_carry() {
  MissionDef m = advanced_base("m19-funding-carry", 181, 5);
  m.gen.funding_base = 0.0002;
  m.gen.squeeze_prob = 0.08;
  m.gen.squeeze_size = 0.02;
  RegimeChain c;
  c.states = {{.drift = 0.0002, .vol = 0.0050, .volume_mult = 1.2, .funding_bias = 0.0005, .anchor = Anchor::Trend, .reversion = 0.03,
               .momentum = 0.03, .label = Crowded},
              {.drift = 0.0015, .vol = 0.0110, .volume_mult = 1.8, .funding_bias = -0.0012, .momentum = 0.03, .label = HighVol}};
  c.transitions = {{0, 1000}, {1000, 0}};
  c.initial = {1000, 0};
  c.min_dwell = 40;
  c.max_dwell = 200;
  c.bars = 121;
  c.at = 1;
  m.gen.chain = c;
  m.gen.segments = {{.bars = 60, .drift = 0.0003, .vol = 0.0050, .funding_bias = 0.0004, .anchor = Anchor::Trend, .reversion = 0.03,
                     .momentum = 0.03, .label = Crowded}};
  m.objectives = {{ObjectiveCode::CarryBreakevenFirst, 0}, {ObjectiveCode::LiqDistance, 1500}, {ObjectiveCode::DeltaWithinLimit, 1000}};
  m.liq_warning_bp = 1500;
  m.options = true;
  m.allow_short_options = true;
  m.spread_margin = true;
  m.risk_limits.delta_bp = 1000;
  m.chain = {0, {60, 120}, 4, 2.5};
  return m;
}

// m20 Announcement: a release announced 35 bars ahead (G3). Implied volatility is bid in two steps
// before it (iv_mult 1.3, then 1.6) and collapses at the release (the event's iv_mult 0.6 over its
// duration on top of 0.8); the actual is drawn (G2) and the move after it leans on the surprise. The
// first expiry falls 7-13 bars after the release.
MissionDef event_vol() {
  MissionDef m = advanced_base("m20-event-vol", 161, 3);
  m.gen.lead_jitter_min = -3;
  m.gen.lead_jitter_max = 3;
  m.gen.segments = {
      {.bars = 85, .vol = 0.0050, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02, .label = Range},
      {.bars = 8, .vol = 0.0050, .iv_mult = 1.4, .anchor = Anchor::Level, .reversion = 0.01, .band = 0.015, .vol_decay = 0.95,
       .label = Calm},
      {.bars = 8, .vol = 0.0040, .iv_mult = 1.8, .anchor = Anchor::Level, .reversion = 0.01, .band = 0.015, .vol_decay = 0.95,
       .label = Calm},
      {.bars = 20, .drift = 0.0015, .vol = 0.0070, .volume_mult = 1.5, .iv_mult = 0.8, .momentum = 0.05, .random_direction = true,
       .label = Trend},
      {.bars = 40, .vol = 0.0055, .iv_mult = 0.8, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  m.gen.events = {{.item = {100, NewsKind::MacroData, 0, 90, 0, 0, 35},
                   .p_none = 0.2,
                   .p_align = 0.7,
                   .drift = 0.003,
                   .duration = 6,
                   .vol_mult = 1.5,
                   .iv_mult = 0.6,
                   .fair_impact = 0.025,
                   .actual_weights = Weights{100, 200, 400, 200, 100},
                   .intensity_min = 0.6,
                   .intensity_max = 1.5}};
  m.objectives = {{ObjectiveCode::StraddleBreakevens, 0}, {ObjectiveCode::HoldThroughDecision, 0}, {ObjectiveCode::VegaWithinLimit, 150}};
  m.options = true;
  m.allow_short_options = true;
  m.spread_margin = true;
  m.risk_limits.vega_bp = 150;
  m.chain = {0, {50, 100}, 5, 2.5};
  return m;
}

// m21 Calendar: a range with steady implied volatility, then a breakout of drawn direction whose bar
// (66-96) falls before or after the near expiry (bar 80). Calendar
// margin: a far long covers a near short of the same kind.
MissionDef calendar_spread() {
  MissionDef m = advanced_base("m21-calendar", 131, 3);
  m.gen.lead_jitter_min = -15;
  m.gen.lead_jitter_max = 15;
  m.gen.segments = {
      {.bars = 81, .vol = 0.0045, .anchor = Anchor::Level, .reversion = 0.008, .band = 0.02, .label = Range},
      {.bars = 16, .drift = 0.0060, .vol = 0.0090, .jump_prob = 0.06, .jump_mean = 0.02, .volume_mult = 1.8, .momentum = 0.05,
       .random_direction = true, .label = Trend},
      {.bars = 34, .vol = 0.0060, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  m.objectives = {{ObjectiveCode::NetDebitFirst, 0}, {ObjectiveCode::DecideBeforeExpiry, 0}, {ObjectiveCode::GammaWithinLimit, 50}};
  m.options = true;
  m.allow_short_options = true;
  m.spread_margin = true;
  m.calendar_margin = true;
  m.risk_limits.gamma_bp = 50;
  m.chain = {0, {20, 45, 70}, 5, 2.5};
  return m;
}

// m22 Management: a range, then an unannounced headline of drawn sign and strength (G2) starts a
// trend that follows it with pullbacks; the second leg is drawn again, tilted by the fair value, so
// some seeds trend far, some stall and some fail. The perpetual at 5x, no options.
MissionDef scale_manage() {
  MissionDef m = advanced_base("m22-scale-manage", 180, 5);
  m.gen.lead_jitter_min = -8;
  m.gen.lead_jitter_max = 8;
  m.gen.tilt = 12.0;
  m.gen.segments = {
      {.bars = 70, .vol = 0.0050, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02, .label = Range},
      {.bars = 35, .drift = 0.0012, .vol = 0.0065, .volume_mult = 1.4, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.04,
       .random_direction = true, .label = Trend},
      {.bars = 35, .drift = 0.0015, .vol = 0.0075, .jump_prob = 0.02, .jump_mean = 0.012, .volume_mult = 1.3, .anchor = Anchor::Trend,
       .reversion = 0.03, .momentum = 0.03, .random_direction = true, .label = Trend},
      {.bars = 40, .vol = 0.0060, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  m.gen.events = {{.item = {69, NewsKind::InstitutionalAdoption, 0, 75, 0, 0},
                   .p_none = 0.3,
                   .p_align = 0.6,
                   .drift = 0.002,
                   .duration = 5,
                   .vol_mult = 1.3,
                   .iv_mult = 1.0,
                   .fair_impact = 0.03,
                   .actual_weights = Weights{250, 250, 0, 250, 250},
                   .intensity_min = 0.6,
                   .intensity_max = 1.6}};
  m.objectives = {{ObjectiveCode::TotalRiskAllEntries, 100}, {ObjectiveCode::PartialExitOrTrail, 0}, {ObjectiveCode::NeverStopAway, 0}};
  return m;
}

// m23 Restart: a range, a clean-looking breakout up on volume that fails fast (a drop through the
// range on an unannounced denial), chop, then a real move of drawn direction later.
MissionDef second_chance() {
  MissionDef m = advanced_base("m23-second-chance", 180, 3);
  m.gen.lead_jitter_min = -8;
  m.gen.lead_jitter_max = 8;
  m.gen.segments = {
      {.bars = 70, .vol = 0.0050, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02, .label = Range},
      {.bars = 5, .drift = 0.0050, .vol = 0.0060, .volume_mult = 2.0, .funding_bias = 0.0002, .momentum = 0.1, .label = Trend},
      {.bars = 3, .drift = -0.0070, .vol = 0.0100, .jump_prob = 0.10, .jump_mean = -0.015, .volume_mult = 1.8, .label = HighVol},
      {.bars = 42, .vol = 0.0090, .volume_mult = 1.3, .anchor = Anchor::Level, .reversion = 0.012, .band = 0.025, .label = HighVol},
      {.bars = 25, .drift = 0.0035, .vol = 0.0065, .volume_mult = 1.4, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.06,
       .random_direction = true, .label = Trend},
      {.bars = 35, .vol = 0.0060, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  m.gen.events = {{.item = {75, NewsKind::RumorDenied, -1, 80, 0, -1},
                   .p_none = 0.2,
                   .p_align = 0.45,
                   .drift = 0.002,
                   .duration = 3,
                   .vol_mult = 1.5,
                   .iv_mult = 1.0,
                   .fair_impact = 0.01,
                   .jump_prob = 0.25,
                   .jump_size = 0.012}};
  m.objectives = {{ObjectiveCode::CooldownAfterStop, 0}, {ObjectiveCode::RiskPerIdea, 100}, {ObjectiveCode::PlanEveryEntry, 0}};
  return m;
}

// m24 Rally: a quiet range, then a steep rally on heavy volume and extreme funding (crowded longs, so
// squeezes against them become possible); a drawn path (G1, one visit) decides the next leg: it gives
// the rally back in about 70% of seeds and keeps going in about 30%.
MissionDef fast_market() {
  MissionDef m = advanced_base("m24-fast-market", 180, 3);
  m.gen.lead_jitter_min = -8;
  m.gen.lead_jitter_max = 8;
  m.gen.squeeze_prob = 0.06;
  const Segment reversal{.drift = -0.0030, .vol = 0.0110, .jump_prob = 0.06, .jump_mean = -0.02, .volume_mult = 2.0,
                         .funding_bias = 0.0004, .momentum = 0.03, .label = HighVol};
  Segment continuation = reversal;
  continuation.drift = 0.0040;
  continuation.jump_mean = 0.02;
  RegimeChain c;
  c.states = {reversal, continuation};
  c.transitions = {{0, 1000}, {1000, 0}};
  c.initial = {700, 300};
  c.min_dwell = 100;
  c.max_dwell = 100;
  c.bars = 25;
  c.at = 2;
  m.gen.chain = c;
  m.gen.segments = {
      {.bars = 90, .vol = 0.0045, .volume_mult = 0.8, .anchor = Anchor::Level, .reversion = 0.012, .band = 0.012, .vol_decay = 0.995,
       .label = Range},
      {.bars = 14, .drift = 0.0075, .vol = 0.0080, .volume_mult = 2.6, .funding_bias = 0.0012, .momentum = 0.12, .label = Crowded},
      {.bars = 51, .vol = 0.0070, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03, .label = Range},
  };
  m.objectives = {{ObjectiveCode::PlanBeforeEntryAtrStop, 0}, {ObjectiveCode::RiskPerIdea, 100}, {ObjectiveCode::NoLiquidationWaitOk, 0}};
  return m;
}

// m25 Dip: a range, then an uptrend with sharp drops along the way (dips that recover); a drawn path
// (G1, one visit) decides the rest: the uptrend with its dips goes on in about 70% of seeds, and in
// about 30% one drop does not recover and the market turns down for good.
MissionDef the_dip() {
  MissionDef m = advanced_base("m25-the-dip", 180, 3);
  m.gen.lead_jitter_min = -8;
  m.gen.lead_jitter_max = 8;
  const Segment dips{.drift = 0.0016, .vol = 0.0070, .jump_prob = 0.05, .jump_mean = -0.019, .volume_mult = 1.3,
                     .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.03, .label = Trend};
  RegimeChain c;
  Segment down = dips;
  down.drift = -0.0030;
  down.jump_mean = 0.015;
  down.vol = 0.0095;
  down.label = HighVol;
  c.states = {dips, down};
  c.transitions = {{0, 1000}, {1000, 0}};
  c.initial = {700, 300};
  c.min_dwell = 200;
  c.max_dwell = 200;
  c.bars = 60;
  c.at = 2;
  m.gen.chain = c;
  Segment first = dips;
  first.bars = 56;
  m.gen.segments = {{.bars = 64, .vol = 0.0050, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02, .label = Range}, first};
  m.objectives = {{ObjectiveCode::NoAddBeyondPlan, 0}, {ObjectiveCode::RiskPerIdea, 100}, {ObjectiveCode::PlanAndStopEveryEntry, 0}};
  return m;
}

// ---- Round markets of "guess the next move" (add-practice-and-advanced-strategies D19) ---------------
// Practice markets, not missions: listed with practice "round", no rubric, no story to spoil. A round
// reads a chart at a drawn decision step in [warm-up + 20, total - 13] and judges the next 12 bars, so
// each market mixes regimes whose next leg no single indicator gives away (the pooled realism bands).
// Existing generator capabilities only (generator v5).

MissionDef round_base(const char* id) {
  MissionDef m;
  m.info = {id, 0, 60, 3, true, "round"};
  return m;
}

// r01 Trend: a drawn path (G1) of trends of drawn direction, pullbacks to the average and pauses.
MissionDef round_trend() {
  MissionDef m = round_base("r01-trend");
  RegimeChain c;
  c.states = {
      {.drift = 0.0022, .vol = 0.0060, .volume_mult = 1.3, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.05,
       .random_direction = true, .label = Trend},
      {.vol = 0.0055, .anchor = Anchor::Level, .reversion = 0.008, .band = 0.015, .label = Range},
      {.vol = 0.0095, .volume_mult = 1.4, .anchor = Anchor::Level, .reversion = 0.012, .band = 0.03, .label = HighVol},
  };
  c.transitions = {{0, 600, 400}, {700, 0, 300}, {700, 300, 0}};
  c.initial = {500, 300, 200};
  c.min_dwell = 15;
  c.max_dwell = 40;
  c.bars = 160;
  c.at = 0;
  m.gen.chain = c;
  m.info.total_bars = 160;
  return m;
}

// r02 Range: support and resistance with false breaks (jumps of drawn direction that the range takes
// back) and, in some seeds, a break of drawn direction.
MissionDef round_range() {
  MissionDef m = round_base("r02-range");
  RegimeChain c;
  c.states = {
      {.vol = 0.0055, .jump_prob = 0.04, .jump_mean = 0.013, .anchor = Anchor::Level, .reversion = 0.010, .band = 0.018,
       .random_direction = true, .label = Range},
      {.vol = 0.0080, .jump_prob = 0.05, .jump_mean = 0.015, .volume_mult = 1.3, .anchor = Anchor::Level, .reversion = 0.015, .band = 0.025,
       .random_direction = true, .label = HighVol},
      {.drift = 0.0025, .vol = 0.0065, .volume_mult = 1.4, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.05,
       .random_direction = true, .label = Trend},
  };
  c.transitions = {{0, 650, 350}, {750, 0, 250}, {800, 200, 0}};
  c.initial = {700, 200, 100};
  c.min_dwell = 20;
  c.max_dwell = 50;
  c.bars = 160;
  c.at = 0;
  m.gen.chain = c;
  m.info.total_bars = 160;
  return m;
}

// r03 Breakout: a tightening range, then a break of drawn direction at a drawn bar that follows
// through or fails back into the range (a one-visit chain).
MissionDef round_breakout() {
  MissionDef m = round_base("r03-breakout");
  m.gen.lead_jitter_min = -15;
  m.gen.lead_jitter_max = 15;
  const Segment follow{.drift = 0.0025, .vol = 0.0070, .volume_mult = 1.6, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.06,
                       .random_direction = true, .label = Trend};
  const Segment fail{.vol = 0.0085, .volume_mult = 1.3, .anchor = Anchor::Level, .reversion = 0.015, .band = 0.02, .label = HighVol};
  RegimeChain c;
  c.states = {follow, fail};
  c.transitions = {{0, 1000}, {1000, 0}};
  c.initial = {550, 450};
  c.min_dwell = 200;
  c.max_dwell = 200;
  c.bars = 55;
  c.at = 2;
  m.gen.chain = c;
  m.gen.segments = {
      {.bars = 90, .vol = 0.0050, .volume_mult = 0.9, .anchor = Anchor::Level, .reversion = 0.010, .band = 0.015, .vol_decay = 0.995,
       .label = Range},
      {.bars = 5, .drift = 0.0040, .vol = 0.0070, .volume_mult = 2.0, .momentum = 0.08, .random_direction = true, .label = Trend},
  };
  m.info.total_bars = 150;
  return m;
}

// r04 Event: an announced release (G3) with a drawn outcome and size (G2), in a range; the reaction
// leans on the surprise but is often faded.
MissionDef round_event() {
  MissionDef m = round_base("r04-event");
  m.gen.lead_jitter_min = -10;
  m.gen.lead_jitter_max = 10;
  m.gen.segments = {
      {.bars = 95, .vol = 0.0050, .anchor = Anchor::Level, .reversion = 0.008, .band = 0.02, .label = Range},
      {.bars = 15, .drift = 0.0006, .vol = 0.0070, .volume_mult = 1.4, .momentum = 0.04, .random_direction = true, .label = Trend},
      {.bars = 50, .vol = 0.0060, .anchor = Anchor::Level, .reversion = 0.008, .band = 0.03, .label = Range},
  };
  m.gen.events = {{.item = {94, NewsKind::MacroData, 0, 85, 0, 0, 15},
                   .p_none = 0.35,
                   .p_align = 0.6,
                   .drift = 0.003,
                   .duration = 5,
                   .vol_mult = 1.4,
                   .fair_impact = 0.03,
                   .actual_weights = Weights{100, 200, 400, 200, 100},
                   .intensity_min = 0.6,
                   .intensity_max = 1.4}};
  m.info.total_bars = 160;
  return m;
}

std::vector<MissionDef> all_missions() {
  return {false_breakout(), funding_squeeze(), volatility_shock(), options_buyer(),     options_writer(), thin_book(),
          trend_trailing(), regime_switch(),   event_straddle(),   hedged_book(),       news_calendar(),  capstone(),
          call_spread(),    covered_call(),    protective_put(),   range_condor(),      before_expiry(),  gamma_scalp(),
          funding_carry(),  event_vol(),       calendar_spread(),  scale_manage(),      second_chance(),  fast_market(),
          the_dip(),        round_trend(),     round_range(),      round_breakout(),    round_event()};
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
  out.mission.objectives = it->objectives;
  out.mission.liq_warning_bp = it->liq_warning_bp;
  out.mission.drawdown_budget_bp = it->drawdown_budget_bp;
  out.mission.risk_limits = it->risk_limits;
  if (it->options) {
    out.options = default_option_spec(it->allow_short_options);
    out.options.spread_margin = it->spread_margin;  // defined-risk spread margin (design D5)
    out.options.calendar_margin = it->calendar_margin;
  }
  generate_market(it->gen, seed, out);
  if (it->options) add_chain(out, it->chain, it->info.warmup_bars);
  else out.options.iv_base.clear();
  return validate(out) == ScenarioError::Ok;
}

namespace {

double close_at(const Scenario& sc, std::uint32_t i) { return static_cast<double>(sc.bars[i].close.raw) / Price::kScale; }

std::int32_t basis_points(double ratio) { return static_cast<std::int32_t>(std::floor((ratio - 1.0) * 10'000.0 + 0.5)); }

// The ATM straddle (call + put mid at the strike nearest the forward) of the first expiry still
// live after `after`, quoted at `step`, as basis points of the forward: the move the premium implied.
std::int32_t implied_move_bp(const Scenario& sc, std::uint32_t step, std::uint32_t after) {
  const Price forward = sc.bars[step].close;
  const OptionSeries* call = nullptr;
  const OptionSeries* put = nullptr;
  for (const OptionSeries& s : sc.options.series) {
    if (s.list_step > step || s.expiry_step <= after) continue;
    const OptionSeries*& slot = s.kind == OptionKind::Call ? call : put;
    const bool sooner = slot == nullptr || s.expiry_step < slot->expiry_step;
    const bool nearer = slot != nullptr && s.expiry_step == slot->expiry_step &&
                        std::llabs(s.strike.raw - forward.raw) < std::llabs(slot->strike.raw - forward.raw);
    if (sooner || nearer) slot = &s;
  }
  if (call == nullptr || put == nullptr || call->strike != put->strike) return -1;
  const double mid = static_cast<double>(quote(sc, *call, step, forward).mid.raw + quote(sc, *put, step, forward).mid.raw);
  return static_cast<std::int32_t>(std::floor(mid / static_cast<double>(forward.raw) * 10'000.0 + 0.5));
}

EventReveal reveal_event(const Scenario& sc, const ScriptedEvent& ev, std::uint64_t seed, std::uint32_t index) {
  const NewsItem& n = ev.item;
  const std::uint32_t r = n.release_step;
  const std::uint32_t last = static_cast<std::uint32_t>(sc.bars.size() - 1);
  EventReveal e;
  e.release_step = r;
  e.kind = n.kind;
  e.actual = n.actual;
  e.surprise = static_cast<std::int8_t>(n.actual - n.expectation);
  e.intensity_pct = draw_outcome(ev, seed, index).intensity_pct;
  e.reaction = static_cast<std::int8_t>(sample_reaction(ev, seed, index));
  e.expectation = n.expectation;
  e.credibility = n.credibility;
  e.announce_lead = n.announce_lead;
  if (r + 1 <= last) e.gap_bp = basis_points(static_cast<double>(sc.bars[r + 1].open.raw) / static_cast<double>(sc.bars[r].close.raw));
  if (r >= 1) e.move_bp = basis_points(close_at(sc, std::min(r + 3, last)) / close_at(sc, r - 1));
  if (r >= 1 && !sc.options.series.empty()) e.implied_bp = implied_move_bp(sc, r - 1, std::min(r + 3, last));
  return e;
}

}  // namespace

// Regenerates the mission (same spec, seed and streams as build_mission) to read back what the run
// could not see: the regime spans as played and each event's drawn outcome, sampled reaction, the
// gap it opened, and the move the options implied against the move that followed.
bool mission_reveal(const std::string& id, std::uint64_t seed, MissionReveal& out) {
  const std::optional<MissionDef> def = find_mission(id);
  if (!def) return false;
  out = MissionReveal{};
  Scenario sc;
  sc.contract = def->contract.value_or(default_contract());  // same tick grid as the played market
  generate_market(def->gen, seed, sc, &out.spans);
  if (!build_mission(id, seed, sc)) return false;
  const GeneratorSpec spec = played(def->gen, seed);
  for (std::uint32_t e = 0; e < spec.events.size(); ++e) out.events.push_back(reveal_event(sc, spec.events[e], seed, e));
  return true;
}

}  // namespace tradelab
