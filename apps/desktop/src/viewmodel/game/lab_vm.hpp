// What-if lab viewmodel (web WhatIfLab.svelte + PayoffChart.svelte + WhatIfTable.svelte, options-whatif
// spec): the sliders, the engine request they make, the engine's answer as texts and as a display list
// for the chart renderer. Academy mode values editable legs on an illustrative market (tl_option_whatif);
// terminal mode values the run's series with today's IV level (tl_run_option_whatif), so at zero shift
// it equals the chain. No number here is computed: every value, P&L and Greek is the engine's.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/snapshot.hpp"
#include "viewmodel/game/chart_vm.hpp"
#include "viewmodel/game/texts.hpp"
#include "viewmodel/game/whatif_lab.hpp"

namespace tradelab::desktop::game {

enum class LabMode : std::uint8_t { Academy, Terminal };

// A replied engine analysis is used only when it answers the latest request (sliders move faster than
// the worker answers): each request takes a ticket, and an older reply is dropped.
class AnalysisTicket {
 public:
  // The request changed since the last one sent: a new ticket to send it with; nullopt otherwise.
  std::optional<int> next(const std::string& request);
  [[nodiscard]] bool current(int ticket) const { return ticket == ticket_; }
  void forget() { sent_.clear(); }

 private:
  std::string sent_;
  int ticket_ = 0;
};

struct LabSliderView {
  Slider which = Slider::Price;
  std::string label;
  int min = 0, max = 0, value = 0;
  bool enabled = true;
  std::string text;  // "+7% → 107,00", "+3 barras", "-5 pontos de vol."
};

struct LabView {
  std::string note;  // the model (and in a run, today's IV level)
  std::vector<LabSliderView> sliders;
  std::string error;  // a localized failure, "" when none
  std::vector<std::pair<std::string, std::string>> summary;
  Tone pnl_tone = Tone::Plain;
  std::string at_expiry_note;  // "" unless the time reached the expiry
  std::vector<Line> table;     // caption, header, one row per leg, the position
  std::string chart_summary;   // the chart's accessible description
  std::string chart_legend;    // "70,00   ── no vencimento  - - no cenário escolhido   130,00"
  bool has_result = false;
};

// The table (WhatIfTable.svelte) as aligned monospace lines; theta per day or per bar.
std::vector<Line> whatif_table(const Texts& t, const WhatIfResult& r, bool per_day);
// Breakevens, maximum profit and loss (summary rows and the chart's description).
std::string breakevens_text(const WhatIfResult& r, Locale L);
std::string max_profit_text(const Texts& t, const WhatIfResult& r);
std::string max_loss_text(const Texts& t, const WhatIfResult& r);
// The payoff chart as a display list (PayoffChart.svelte): expiry solid, today dashed, breakevens
// dotted, strikes ticked, the forward marked.
// The same numbers in a narrow panel (the builder): one line per leg, then the position's Greeks.
std::vector<Line> whatif_compact(const Texts& t, const WhatIfResult& r);
ChartScene payoff_scene(const WhatIfResult& r, float width, float height);
// The payoff chart's accessible description and its legend ("70,00   ── no vencimento  - - ...   130,00").
void chart_texts(const Texts& t, const WhatIfResult& r, std::string& summary, std::string& legend);

class LabViewModel {
 public:
  LabMode mode = LabMode::Academy;
  std::vector<AcademyLeg> academy_legs;
  std::vector<AnalysisLeg> run_legs;
  double forward = kAcademyForward;
  int max_bars = 0;
  std::vector<Slider> unlocked = {kSliders.begin(), kSliders.end()};
  LabShift shift;

  // Academy: an at-the-money call for 30 days at 60% IV, priced by price_at_model().
  void open_academy();
  // Terminal: the legs by series id (or the perpetual), the last price, the bars to the nearest expiry.
  void open_terminal(std::vector<AnalysisLeg> legs, double last_price, int bars_to_expiry);

  [[nodiscard]] int max_time() const;
  [[nodiscard]] AnalysisKind kind() const { return mode == LabMode::Terminal ? AnalysisKind::RunWhatIf : AnalysisKind::WhatIf; }
  [[nodiscard]] std::string request() const;
  // Academy: the request that prices every option leg at the model (no shift, entries at 0).
  [[nodiscard]] std::string pricing_request() const;
  void slide(Slider which, int value);
  void reset() { shift = {}; }

  // Academy legs (AcademyLegs.svelte).
  void add_leg(LegKind kind);
  void remove_leg(std::size_t i);

  // The engine's answers.
  void answer(const std::string& text);
  // The pricing request's answer: each option leg's entry becomes its model value (to the cent).
  void priced(const std::string& text);
  [[nodiscard]] const std::optional<WhatIfResult>& result() const { return result_; }
  [[nodiscard]] const std::string& error() const { return error_; }

  [[nodiscard]] LabView view(const Texts& t) const;
  [[nodiscard]] LabSliderView slider_view(const Texts& t, Slider s) const;
  AnalysisTicket ticket;

 private:
  std::optional<WhatIfResult> result_;
  std::string error_;
};

}  // namespace tradelab::desktop::game
