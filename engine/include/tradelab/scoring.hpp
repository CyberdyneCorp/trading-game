#pragma once

// Authoritative process scoring and debrief facts (spec: mission-scoring, gameplay-loop "Debrief").
// Clients display this report; the server recomputes it from the same commands (never trusts a
// client-reported score). Integer arithmetic only.
//
// Dimensions (scoring version 4): risk 35, plan coherence 25, understanding 25 (not measured yet),
// calibration 15. A dimension without data is not applicable and the rest are renormalized.
// Report-only debrief facts (spread/slippage cost, option P&L by Greek, the leverage that would have
// kept a stop in front of liquidation) never feed the score.

#include <cstdint>
#include <optional>
#include <vector>

#include "tradelab/engine.hpp"

namespace tradelab {

constexpr std::uint32_t kScoringVersion = 4;  // v2: calibration 100 * (1 - 2 * Brier); v3: margin calls count like liquidations, oversized judged per trade, stricter good-process credit; v4: spread/slippage cost, Greek attribution, safe leverage, theta/IV findings and the Options lesson

enum class FindingCode : std::uint8_t {
  Waited = 0,
  Liquidated,
  StopBeyondLiq,
  NoStop,
  GapSlippage,
  FundingCost,
  Ambiguous,
  Capacity,
  GoodProcessLoss,
  LuckyWin,
  NoPlan,
  Oversized,
  Bankrupt,
  MarginCalled,  // written options bought back by force (step, buy-back premium, loss)
  ThetaDecay,    // time decay (theta) explains at least half of an option loss (amount = theta P&L lost)
  IvCrush,       // falling implied volatility (vega) explains at least half of an option loss
};

enum class FindingTone : std::uint8_t { Good = 0, Warn, Bad, Info };

struct Finding {
  FindingCode code = FindingCode::Waited;
  FindingTone tone = FindingTone::Info;
  std::uint32_t step = 0;
  std::uint32_t count = 0;
  Price price;
  Price requested;
  Money amount;
};

enum class LessonCode : std::uint8_t { Futures2 = 0, Risk, Futures3, Orders, Structure, Evaluation, Options };

enum class ExitReason : std::uint8_t { Stop = 0, Target, Manual, Liquidation, Open, Expiry, MarginCall };

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
  Money pnl;             // realized, before fees
  Money fees;            // this trade's share of fill fees (a flip's fee is split by quantity)
  Money funding;         // funding received (+) or paid (-) while the trade was open
  Money net;             // pnl - fees + funding
  std::uint32_t bars = 0;
  ExitReason reason = ExitReason::Open;
  Money planned_risk;    // 0 = no plan / unknown
  std::optional<std::int64_t> r_bp;  // net / planned risk, in basis points (10000 = 1R)
  std::optional<bool> plan_respected;
  // Perpetual rows only: the protective stop (first reduce-only stop of the trade, else the plan's
  // invalidation; 0 = none), the leverage at entry and the liquidation price it implied at entry.
  Price stop;
  std::int32_t leverage = 0;
  Price liq_price;  // 0 = not liquidable (e.g. a 1x long)
  bool stop_beyond_liq = false;
  // When the stop sat at or beyond liquidation: the highest whole leverage (>= 1) that would have kept
  // it in front; 0 when even 1x would not have.
  std::int32_t safe_leverage = 0;
};

// Approximate option P&L attribution (spec gameplay-loop "Debrief"): per bar and held series, from the
// quote at the start of each interval (previous close -> open, open -> close or settlement) as
// delta*dF + gamma*dF^2/2 + theta*dt + vega*dIV. The residual is the rest of the option P&L (before
// fees): spread paid against the mid, fills away from the reference quote and higher-order terms.
struct GreekAttribution {
  Money delta;
  Money gamma;
  Money theta;
  Money vega;
  Money residual;
  Money total;  // option P&L before fees: realized + open positions at the final mid
};

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
  std::optional<GreekAttribution> greeks;  // only when options were traded
};

std::vector<TradeRow> build_trades(const Scenario& sc, const RunState& st);
ScoreReport score_run(const Scenario& sc, const RunState& st);

// Report-only debrief facts (engine/src/debrief.cpp).
Money spread_slippage_cost(const Scenario& sc, const RunState& st);
std::optional<GreekAttribution> greek_attribution(const Scenario& sc, const RunState& st);
void annotate_stop(const Scenario& sc, const RunState& st, TradeRow& t);

}  // namespace tradelab
