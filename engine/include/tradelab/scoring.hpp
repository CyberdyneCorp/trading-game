#pragma once

// Authoritative process scoring and debrief facts (spec: mission-scoring, gameplay-loop "Debrief").
// Clients display this report; the server recomputes it from the same commands (never trusts a
// client-reported score). Integer arithmetic only.
//
// Dimensions (scoring version 2): risk 35, plan coherence 25, understanding 25 (not measured yet),
// calibration 15. A dimension without data is not applicable and the rest are renormalized.

#include <cstdint>
#include <optional>
#include <vector>

#include "tradelab/advisor.hpp"
#include "tradelab/engine.hpp"

namespace tradelab {

constexpr std::uint32_t kScoringVersion = 4;  // v2: calibration 100 * (1 - 2 * Brier); v3: margin calls count like liquidations, oversized judged per trade, stricter good-process credit; v4: advisor points and advice outcomes

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

enum class LessonCode : std::uint8_t { Futures2 = 0, Risk, Futures3, Orders, Structure, Evaluation };

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
};

// Advice in the debrief (spec advisor "Advice in the terminal and the debrief"): a long or short
// read is right when the close kAdviceHorizon bars after the next open moved its way.
enum class AdviceOutcome : std::uint8_t { Right = 0, Wrong, Wait, Open };

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

}  // namespace tradelab
