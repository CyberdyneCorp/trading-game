#pragma once

// Authoritative process scoring and debrief facts (spec: mission-scoring, gameplay-loop "Debrief").
// Clients display this report; the server recomputes it from the same commands (never trusts a
// client-reported score). Integer arithmetic only.
//
// Dimensions (scoring version 1): risk 35, plan coherence 25, understanding 25 (not measured yet),
// calibration 15. A dimension without data is not applicable and the rest are renormalized.

#include <cstdint>
#include <optional>
#include <vector>

#include "tradelab/engine.hpp"

namespace tradelab {

constexpr std::uint32_t kScoringVersion = 1;

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

enum class ExitReason : std::uint8_t { Stop = 0, Target, Manual, Liquidation, Open, Expiry };

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
};

std::vector<TradeRow> build_trades(const Scenario& sc, const RunState& st);
ScoreReport score_run(const Scenario& sc, const RunState& st);

}  // namespace tradelab
