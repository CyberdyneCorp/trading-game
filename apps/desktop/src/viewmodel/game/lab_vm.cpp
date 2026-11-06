#include "viewmodel/game/lab_vm.hpp"

#include <algorithm>
#include <cmath>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

namespace {

const std::string kDot = " \xC2\xB7 ";  // " · "

// Characters, not bytes (the texts are UTF-8; the font is monospace).
std::size_t width_of(const std::string& s) {
  return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (c & 0xC0) != 0x80; }));
}

std::string pad_left(const std::string& s, std::size_t w) {
  const std::size_t n = width_of(s);
  return n >= w ? s : std::string(w - n, ' ') + s;
}

std::string pad_right(const std::string& s, std::size_t w) {
  const std::size_t n = width_of(s);
  return n >= w ? s : s + std::string(w - n, ' ');
}

constexpr std::size_t kLegWidth = 14;
constexpr std::size_t kCellWidth = 10;

std::string row(const std::vector<std::string>& cells) {
  std::string out = pad_right(cells.front(), kLegWidth);
  for (std::size_t i = 1; i < cells.size(); ++i) out += pad_left(cells[i], kCellWidth);
  return out;
}

Tone sign_tone(const std::string& decimal) {
  const double v = fmt::num(decimal);
  if (v > 0) return Tone::Up;
  return v < 0 ? Tone::Down : Tone::Plain;
}

std::string leg_label(const Texts& t, const WhatIfLeg& l) {
  const Locale L = t.locale();
  const std::string side = l.buy ? "+" : "\xE2\x88\x92";  // −
  const std::string qty = fmt::fmt_num(fmt::num(l.qty), L, 0);
  if (l.kind == "perp") return side + qty + " " + t.tr("lab.perp");
  return side + qty + " " + (l.kind == "call" ? "C" : "P") + " " + fmt::fmt_price(l.strike.value_or("0"), L);
}

std::string greek(const std::string& v, Locale L, int digits = 4) { return fmt::fmt_num(fmt::num(v), L, digits); }

// A leg's implied volatility ("66,5%"), starred when the engine floored it; a dash for the perpetual.
std::string iv_text(const WhatIfLeg& l, Locale L) {
  if (!l.iv) return "\xE2\x80\x94";  // —
  return fmt::fmt_pct(fmt::num(*l.iv), L, 1) + (l.iv_floored ? "*" : "");
}

bool any_iv_floored(const WhatIfResult& r) {
  return std::any_of(r.legs.begin(), r.legs.end(), [](const WhatIfLeg& l) { return l.iv_floored; });
}

Line leg_row(const Texts& t, const WhatIfLeg& l) {
  const Locale L = t.locale();
  return {row({leg_label(t, l), fmt::fmt_price(l.value, L), iv_text(l, L), greek(l.delta, L), greek(l.gamma, L), greek(l.theta, L),
               greek(l.vega, L), fmt::fmt_money(l.pnl, L, true)}),
          sign_tone(l.pnl)};
}

std::string slider_text(const Texts& t, Slider s, const LabShift& shift, double forward, bool terminal) {
  const auto sign = [](int v) { return std::string(v > 0 ? "+" : "") + std::to_string(v); };
  if (s == Slider::Price) {
    return sign(shift.price) + "% \xE2\x86\x92 " + fmt::fmt_price(shifted_forward(forward, shift.price), t.locale());  // →
  }
  if (s == Slider::Iv) return t.tr("lab.ivPoints", {{"n", sign(shift.iv)}});
  return t.tr(terminal ? "lab.bars" : "lab.days", {{"n", std::to_string(shift.time)}});
}

int slider_value(const LabShift& s, Slider which) {
  if (which == Slider::Price) return s.price;
  return which == Slider::Time ? s.time : s.iv;
}

SceneLine vertical(float x, float y0, float y1, Ink ink, float dash = 0) { return {x, y0, x, y1, ink, 1, 1, dash, dash}; }

void add_curve(ChartScene& s, const std::vector<ChartPoint>& pts, Ink ink, float width, float dash) {
  for (std::size_t i = 1; i < pts.size(); ++i) {
    s.lines.push_back({static_cast<float>(pts[i - 1].x), static_cast<float>(pts[i - 1].y), static_cast<float>(pts[i].x),
                       static_cast<float>(pts[i].y), ink, 1, width, dash, dash > 0 ? 3.0f : 0.0f});
  }
}

}  // namespace

std::optional<int> AnalysisTicket::next(const std::string& request) {
  if (request == sent_) return std::nullopt;
  sent_ = request;
  return ++ticket_;
}

std::string breakevens_text(const WhatIfResult& r, Locale L) {
  if (!r.expiry || r.expiry->breakevens.empty()) return "\xE2\x80\x94";
  std::string out;
  for (const auto& b : r.expiry->breakevens) out += (out.empty() ? "" : kDot) + fmt::fmt_price(b, L);
  return out;
}

std::string max_profit_text(const Texts& t, const WhatIfResult& r) {
  if (r.expiry && r.expiry->max_profit) return fmt::fmt_money(*r.expiry->max_profit, t.locale());
  return t.tr("lab.unbounded");
}

std::string max_loss_text(const Texts& t, const WhatIfResult& r) {
  if (r.expiry && r.expiry->max_loss) return fmt::fmt_money(*r.expiry->max_loss, t.locale());
  const auto above = unbounded_above(r);
  return t.tr("lab.unboundedAbove", {{"price", above ? fmt::fmt_price(*above, t.locale()) : "\xE2\x80\x94"}});
}

std::vector<Line> whatif_table(const Texts& t, const WhatIfResult& r, bool per_day) {
  const Locale L = t.locale();
  const std::string unit = t.tr(per_day ? "lab.perDay" : "lab.perBar");
  std::vector<Line> out;
  out.push_back({t.tr("lab.tableCaption"), Tone::Muted});
  out.push_back({row({t.tr("lab.leg"), t.tr("lab.value"), t.tr("opt.iv"), "\xCE\x94", "\xCE\x93", "\xCE\x98/" + unit,
                      "\xCE\xBD/" + t.tr("lab.perPoint"), t.tr("lab.pnl")}),
                 Tone::Muted});
  for (const auto& l : r.legs) out.push_back(leg_row(t, l));
  out.push_back({row({t.tr("lab.net"), fmt::fmt_money(r.net.value, L), "", greek(r.net.delta, L, 3), greek(r.net.gamma, L),
                      fmt::fmt_money(r.net.theta, L), fmt::fmt_money(r.net.vega, L), fmt::fmt_money(r.net.pnl, L, true)}),
                 sign_tone(r.net.pnl)});
  if (any_iv_floored(r)) out.push_back({"* " + t.tr("lab.ivFloored"), Tone::Muted});
  return out;
}

std::vector<Line> whatif_compact(const Texts& t, const WhatIfResult& r) {
  const Locale L = t.locale();
  std::vector<Line> out;
  for (const auto& l : r.legs) {
    std::string line = leg_label(t, l) + kDot + fmt::fmt_price(l.value, L);
    if (l.iv) line += kDot + t.tr("opt.iv") + " " + iv_text(l, L);
    out.push_back({line + kDot + "\xCE\x94 " + greek(l.delta, L) + kDot + fmt::fmt_money(l.pnl, L, true), sign_tone(l.pnl)});
  }
  // The web's WhatIfTable total row: the position's value and P&L, then its Greeks.
  out.push_back({t.tr("lab.net") + ": " + fmt::fmt_money(r.net.value, L) + kDot + t.tr("lab.pnl") + " " +
                     fmt::fmt_money(r.net.pnl, L, true) + kDot + "\xCE\x94 " + greek(r.net.delta, L, 3) + kDot + "\xCE\x93 " +
                     greek(r.net.gamma, L) + kDot + "\xCE\x98/" + t.tr("lab.perBar") + " " + fmt::fmt_money(r.net.theta, L) +
                     kDot + "\xCE\xBD/" + t.tr("lab.perPoint") + " " + fmt::fmt_money(r.net.vega, L),
                 sign_tone(r.net.pnl)});
  if (any_iv_floored(r)) out.push_back({"* " + t.tr("lab.ivFloored"), Tone::Muted});
  return out;
}

ChartScene payoff_scene(const WhatIfResult& r, float width, float height) {
  ChartScene s;
  const auto g = payoff_geometry(r, width, height, result_strikes(r));
  if (!g) return s;
  s.empty = false;
  s.rects.push_back({0, 0, width, height, Ink::Line, 1, true});
  s.lines.push_back({0, static_cast<float>(g->zero_y), width, static_cast<float>(g->zero_y), Ink::Muted, 1, 1, 3, 3});
  for (const double x : g->strike_x) s.lines.push_back(vertical(static_cast<float>(x), height - 6, height, Ink::Muted));
  for (const double x : g->breakeven_x) s.lines.push_back(vertical(static_cast<float>(x), 0, height, Ink::Attention, 2));
  s.lines.push_back(vertical(static_cast<float>(g->forward_x), 0, height, Ink::Info));
  add_curve(s, g->expiry, Ink::Accent, 2, 0);
  add_curve(s, g->today, Ink::Info, 1.5f, 5);
  return s;
}

// ---- LabViewModel ----

void LabViewModel::open_academy() {
  mode = LabMode::Academy;
  forward = kAcademyForward;
  academy_legs = {AcademyLeg{LegKind::Call, true, kAcademyForward, 1, 30, 60, 0}};
  run_legs.clear();
  shift = {};
  result_.reset();
  error_.clear();
  ticket.forget();
}

void LabViewModel::open_terminal(std::vector<AnalysisLeg> legs, double last_price, int bars_to_expiry) {
  mode = LabMode::Terminal;
  run_legs = std::move(legs);
  forward = last_price;
  max_bars = bars_to_expiry;
  shift = {};
  result_.reset();
  error_.clear();
  ticket.forget();
}

int LabViewModel::max_time() const {
  if (mode == LabMode::Terminal) return max_bars;
  return static_cast<int>(academy_max_days(academy_legs));
}

std::string LabViewModel::request() const {
  const LabShift s = clamp_shift(shift, max_time());
  if (mode == LabMode::Terminal) return run_request(run_legs, s, forward);
  return academy_request(academy_legs, s, forward);
}

std::string LabViewModel::pricing_request() const {
  std::vector<AcademyLeg> unpriced = academy_legs;
  for (auto& l : unpriced) l.entry = l.kind == LegKind::Perp ? forward : 0;
  return academy_request(unpriced, {}, forward);
}

void LabViewModel::slide(Slider which, int value) { shift = clamp_shift(set_slider(shift, which, value, unlocked), max_time()); }

void LabViewModel::add_leg(LegKind kind) {
  AcademyLeg l;
  l.kind = kind;
  l.entry = kind == LegKind::Perp ? forward : 0;
  academy_legs.push_back(l);
}

void LabViewModel::remove_leg(std::size_t i) {
  if (i < academy_legs.size()) academy_legs.erase(academy_legs.begin() + static_cast<std::ptrdiff_t>(i));
}

void LabViewModel::answer(const std::string& text) {
  auto a = whatif_from_json(text);
  result_ = std::move(a.value);
  error_ = a.error;
}

void LabViewModel::priced(const std::string& text) {
  const auto a = whatif_from_json(text);
  if (!a.ok() || a.value->legs.size() != academy_legs.size()) return;
  for (std::size_t i = 0; i < academy_legs.size(); ++i) {
    AcademyLeg& l = academy_legs[i];
    l.entry = l.kind == LegKind::Perp ? forward : fmt::num(to_fixed(fmt::num(a.value->legs[i].value), 2));
  }
}

LabView LabViewModel::view(const Texts& t) const {
  LabView v;
  const bool terminal = mode == LabMode::Terminal;
  const Locale L = t.locale();
  v.note = t.tr("lab.model") + (terminal ? " " + t.tr("lab.todayIv") : "");
  for (const Slider s : kSliders) v.sliders.push_back(slider_view(t, s));
  if (!error_.empty()) v.error = t.tr(error_ == "unsupported" ? "lab.unsupported" : "lab.error");
  if (!result_ || !error_.empty()) return v;
  const WhatIfResult& r = *result_;
  v.has_result = true;
  v.summary.emplace_back(t.tr("lab.positionValue"), fmt::fmt_money(r.net.value, L));
  v.summary.emplace_back(t.tr("lab.pnlNow"), fmt::fmt_money(r.net.pnl, L, true));
  v.pnl_tone = sign_tone(r.net.pnl);
  if (r.expiry) {
    v.summary.emplace_back(t.tr("opt.breakeven"), breakevens_text(r, L));
    v.summary.emplace_back(t.tr("lab.maxProfit"), max_profit_text(t, r));
    v.summary.emplace_back(t.tr("lab.maxLoss"), max_loss_text(t, r));
  }
  if (r.at_expiry) v.at_expiry_note = t.tr("lab.atExpiryNote");
  v.table = whatif_table(t, r, !terminal);
  chart_texts(t, r, v.chart_summary, v.chart_legend);
  return v;
}

LabSliderView LabViewModel::slider_view(const Texts& t, Slider s) const {
  const bool locked = std::find(unlocked.begin(), unlocked.end(), s) == unlocked.end();
  LabSliderView sv;
  sv.which = s;
  sv.label = t.tr("lab.slider." + std::string(slider_code(s))) + (locked ? kDot + t.tr("lab.locked") : "");
  sv.min = s == Slider::Price ? -kPriceRange : (s == Slider::Iv ? -kIvRange : 0);
  sv.max = s == Slider::Price ? kPriceRange : (s == Slider::Iv ? kIvRange : max_time());
  sv.value = slider_value(shift, s);
  sv.enabled = !locked && !(s == Slider::Time && max_time() == 0);
  sv.text = slider_text(t, s, shift, forward, mode == LabMode::Terminal);
  return sv;
}

// The chart's accessible description (PayoffChart.svelte's summary) and its legend.
void chart_texts(const Texts& t, const WhatIfResult& r, std::string& summary, std::string& legend) {
  const Locale L = t.locale();
  const std::string profit = r.expiry && r.expiry->max_profit ? fmt::fmt_money(*r.expiry->max_profit, L) : t.tr("lab.unbounded");
  const std::string loss = r.expiry && r.expiry->max_loss ? fmt::fmt_money(*r.expiry->max_loss, L) : t.tr("lab.unbounded");
  summary = t.tr("lab.chartSummary", {{"bes", breakevens_text(r, L)}, {"profit", profit}, {"loss", loss}});
  if (const auto g = payoff_geometry(r, 1, 1, {})) {
    legend = fmt::fmt_price(js_number(g->lo), L) + "   \xE2\x94\x80\xE2\x94\x80 " + t.tr("lab.atExpiry") + "   - - " +
                     t.tr("lab.today") + "   " + fmt::fmt_price(js_number(g->hi), L);
  }
}

}  // namespace tradelab::desktop::game
