// ChartViewModel: pointer, wheel and keyboard handling and the text lines around the canvas
// (web Chart.svelte onDown/onMove/onUp/onClick/onWheel/onChartKey and the readout).
#include <algorithm>
#include <cmath>

#include "viewmodel/format.hpp"
#include "viewmodel/game/chart_vm.hpp"

namespace tradelab::desktop::game {

namespace {

std::string fmt_osc(const Series& s, std::size_t i, Locale L, int digits) {
  return i < s.size() && s[i] ? fmt::fmt_num(*s[i], L, digits) : std::string("\xE2\x80\x94");  // —
}

// Bars still needed before a series has a value (spec indicators "Warm-up").
int warming(const Series& s, int need, std::size_t bars) {
  const bool any = std::any_of(s.begin(), s.end(), [](const auto& v) { return v.has_value(); });
  return any ? 0 : std::max(0, need - static_cast<int>(bars));
}

ChartAction draft_action(const DragTarget& target, double price) {
  return {target.stop ? ChartAction::Kind::DraftStop : ChartAction::Kind::DraftTarget, price, 0, {}};
}

std::optional<std::string> pattern_at(const GameState& g, int bar, bool shown) {
  if (!shown) return std::nullopt;
  for (const auto& p : g.indicators.patterns) {
    if (p.step == bar) return p.id;
  }
  return std::nullopt;
}

}  // namespace

double ChartViewModel::price_at(float py) const {
  return frame_.hi - (py - 4.5) / (frame_.price_h - 8) * (frame_.hi - frame_.lo);
}

float ChartViewModel::y_of(double price) const {
  return std::round(static_cast<float>((frame_.hi - price) / (frame_.hi - frame_.lo)) * (frame_.price_h - 8)) + 4.5f;
}

std::optional<int> ChartViewModel::hover_bar(const GameState& g) const {
  if (!hover_) return std::nullopt;
  return static_cast<int>(first_bar(g)) + *hover_;
}

ChartAction ChartViewModel::drag_to(float py) {
  const double price = price_at(std::min(std::max(py, 4.0f), frame_.price_h - 4));
  if (!(price > 0)) return {};
  drag_->price = price;
  touch();
  return drag_->target.draft ? draft_action(drag_->target, price) : ChartAction{};
}

ChartAction ChartViewModel::pointer_move(const GameState& g, float x, float y, bool picking) {
  if (drag_) return drag_to(y);
  const bool grab = !picking && hit_test(frame_.zones, x, y, frame_.width - kAxisW).has_value();
  const float slot_w = (frame_.width - kAxisW) / static_cast<float>(slots(g));
  const int i = slot_w > 0 ? static_cast<int>(std::floor(x / slot_w)) : -1;
  const int visible = static_cast<int>(g.bars.size() - first_bar(g));
  const std::optional<int> hover = i >= 0 && i < visible ? std::optional<int>(i) : std::nullopt;
  if (hover != hover_ || grab != grabbable_ || hover_y_ != y) touch();
  hover_ = hover;
  hover_y_ = y;
  grabbable_ = grab;
  return {};
}

void ChartViewModel::pointer_down(float x, float y, bool picking) {
  if (picking) return;
  const auto target = hit_test(frame_.zones, x, y, frame_.width - kAxisW);
  if (!target) return;
  double from = price_at(y);
  for (const auto& z : frame_.zones) {
    if (z.target == *target) from = z.price;
  }
  drag_ = Drag{*target, from, from, frame_.lo, frame_.hi};
  touch();
}

ChartAction ChartViewModel::pointer_up() {
  if (!drag_) return {};
  const Drag d = *drag_;
  drag_.reset();
  suppress_click_ = true;  // the click that ends a drag is not a bar click
  touch();
  if (d.target.draft) return draft_action(d.target, d.price);
  return {ChartAction::Kind::MoveOrder, d.price, d.target.order_id, {}};
}

void ChartViewModel::pointer_leave() {
  if (drag_) return;  // a drag follows the pointer until it is released
  hover_.reset();
  hover_y_.reset();
  grabbable_ = false;
  touch();
}

ChartAction ChartViewModel::click(const GameState& g, float /*x*/, float y, bool picking) {
  if (suppress_click_) {
    suppress_click_ = false;
    return {};
  }
  if (!picking) {
    if (!hover_) return {};
    const auto id = pattern_at(g, static_cast<int>(first_bar(g)) + *hover_, show_patterns);
    return id ? ChartAction{ChartAction::Kind::Pattern, 0, 0, *id} : ChartAction{};
  }
  if (y > frame_.price_h) return {};
  const double price = price_at(y);
  return price > 0 ? ChartAction{ChartAction::Kind::Picked, price, 0, {}} : ChartAction{};
}

void ChartViewModel::wheel(const GameState& g, float wheel_y) {
  const int most = std::max(30, static_cast<int>(g.bars.size()));
  const int next = std::min(std::max(30, zoom_ + (wheel_y < 0 ? 10 : -10)), most);
  if (next == zoom_) return;
  zoom_ = next;
  touch();
}

ChartAction ChartViewModel::escape(bool picking) {
  if (drag_) {
    const Drag d = *drag_;
    drag_.reset();
    touch();
    return d.target.draft ? draft_action(d.target, d.from) : ChartAction{};
  }
  if (picking) return {ChartAction::Kind::CancelPick, 0, 0, {}};
  if (table_open) {
    table_open = false;
    touch();
  }
  return {};
}

void ChartViewModel::focus_candle(const GameState& g, int i) {
  focus_ = i;
  const int total = static_cast<int>(g.bars.size());
  if (static_cast<std::size_t>(i) < first_bar(g)) zoom_ = std::min(std::max(30, total - i), total);
  hover_ = i - static_cast<int>(first_bar(g));
  touch();
}

ChartAction ChartViewModel::key(const GameState& g, std::string_view key) {
  if (key == "Enter" && focus_) {
    const auto id = pattern_at(g, *focus_, show_patterns);
    return id ? ChartAction{ChartAction::Kind::Pattern, 0, 0, *id} : ChartAction{};
  }
  const auto next = step_focus(key, focus_, static_cast<int>(g.bars.size()));
  if (next) focus_candle(g, *next);
  return {};
}

void ChartViewModel::focus_gained(const GameState& g) {
  if (!focus_ && !g.bars.empty()) focus_candle(g, static_cast<int>(g.bars.size()) - 1);
}

void ChartViewModel::focus_lost() {
  focus_.reset();
  hover_.reset();
  touch();
}

void ChartViewModel::page_by(const GameState& g, int delta) {
  const BarPage p = page(g);
  const int to = p.page + delta;
  table_page = to >= p.pages - 1 ? std::nullopt : std::optional<int>(std::max(0, to));
  touch();
}

std::string ChartViewModel::announcement(const Texts& t, const GameState& g) const {
  if (!focus_ || static_cast<std::size_t>(*focus_) >= g.bars.size()) return {};
  std::vector<std::string> names;
  for (const auto& p : g.indicators.patterns) {
    if (show_patterns && p.step == *focus_) names.push_back(pattern_name(t, p.id));
  }
  const GameBar& b = g.bars[static_cast<std::size_t>(*focus_)];
  std::string text = candle_text(t, *focus_, b, names);
  if (b.continuity) text += " (" + t.tr("chart.noTrading") + ")";
  return text;
}

std::string ChartViewModel::readout(const Texts& t, const GameState& g) const {
  if (g.bars.empty()) return t.tr("chart.noData");
  const Locale L = t.locale();
  const std::size_t i = std::min(hover_ ? first_bar(g) + static_cast<std::size_t>(*hover_) : g.bars.size() - 1,
                                 g.bars.size() - 1);
  const GameBar& b = g.bars[i];
  std::string out = "#" + std::to_string(i) + "  O " + fmt::fmt_num(b.open, L, 2) + "  H " + fmt::fmt_num(b.high, L, 2) +
                    "  L " + fmt::fmt_num(b.low, L, 2) + "  C " + fmt::fmt_num(b.close, L, 2) + "  V " +
                    fmt::fmt_num(b.volume, L, 1);
  if (b.continuity) out += "  " + t.tr("chart.noTrading");
  const Indicators& ind = g.indicators;
  out += "  RSI14 " + fmt_osc(ind.rsi14, i, L, 1) + "  ATR14 " + fmt_osc(ind.atr14, i, L, 2);
  if (show_bb) out += "  BB " + fmt_osc(ind.bb_lower, i, L, 2) + "\xE2\x80\x93" + fmt_osc(ind.bb_upper, i, L, 2);
  if (show_vwap) out += "  VWAP " + fmt_osc(ind.vwap, i, L, 2);
  if (osc == Oscillator::Macd) out += "  MACD " + fmt_osc(ind.macd, i, L, 3) + " / " + fmt_osc(ind.macd_signal, i, L, 3);
  if (osc == Oscillator::Adx) {
    out += "  ADX " + fmt_osc(ind.adx14, i, L, 1) + " \xC2\xB7 +DI " + fmt_osc(ind.plus_di14, i, L, 1) +
           " \xC2\xB7 \xE2\x88\x92" "DI " + fmt_osc(ind.minus_di14, i, L, 1);
  }
  return out;
}

std::vector<std::pair<std::string, int>> ChartViewModel::hover_patterns(const Texts& t, const GameState& g) const {
  std::vector<std::pair<std::string, int>> out;
  if (!show_patterns || g.bars.empty()) return out;
  const int bar = hover_ ? static_cast<int>(first_bar(g)) + *hover_ : static_cast<int>(g.bars.size()) - 1;
  for (const auto& p : g.indicators.patterns) {
    if (p.step == bar) out.emplace_back(pattern_name(t, p.id), p.bias);
  }
  return out;
}

std::string ChartViewModel::status_line(const Texts& t, const GameState& g, bool picking) const {
  std::vector<std::string> parts;
  const int ema = warming(g.indicators.ema50, 50, g.bars.size());
  if (ema > 0) parts.push_back(t.tr("chart.warming", {{"name", "EMA50"}, {"n", std::to_string(ema)}}));
  long gaps = 0;
  for (std::size_t i = first_bar(g); i < g.bars.size(); ++i) gaps += g.bars[i].continuity ? 1 : 0;
  if (gaps > 0) parts.push_back("\xE2\x96\x92 " + t.count("chart.continuityBars", gaps));  // ▒
  if (picking) parts.push_back("\xE2\x8C\x96 " + t.tr("chart.pickHint"));                  // ⌖
  std::string out;
  for (const auto& p : parts) out += (out.empty() ? "" : "  \xC2\xB7  ") + p;
  return out;
}

}  // namespace tradelab::desktop::game
