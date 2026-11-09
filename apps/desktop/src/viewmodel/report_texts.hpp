// Ports of the web's text helpers for the engine's reports (names kept): analysis.ts
// (describeFindings, describeLesson), objectives.ts (bpPct, objectiveText), debrief.ts (fmtLeverageBp,
// fmtLiqDistance, stopAdvice, regimeName, fmtTally, releaseRows, fmtBp, greekRows, decisionCounts) and
// sizing.ts briefTiers. The engine is authoritative: these only render its codes and numbers.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/i18n.hpp"
#include "model/json.hpp"

namespace tradelab::desktop {

// Basis points as a percentage without trailing zeros: 100 -> "1%", 250 -> "2,5%".
std::string bp_pct(int bp, Locale locale);
std::string objective_text(const Catalog& c, const json::Value& objective);
std::vector<std::string> objective_texts(const Catalog& c, const json::Value& mission_rules);

// One text per engine finding; the safe-leverage advice is said once (liquidation line shortened
// when stop_beyond_liq carries it). `liq_warning_bp` < 0: no mission context.
std::vector<std::string> describe_findings(const Catalog& c, const json::Value& findings, int liq_warning_bp);
std::string describe_lesson(const Catalog& c, const std::string& lesson);

std::string fmt_leverage_bp(double bp, Locale locale);
std::string fmt_liq_distance(double bp, Locale locale);
std::string fmt_bp(double bp, Locale locale);

// "Stop 98,50 beyond liquidation 97,00 at 10x: up to 4x...": null text when not a perpetual trade
// whose stop sat beyond liquidation.
std::string stop_advice_text(const Catalog& c, const json::Value& trade);
// "▲ LONG", "▼ SHORT", or "▲ COMPRA P 100,00 · exp 96" for an option.
std::string trade_label(const Catalog& c, const json::Value& trade);

// EpisodesPanel.svelte: one scoring v14 position episode as table cells (#, side, entries, exits,
// stop moves, R with the net, MFE · MAE).
std::vector<std::string> episode_cells(const Catalog& c, const json::Value& episode);

std::string regime_name(const Catalog& c, const std::string& label, int direction = 0);
std::string fmt_tally(const Catalog& c, const json::Value& tally);
// A trade's entry regime with the direction of the revealed span it was entered in.
std::string entry_regime_name(const Catalog& c, const json::Value& trade, const json::Value& spans);

struct BandPiece {
  std::string label;
  int direction = 0;
  int start = 0;
  int bars = 0;
  double left = 0;   // share of the decision window
  double width = 0;
};
std::vector<BandPiece> regime_band(const json::Value& spans, int from, int to);

enum class ReleasePosition : std::uint8_t { Before, After, Both, Flat };
struct ReleaseRow {
  json::Value event;
  double weighted = 0;
  ReleasePosition you = ReleasePosition::Flat;
};
std::vector<ReleaseRow> release_rows(const json::Value& events, const json::Value& trades, int last_step,
                                     int after_release_bars);
std::string_view release_position_name(ReleasePosition p);

struct GreekRow {
  std::string key;
  double value = 0;
};
std::vector<GreekRow> greek_rows(const json::Value& greeks);

// A row of the debrief's strategy comparison as drawn (debrief.ts comparisonLines): the player's own
// result first, then the engine's reference structures (scoring v13, report-only). A refused structure
// has no numbers, only the engine's refusal reason.
struct ComparisonLine {
  std::string key;   // "you" or the structure's id
  std::string name;  // i18n key: comparison.you or comparison.<structure>
  std::optional<double> net, drawdown;
  std::optional<int> margin_calls, entry_step, exit_step;
  std::string refused;  // the engine's refusal reason, "" when the structure was placed
  bool best = false;    // the best net result among the rows (the player's included); ties all marked
};
std::vector<ComparisonLine> comparison_lines(const json::Value& comparison, double player_net);
// debrief.ts comparisonBasisKey: the intro's key; rows played by their own entry rule (m20, m22) start on
// other bars than the basis and name their own rules.
std::string comparison_basis_key(const json::Value& comparison);

struct DecisionCounts {
  int act = 0;
  int hold = 0;
  int wait = 0;
};
DecisionCounts decision_counts(const json::Value& decisions);

// Higher margin tiers by notional, capped at the mission's leverage: "≥ 50.000: 1% (até 5x)".
std::vector<std::string> brief_tiers(const Catalog& c, const json::Value& contract);

// tCount: `{n}` in the locale, or the key's "...One" form when n is 1.
std::string tr_count(const Catalog& c, const std::string& key, long n, Params params = {});

}  // namespace tradelab::desktop
