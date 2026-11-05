#pragma once

// Authoritative process scoring and debrief facts (spec: mission-scoring, gameplay-loop "Debrief").
// Clients display this report; the server recomputes it from the same commands (never trusts a
// client-reported score). Integer arithmetic only.
//
// Dimensions (scoring version 7): risk 35, plan coherence 25, understanding 25 (not measured yet),
// calibration 15. A dimension without data is not applicable and the rest are renormalized.
// Report-only debrief facts (spread/slippage cost, option P&L by Greek, the leverage that would have
// kept a stop in front of liquidation) never feed the score.

#include <cstdint>
#include <optional>
#include <vector>

#include "tradelab/advisor.hpp"
#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"

namespace tradelab {

// v8: Greek attribution by full revaluation (gaps land in delta/gamma), side-aware IV finding (iv_rise
// for written options), wrote_options finding, lucky_win when a trade had no plan, the buy-and-hold
// benchmark and market span measured to the run's last bar.
// v9 (report-only, no digest change): signals by revealed regime and each trade's entry regime (G4).
constexpr std::uint32_t kScoringVersion = 10;  // v2: calibration 100 * (1 - 2 * Brier); v3: margin calls count like liquidations, oversized judged per trade, stricter good-process credit; v4: spread/slippage cost, Greek attribution, safe leverage, theta/IV findings and the Options lesson; v5: open trades include unrealized P&L, safe leverage uses the highest leverage held, good-process credit excludes written options, good_process_win finding; v6: advisor points and advice outcomes; v7: plans matched to their entry order (an add adds its risk), written-off shortfall; v8: full-revaluation Greek attribution, side-aware IV finding; v9: signals by regime, entry regime; v10: exit reason from the order type (a stop in profit is a stop, trailing stops named)

struct Finding {
  FindingCode code = FindingCode::Waited;
  FindingTone tone = FindingTone::Info;
  std::uint32_t step = 0;
  std::uint32_t count = 0;
  Price price;
  Price requested;
  Money amount;
};

// One round trip: a perpetual position (series 0) or an option position in one series, from the
// fill that opens it to the fill, liquidation or expiry settlement that closes it. Option prices
// are premiums (the exit at expiry is the settlement value per contract).
struct TradeRow {
  std::uint32_t n = 0;
  std::uint32_t series = 0;
  bool is_call = false;             // option rows only
  Price strike;                     // option rows only
  std::uint32_t expiry_step = 0;    // option rows only
  bool is_long = true;
  std::uint32_t entry_step = 0;
  std::optional<std::uint32_t> exit_step;
  Qty qty;               // largest absolute size reached
  Price entry_avg;
  Price exit_avg;        // 0 while open
  Money pnl;             // realized, before fees; an open trade adds its unrealized P&L at the final mark
  Money fees;            // this trade's share of fill fees (a flip's fee is split by quantity)
  Money funding;         // funding received (+) or paid (-) while the trade was open
  Money net;             // pnl - fees + funding
  std::uint32_t bars = 0;
  ExitReason reason = ExitReason::Open;
  Money planned_risk;    // 0 = no plan / unknown; the sum of the plans of the entry orders (an add adds its risk)
  Money declared_risk;   // largest risk declared for one entry order, skipped plans included (oversized check)
  std::optional<std::int64_t> r_bp;  // net / planned risk, in basis points (10000 = 1R)
  std::optional<bool> plan_respected;
  // Perpetual rows only: the protective stop (first reduce-only stop of the trade, else the plan's
  // invalidation; 0 = none), the highest leverage the position carried (at entry or re-leveraged while
  // open) and the liquidation price it implied.
  Price stop;
  std::int32_t leverage = 0;
  Price liq_price;  // 0 = not liquidable (e.g. a 1x long)
  bool stop_beyond_liq = false;
  // When the stop sat at or beyond liquidation: the highest whole leverage (>= 1) that would have kept
  // it in front; 0 when even 1x would not have.
  std::int32_t safe_leverage = 0;
};

// Option P&L attribution (spec gameplay-loop "Debrief"), by full revaluation per bar and held series:
// the price legs (previous close -> open, open -> close or settlement, at the previous step's quote)
// split the mid change into delta = start delta x dF and gamma = the rest; the time leg (one step at
// that forward, intrinsic at expiry) into vega = average vega x dIV and theta = the rest. The residual
// is the rest of the option P&L (before fees): the spread paid against the mid at fills, and rounding.
struct GreekAttribution {
  Money delta;
  Money gamma;
  Money theta;
  Money vega;
  Money vega_written;  // part of vega from written (short) positions; not a separate line, picks the IV finding
  Money residual;
  Money total;  // option P&L before fees: realized + open positions at the final mid
};

struct AdviceRow {
  std::uint32_t step = 0;
  AdviceStance stance = AdviceStance::Wait;
  std::uint32_t confidence_bp = 0;
  AdviceOutcome outcome = AdviceOutcome::Wait;
};

// Advisor points (spec progression-rewards "Advisor points earned by process", design D3).
constexpr std::uint32_t kAdvisorPointsCap = 6;

struct Dimension {
  std::uint32_t weight = 0;
  std::optional<std::int32_t> score;  // 0..100; nullopt = not applicable
};

struct ScoreReport {
  Dimension risk{35, std::nullopt};
  Dimension plan{25, std::nullopt};
  Dimension understanding{25, std::nullopt};
  Dimension calibration{15, std::nullopt};
  std::optional<std::int32_t> total;
  std::vector<Finding> findings;
  LessonCode lesson = LessonCode::Evaluation;
  std::vector<TradeRow> trades;
  std::uint32_t entries = 0;
  std::uint32_t resolved_answers = 0;
  std::optional<std::int64_t> brier_bp;  // Brier score in basis points of 1.0
  Money net_pnl;
  Money spread_cost;  // spread + slippage paid against the reference price (already inside fill prices)
  Money written_off;  // option loss beyond the balance covered by the simulated fund (wallet floored at 0)
  std::optional<GreekAttribution> greeks;  // only when options were traded
  std::uint32_t advisor_points_earned = 0;
  std::vector<AdviceRow> advice;
  std::optional<std::int64_t> advice_hit_bp;  // right / (right + wrong), basis points
};

std::vector<TradeRow> build_trades(const Scenario& sc, const RunState& st);
ScoreReport score_run(const Scenario& sc, const RunState& st);

// +1 per closed perpetual trade with a plan (planned risk > 0) and a protective stop working while
// it was open, +1 more if its net P&L is positive, +2 if the run finished; capped at 6.
std::uint32_t advisor_points(const RunState& st, const std::vector<TradeRow>& trades);
std::vector<AdviceRow> advice_rows(const Scenario& sc, const RunState& st);

// Report-only debrief facts (engine/src/debrief.cpp).
Money spread_slippage_cost(const Scenario& sc, const RunState& st);
std::optional<GreekAttribution> greek_attribution(const Scenario& sc, const RunState& st);
void annotate_stop(const Scenario& sc, const RunState& st, TradeRow& t);
// Adds an open trade's unrealized P&L at the final mark to its pnl and net (as in the account equity).
void mark_open_trade(const Scenario& sc, const RunState& st, TradeRow& t);
// Signals by regime (design D8.1 G4, report-only): for each revealed span of the run, how often four
// common reads were right. A read at the close of bar i acts from the open of bar i+1 and is right
// when the close kSignalHorizon bars after i moved its way: RSI(14) fade below 30 / above 70,
// Bollinger(20, 2) fade outside the bands, MACD(12, 26, 9) histogram follow, SMA(20) side follow.
constexpr std::uint32_t kSignalHorizon = 5;

struct SignalTally {
  std::uint32_t hits = 0;
  std::uint32_t samples = 0;
};

struct RegimeSignals {
  RegimeSpan span;
  SignalTally rsi_fade;
  SignalTally bollinger_fade;
  SignalTally macd_follow;
  SignalTally sma_follow;
};

// Spans overlapping the run's decision window (first decision step .. last step), each clipped to it.
std::vector<RegimeSignals> signals_by_regime(const Scenario& sc, const RunState& st, const std::vector<RegimeSpan>& spans);

// Buy-and-hold benchmark: 1x long from the first eligible open to the close of the run's last bar (the
// scenario's end, or the game-over bar), taker fees both ways; 0 before the first decision.
Money buy_and_hold_pnl(const Scenario& sc, const RunState& st);

}  // namespace tradelab
