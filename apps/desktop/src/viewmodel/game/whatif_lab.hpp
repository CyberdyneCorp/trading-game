// What-if lab model (port of the web's whatifLab.ts, add-academy-and-strategies design D4, options-whatif
// spec): slider state, the engine requests it sends, the engine's answers read into structs, and the
// payoff chart's geometry. Every value, P&L, Greek, breakeven and bound shown comes from the engine
// (tl_option_whatif / tl_run_option_whatif, tl_combo_preview); this module only turns slider positions
// into requests and engine strings into drawing coordinates. Parity fixtures: whatifLab.json.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/i18n.hpp"
#include "model/json.hpp"
#include "viewmodel/game/strategies.hpp"

namespace tradelab::desktop::game {

enum class Slider : std::uint8_t { Price, Time, Iv };
inline constexpr std::array<Slider, 3> kSliders = {Slider::Price, Slider::Time, Slider::Iv};
std::string_view slider_code(Slider s);  // "price" | "time" | "iv"

// Price shift in % of the forward, time elapsed (bars in a run, days in the Academy), IV shift in points.
struct LabShift {
  int price = 0;
  int time = 0;
  int iv = 0;
  bool operator==(const LabShift&) const = default;
};

inline constexpr int kPriceRange = 30;
inline constexpr int kIvRange = 30;
inline constexpr int kGridPoints = 61;
inline constexpr double kAcademyForward = 100;

// JavaScript's Number#toFixed and String(number), for the request texts the web sends.
std::string to_fixed(double v, int digits);
std::string js_number(double v);

// The shifted forward on a cent grid.
std::string shifted_forward(double forward, int pct);

// A leg of the Academy lab: an illustrative market, every field editable.
struct AcademyLeg {
  LegKind kind = LegKind::Call;
  bool buy = true;
  double strike = kAcademyForward;
  double qty = 1;
  double days = 30;     // days to expiry (options)
  double iv_pct = 60;   // implied volatility in %
  double entry = 0;     // premium per unit (the perpetual: its entry price)
  bool operator==(const AcademyLeg&) const = default;
};

// Run-less request (one step is one day, a contract is one unit), as JSON text.
std::string academy_request(const std::vector<AcademyLeg>& legs, const LabShift& shift, double forward = kAcademyForward);
// The run's request: legs by series id (or the perpetual), time in bars, the IV model at today's base.
std::string run_request(const std::vector<AnalysisLeg>& legs, const LabShift& shift, double forward);
// The builder's analysis: the legs at the prices they would cross, no shift, the default grid.
std::string run_request(const std::vector<AnalysisLeg>& legs);
std::string combo_request(const std::vector<AnalysisLeg>& legs);

double academy_max_days(const std::vector<AcademyLeg>& legs);
int run_max_bars(const std::vector<int>& expiry_steps, int step);
LabShift clamp_shift(const LabShift& s, int max_time);
// Moves only the unlocked sliders (a lesson can fix the others).
LabShift set_slider(const LabShift& s, Slider which, int value, const std::vector<Slider>& unlocked);

// ---- the engine's answers ----

using Curve = std::vector<std::pair<std::string, std::string>>;  // [price, P&L]

struct WhatIfLeg {
  std::string kind;  // call | put | perp
  bool buy = true;
  std::string qty;
  int series = 0;
  int expiry_step = 0;
  std::optional<std::string> strike;
  std::string entry, value;
  std::optional<std::string> iv;
  bool iv_floored = false;
  std::string delta, gamma, theta, vega, position_value, pnl;
};

struct WhatIfNet {
  std::string value, pnl, delta, gamma, theta, vega;
};

struct WhatIfExpiry {
  std::vector<std::string> breakevens;
  std::optional<std::string> max_profit;
  std::optional<std::string> max_loss;  // a positive amount is a loss; nullopt = unbounded
  Curve curve;
};

struct WhatIfResult {
  std::string forward;
  bool at_expiry = false;
  std::vector<WhatIfLeg> legs;
  WhatIfNet net;
  std::optional<WhatIfExpiry> expiry;
  Curve today;
};

struct ComboLegPreview {
  bool accepted = false;
  std::string reason, quote, premium, fee, required, available_after;
};

struct ComboPreview {
  bool accepted = false;
  std::vector<ComboLegPreview> legs;
  std::string net_premium, fees, required, available_after;
  std::string option_margin_after_fill, position_margin_after_fill, available_after_fill;
};

// An engine answer: the value, or the engine's error code ("unsupported" when the engine cannot answer).
template <typename T>
struct Analysis {
  std::optional<T> value;
  std::string error;
  [[nodiscard]] bool ok() const { return value.has_value(); }
};

Analysis<WhatIfResult> whatif_from_json(const std::string& text);
Analysis<ComboPreview> preview_from_json(const std::string& text);

// whatifLab.ts legLabel: side, quantity as held (47,9 stays 47,9, never rounded to a whole contract),
// then the option ("C 105,00") or the perpetual's name.
std::string leg_label(const WhatIfLeg& l, Locale locale, const std::string& perp_name);

// Which way an unbounded loss runs: always above the upper breakeven (prices stop at 0).
std::optional<std::string> unbounded_above(const WhatIfResult& r);
// Strikes of the result's option legs, ascending (the chart's markers).
std::vector<double> result_strikes(const WhatIfResult& r);

// ---- payoff chart geometry (width w, height h; y grows downwards) ----

struct ChartPoint {
  double x = 0, y = 0;
};

struct PayoffGeometry {
  std::vector<ChartPoint> expiry;  // empty without an expiry curve
  std::vector<ChartPoint> today;
  double zero_y = 0, forward_x = 0;
  std::vector<double> breakeven_x, strike_x;
  double lo = 0, hi = 0, max_abs = 0;
};

std::optional<PayoffGeometry> payoff_geometry(const WhatIfResult& r, double w, double h, const std::vector<double>& strikes);
// The web's SVG path of a curve ("M x,y L x,y ...", one decimal): the parity fixtures compare it.
std::string path_text(const std::vector<ChartPoint>& points);

}  // namespace tradelab::desktop::game
