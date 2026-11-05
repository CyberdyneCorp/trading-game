#include "viewmodel/game/chart_vm.hpp"

#include <algorithm>
#include <cmath>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

namespace {

struct Level {
  double price = 0;
  std::string label;
  Ink ink = Ink::Muted;
  float dash = 0, gap = 0;
  std::optional<DragTarget> drag;
};

struct Layout {
  float price_h = 0, vol_top = 0, vol_h = 0, osc_top = 0, osc_h = 0;
};

Layout layout_for(float height, Oscillator osc) {
  const float osc_h = osc == Oscillator::None ? 0 : std::floor(height * 0.24f);
  const float vol_h = std::floor(height * (osc == Oscillator::None ? 0.2f : 0.12f));
  const float price_h = height - osc_h - vol_h - 8;
  return {price_h, price_h + 4, vol_h - 4, price_h + vol_h + 8, osc_h - 4};
}

std::string order_label(const Order& o) {
  std::string kind = "LMT";
  if (o.type == "stop_market") kind = "STOP";
  else if (o.type == "trailing_stop") kind = "TRAIL";
  else if (o.type == "take_profit") kind = "TP";
  return kind + " #" + std::to_string(o.id);
}

std::vector<Level> state_levels(const GameState& g) {
  std::vector<Level> out;
  if (g.position.qty != 0) out.push_back({g.position.entry, "ENTRY", Ink::Info, 0, 0, std::nullopt});
  if (g.account.liquidation) out.push_back({*g.account.liquidation, "LIQ", Ink::Danger, 6, 3, std::nullopt});
  for (const auto& o : g.orders) {
    if (o.type == "market" || o.series) continue;  // option limits are premiums, not underlying prices
    const bool is_stop = o.type == "stop_market" || o.type == "trailing_stop";
    Level l{o.price, order_label(o), is_stop ? Ink::Attention : Ink::Accent, 3, 3, std::nullopt};
    if (is_draggable_order(o)) l.drag = DragTarget{false, false, o.id};
    out.push_back(std::move(l));
  }
  return out;
}

void add_draft_levels(std::vector<Level>& out, const std::optional<TicketPreview>& p) {
  if (!p) return;
  out.push_back({p->entry, "ENTRY?", Ink::Muted, 1, 4, std::nullopt});
  if (p->stop) out.push_back({*p->stop, "SL?", Ink::Attention, 1, 4, DragTarget{true, true, 0}});
  if (p->take_profit) out.push_back({*p->take_profit, "TP?", Ink::Accent, 1, 4, DragTarget{true, false, 0}});
  if (p->liquidation) out.push_back({*p->liquidation, "LIQ?", Ink::Danger, 1, 4, std::nullopt});
}

// One frame of drawing: the geometry every part shares.
class Builder {
 public:
  Builder(const GameState& g, ChartScene& scene, std::size_t first, float width, float height, Oscillator osc)
      : g_(g), s_(scene), first_(first), plot_w_(width - ChartViewModel::kAxisW) {
    lay_ = layout_for(height, osc);
    count_ = g.bars.size() - first;
    const int future = g.active() ? ChartViewModel::kFutureSlots : 1;
    slot_w_ = plot_w_ / static_cast<float>(count_ + static_cast<std::size_t>(future));
  }

  void set_scale(double lo, double hi) {
    lo_ = lo;
    hi_ = hi;
  }
  [[nodiscard]] float y(double p) const {
    return std::round(static_cast<float>((hi_ - p) / (hi_ - lo_)) * (lay_.price_h - 8)) + 4.5f;
  }
  [[nodiscard]] float x(double i) const { return std::round(static_cast<float>(i) * slot_w_ + slot_w_ / 2) + 0.5f; }
  [[nodiscard]] const Layout& layout() const { return lay_; }
  [[nodiscard]] float slot_w() const { return slot_w_; }
  [[nodiscard]] float plot_w() const { return plot_w_; }

  void line(float x0, float y0, float x1, float y1, Ink ink, float alpha = 1, float dash = 0, float gap = 0,
            float w = 1) {
    s_.lines.push_back({x0, y0, x1, y1, ink, alpha, w, dash, gap});
  }
  void rect(float x, float y, float w, float h, Ink ink, float alpha = 1, bool outline = false) {
    s_.rects.push_back({x, y, w, h, ink, alpha, outline});
  }
  void text(float x, float y, std::string t, Ink ink, float size = 12, Anchor a = Anchor::Left) {
    s_.texts.push_back({x, y, std::move(t), ink, size, a});
  }

  void grid(const std::vector<float>& tag_y, Locale L) {
    for (int i = 0; i <= 6; ++i) {
      const double p = lo_ + (hi_ - lo_) * i / 6;
      const float yy = y(p);
      line(0, yy, plot_w_, yy, Ink::Line, 0.35f);
      if (yy < lay_.price_h && !hits_tag(yy, tag_y)) text(plot_w_ + 6, yy - 8, fmt::fmt_num(p, L, 2), Ink::Muted);
    }
  }

  void warmup() {
    const long warm_end = static_cast<long>(g_.warmup_bars) - static_cast<long>(first_);
    if (warm_end <= 0) return;
    const float w = static_cast<float>(std::min<long>(warm_end, static_cast<long>(count_))) * slot_w_;
    rect(0, 0, w, lay_.price_h, Ink::Info, 0.04f);
  }

  void future(const std::string& label) {
    if (!g_.active()) return;
    const float x0 = static_cast<float>(count_) * slot_w_;
    const float h = lay_.price_h;
    for (float i = -h; i < plot_w_ - x0; i += 10) {
      // Hatching clipped to the zone: each stroke from (x0+i, h) to (x0+i+h, 0).
      const float ax = std::max(x0, x0 + i);
      const float ay = h - (ax - (x0 + i));
      const float bx = std::min(plot_w_, x0 + i + h);
      const float by = h - (bx - (x0 + i));
      if (bx > ax) line(ax, ay, bx, by, Ink::Muted, 0.12f);
    }
    // The web writes it vertically; here it is written across only when the zone is wide enough.
    if (static_cast<float>(label.size()) * 8 < plot_w_ - x0 - 8) {
      text(x0 + (plot_w_ - x0) / 2, h / 2 - 9, label, Ink::Muted, 16, Anchor::Center);
    }
  }

  void volume() {
    const auto candles = drawn_candles(g_.bars, first_, slot_w_, false);
    double max_v = 1;
    for (const auto& k : candles) max_v = std::max(max_v, k.v);
    const float w = std::max(1.0f, std::round(slot_w_ * 0.7f));
    for (const auto& k : candles) {
      const float vh = std::max(1.0f, std::round(static_cast<float>(k.v / max_v) * lay_.vol_h));
      rect(std::round(x(k.at) - slot_w_ * 0.35f), lay_.vol_top + lay_.vol_h - vh, w, vh, k.c >= k.o ? Ink::Up : Ink::Down,
           0.45f);
    }
  }

  // Up candles hollow, down candles filled: direction is readable without colour.
  void candles() {
    const float body_w = std::max(1.0f, std::floor(slot_w_ * 0.6f));
    for (const auto& k : drawn_candles(g_.bars, first_, slot_w_, true)) {
      const bool rising = k.c >= k.o;
      const Ink ink = rising ? Ink::Up : Ink::Down;
      const float cx = x(k.at);
      line(cx, y(k.h), cx, y(k.l), ink);
      const float top = y(std::max(k.o, k.c));
      const float bh = std::max(1.0f, y(std::min(k.o, k.c)) - top);
      const float bx = std::round(cx - body_w / 2) + 0.5f;
      if (rising) {
        rect(bx, top, body_w, bh, Ink::Bg);
        rect(bx, top, body_w, bh, ink, 1, true);
      } else {
        rect(bx - 0.5f, top - 0.5f, body_w + 1, bh + 1, ink);
      }
    }
  }

  // Continuity bars (no trading): a shaded slot with a dashed line at the carried close.
  void continuity() {
    for (std::size_t i = 0; i < count_; ++i) {
      const GameBar& b = g_.bars[first_ + i];
      if (!b.continuity) continue;
      const float cx = x(static_cast<double>(i));
      rect(std::round(cx - slot_w_ / 2), 0, std::max(1.0f, std::round(slot_w_)), lay_.price_h, Ink::Muted, 0.10f);
      line(cx - slot_w_ * 0.4f, y(b.close), cx + slot_w_ * 0.4f, y(b.close), Ink::Muted, 1, 2, 2);
    }
  }

  // The visible slot of a bar, or -1 when it is off the chart.
  [[nodiscard]] long slot_of(int step) const {
    const long i = static_cast<long>(step) - static_cast<long>(first_);
    return i >= 0 && i < static_cast<long>(count_) ? i : -1;
  }

  // ▲ buy fills under the price, ▼ sell fills over it, ✖ liquidations.
  void fill_mark(const EngineEvent& e, long i) {
    const bool buy = e.side == "buy";
    const float py = y(fmt::num(e.price));
    if (e.type == "liquidation") {
      text(x(static_cast<double>(i)), py - 18, "\xE2\x9C\x96", Ink::Danger, 13, Anchor::Center);
      return;
    }
    text(x(static_cast<double>(i)), buy ? py + 2 : py - 18, buy ? "\xE2\x96\xB2" : "\xE2\x96\xBC", buy ? Ink::Info : Ink::Attention,
         13, Anchor::Center);
  }

  void fills() {
    for (const auto& e : g_.events) {
      const long i = slot_of(e.step);
      if ((e.type == "fill" || e.type == "liquidation") && i >= 0) fill_mark(e, i);
    }
  }

  void news() {
    for (const auto& n : g_.news) {
      const long i = static_cast<long>(n.step) - static_cast<long>(first_);
      if (i < 0 || i >= static_cast<long>(count_)) continue;
      const Ink ink = n.tone > 0 ? Ink::Up : n.tone < 0 ? Ink::Down : Ink::Attention;
      const float xx = x(static_cast<double>(i));
      line(xx, 12, xx, lay_.price_h, ink, 1, 2, 3);
      text(xx - 3, 0, "N", ink, 12);
    }
  }

  void series(const Series& values, Ink ink, float alpha = 0.8f, float width = 1) {
    std::optional<std::pair<float, float>> prev;
    for (std::size_t i = 0; i < count_; ++i) {
      const std::size_t at = first_ + i;
      if (at >= values.size() || !values[at]) {
        prev.reset();
        continue;
      }
      const std::pair<float, float> p{x(static_cast<double>(i)), y(*values[at])};
      if (prev) line(prev->first, prev->second, p.first, p.second, ink, alpha, 0, 0, width);
      prev = p;
    }
  }

  void bands() {
    const Indicators& ind = g_.indicators;
    for (std::size_t i = 0; i < count_; ++i) {
      const std::size_t at = first_ + i;
      if (at >= ind.bb_upper.size() || !ind.bb_upper[at] || !ind.bb_lower[at]) continue;
      const float top = y(*ind.bb_upper[at]);
      rect(x(static_cast<double>(i)) - slot_w_ / 2, top, slot_w_, y(*ind.bb_lower[at]) - top, Ink::Bb, 0.06f);
    }
    series(ind.bb_upper, Ink::Bb);
    series(ind.bb_lower, Ink::Bb);
  }

  // Detected candle patterns: a bracket under (bullish) or over (bearish) the bars that form it.
  // Lowest low (bullish: the bracket goes under) or highest high of the visible bars of a pattern.
  [[nodiscard]] double pattern_edge(long start, long end, bool below) const {
    double edge = below ? 1e300 : -1e300;
    for (long i = std::max(0L, start); i <= end; ++i) {
      const GameBar& b = g_.bars[first_ + static_cast<std::size_t>(i)];
      edge = below ? std::min(edge, b.low) : std::max(edge, b.high);
    }
    return edge;
  }

  void patterns() {
    for (const auto& p : g_.indicators.patterns) {
      const long end = slot_of(p.step);
      if (end < 0) continue;
      const long start = end - p.bars + 1;
      const bool below = p.bias >= 0;
      const float yy = y(pattern_edge(start, end, below)) + (below ? 6.0f : -6.0f);
      const Ink ink = p.bias > 0 ? Ink::Up : p.bias < 0 ? Ink::Down : Ink::Attention;
      line(x(static_cast<double>(std::max(0L, start))) - slot_w_ * 0.35f, yy, x(static_cast<double>(end)) + slot_w_ * 0.35f,
           yy, ink, 0.85f);
      rect(x(static_cast<double>(end)) - 2, yy + (below ? 1.0f : -5.0f), 4, 4, ink, 0.85f);
    }
  }

  // Lines at their price, or pinned to the pane edge with an arrow when off the scale; tags spread
  // apart so nearby levels stay readable. Returns the drag zones of this frame.
  std::vector<DragZone> levels(const std::vector<Level>& lv, Locale L) {
    std::vector<const Level*> shown;
    std::vector<Pin> pins;
    std::vector<float> line_y;
    for (const auto& l : lv) {
      if (!(l.price > 0)) continue;
      const Pin pin = pin_of(l.price, lo_, hi_);
      shown.push_back(&l);
      pins.push_back(pin);
      line_y.push_back(pin == Pin::Above ? 4.5f : pin == Pin::Below ? lay_.price_h - 3.5f : y(l.price));
    }
    tag_y_ = spread_labels(line_y, 0, lay_.price_h);
    std::vector<DragZone> zones;
    for (std::size_t i = 0; i < shown.size(); ++i) {
      draw_level(*shown[i], line_y[i], tag_y_[i], pins[i], L);
      if (shown[i]->drag) zones.push_back({*shown[i]->drag, shown[i]->price, line_y[i], tag_y_[i]});
    }
    return zones;
  }

  [[nodiscard]] std::vector<float> tag_positions(const std::vector<Level>& lv) const {
    std::vector<float> ys;
    for (const auto& l : lv) {
      if (!(l.price > 0)) continue;
      const Pin pin = pin_of(l.price, lo_, hi_);
      ys.push_back(pin == Pin::Above ? 4.5f : pin == Pin::Below ? lay_.price_h - 3.5f : y(l.price));
    }
    return spread_labels(ys, 0, lay_.price_h);
  }

  void hover(std::optional<int> at, bool osc) {
    if (!at || *at < 0 || *at >= static_cast<int>(count_)) return;
    const float bottom = osc ? lay_.osc_top + lay_.osc_h : lay_.price_h;
    line(x(*at), 0, x(*at), bottom, Ink::Text, 0.35f);
  }

  void pick(std::optional<float> hover_y, double price_at_hover, Locale L) {
    if (!hover_y || *hover_y > lay_.price_h) return;
    const float yy = y(price_at_hover);
    line(0, yy, plot_w_, yy, Ink::Attention, 1, 4, 2);
    rect(plot_w_, yy - 8, ChartViewModel::kAxisW, 16, Ink::Attention);
    text(plot_w_ + 6, yy - 7, fmt::fmt_num(price_at_hover, L, 2), Ink::Bg, 11);
  }

  // ---- oscillator panes (chartPanes.ts) ----

  void pane_frame(const char* title) {
    line(0, lay_.osc_top - 0.5f, plot_w_, lay_.osc_top - 0.5f, Ink::Line);
    text(4, lay_.osc_top + 1, title, Ink::Muted, 11);
  }

  [[nodiscard]] float pane_y(double v, double lo, double hi) const {
    const double span = hi - lo != 0 ? hi - lo : 1;
    return lay_.osc_top + std::round(static_cast<float>((hi - v) / span) * (lay_.osc_h - 4)) + 2.5f;
  }

  void guide(float yy, Ink ink, const std::string& label) {
    line(0, yy, plot_w_, yy, ink, 0.5f, 3, 4);
    text(plot_w_ + 6, yy - 7, label, ink, 11);
  }

  void pane_series(const Series& values, double lo, double hi, Ink ink, float width = 1) {
    std::optional<std::pair<float, float>> prev;
    for (std::size_t i = 0; i < count_; ++i) {
      const std::size_t at = first_ + i;
      if (at >= values.size() || !values[at]) {
        prev.reset();
        continue;
      }
      const std::pair<float, float> p{x(static_cast<double>(i)), pane_y(*values[at], lo, hi)};
      if (prev) line(prev->first, prev->second, p.first, p.second, ink, 1, 0, 0, width);
      prev = p;
    }
  }

  [[nodiscard]] Range visible_range(std::initializer_list<const Series*> lists) const {
    Range r{1e300, -1e300};
    for (const Series* list : lists) {
      for (std::size_t i = first_; i < std::min(list->size(), g_.bars.size()); ++i) {
        if (!(*list)[i]) continue;
        r.lo = std::min(r.lo, *(*list)[i]);
        r.hi = std::max(r.hi, *(*list)[i]);
      }
    }
    if (r.lo > r.hi) return {-1, 1};
    return r;
  }

  void rsi() {
    pane_frame("RSI 14");
    guide(pane_y(70, 0, 100), Ink::Down, "70");
    guide(pane_y(30, 0, 100), Ink::Up, "30");
    pane_series(g_.indicators.rsi14, 0, 100, Ink::Info);
  }

  void macd() {
    const Indicators& ind = g_.indicators;
    pane_frame("MACD 12 26 9");
    const Range r = visible_range({&ind.macd, &ind.macd_signal, &ind.macd_hist});
    double m = std::max(std::fabs(r.lo), std::fabs(r.hi)) * 1.1;
    if (m == 0) m = 1;
    guide(pane_y(0, -m, m), Ink::Muted, "0");
    const float bar_w = std::max(1.0f, std::round(slot_w_ * 0.6f));
    for (std::size_t i = 0; i < count_; ++i) {
      const std::size_t at = first_ + i;
      if (at >= ind.macd_hist.size() || !ind.macd_hist[at]) continue;
      const double h = *ind.macd_hist[at];
      const float top = std::min(pane_y(h, -m, m), pane_y(0, -m, m));
      rect(std::round(x(static_cast<double>(i)) - bar_w / 2), top, bar_w,
           std::max(1.0f, std::fabs(pane_y(h, -m, m) - pane_y(0, -m, m))), h >= 0 ? Ink::Up : Ink::Down, 0.45f);
    }
    pane_series(ind.macd, -m, m, Ink::Info);
    pane_series(ind.macd_signal, -m, m, Ink::Attention);
  }

  void adx() {
    const Indicators& ind = g_.indicators;
    pane_frame("ADX 14  +DI  \xE2\x88\x92" "DI");
    const Range r = visible_range({&ind.adx14, &ind.plus_di14, &ind.minus_di14});
    const double hi = std::max(50.0, r.hi * 1.05);
    guide(pane_y(25, 0, hi), Ink::Muted, "25");
    pane_series(ind.plus_di14, 0, hi, Ink::Up);
    pane_series(ind.minus_di14, 0, hi, Ink::Down);
    pane_series(ind.adx14, 0, hi, Ink::Attention, 2);
  }

 private:
  void draw_level(const Level& l, float yy, float tag, Pin pin, Locale L) {
    const float from = pin != Pin::None ? plot_w_ * 0.6f : 0;
    line(from, yy, plot_w_, yy, l.ink, 1, pin != Pin::None ? 2 : l.dash, pin != Pin::None ? 6 : l.gap);
    std::string name = l.drag ? "\xE2\x87\x95 " + l.label : l.label;  // ⇕
    if (pin == Pin::Above) name += " \xE2\x96\xB2";
    if (pin == Pin::Below) name += " \xE2\x96\xBC";
    text(plot_w_ - 4, std::max(tag - 4, 11.0f) - 11, name, l.ink, 11, Anchor::Right);
    rect(plot_w_, tag - 8, ChartViewModel::kAxisW, 16, l.ink);
    text(plot_w_ + 6, tag - 7, fmt::fmt_num(l.price, L, 2), Ink::Bg, 11);
  }

  const GameState& g_;
  ChartScene& s_;
  std::size_t first_;
  float plot_w_;
  Layout lay_;
  std::size_t count_ = 0;
  float slot_w_ = 1;
  double lo_ = 0, hi_ = 1;
  std::vector<float> tag_y_;
};

void apply_drag(std::vector<Level>& lv, const GameState& g, const DragTarget& target, double price) {
  for (auto& l : lv) {
    if (!l.drag || !(*l.drag == target)) continue;
    l.price = price;
    l.dash = 0;
    if (target.draft) continue;
    for (const auto& o : g.orders) {
      if (o.id == target.order_id && !exit_on_valid_side(o.side, o.type, price, g.account.mark)) l.ink = Ink::Danger;
    }
  }
}

}  // namespace

std::size_t ChartViewModel::first_bar(const GameState& g) const {
  return g.bars.size() > static_cast<std::size_t>(zoom_) ? g.bars.size() - static_cast<std::size_t>(zoom_) : 0;
}

int ChartViewModel::slots(const GameState& g) const {
  return static_cast<int>(g.bars.size() - first_bar(g)) + (g.active() ? kFutureSlots : 1);
}

ChartScene ChartViewModel::build(const Texts& t, const GameState& g, const std::optional<TicketPreview>& preview,
                                 bool picking, float width, float height) {
  ChartScene scene;
  frame_.width = width;
  frame_.height = height;
  frame_.first = first_bar(g);
  frame_.zones.clear();
  if (g.bars.empty() || width <= kAxisW || height <= 40) return scene;
  scene.empty = false;
  const Locale L = t.locale();

  std::vector<Level> lv = state_levels(g);
  add_draft_levels(lv, preview);
  if (drag_) apply_drag(lv, g, drag_->target, drag_->price);

  Builder b(g, scene, frame_.first, width, height, osc);
  std::vector<double> prices;
  prices.reserve(lv.size());
  for (const auto& l : lv) prices.push_back(l.price);
  const Range candles = candle_range(g.bars, frame_.first);
  const Range scale = drag_ ? Range{drag_->lo, drag_->hi} : price_range(candles.lo, candles.hi, prices);
  b.set_scale(scale.lo, scale.hi);
  frame_.lo = scale.lo;
  frame_.hi = scale.hi;
  frame_.price_h = b.layout().price_h;
  frame_.slot_w = b.slot_w();

  b.grid(b.tag_positions(lv), L);
  b.warmup();
  b.future(t.tr("chart.future"));
  b.volume();
  b.candles();
  b.continuity();
  b.fills();
  b.news();
  if (show_sma) b.series(g.indicators.sma20, Ink::Attention);
  if (show_ema) b.series(g.indicators.ema50, Ink::Info);
  if (show_bb) b.bands();
  if (show_patterns) b.patterns();
  if (show_vwap) b.series(g.indicators.vwap, Ink::Vwap);
  frame_.zones = b.levels(lv, L);
  if (osc == Oscillator::Rsi) b.rsi();
  if (osc == Oscillator::Macd) b.macd();
  if (osc == Oscillator::Adx) b.adx();
  b.hover(hover_, osc != Oscillator::None);
  if (picking && hover_y_) b.pick(hover_y_, price_at(*hover_y_), L);
  return scene;
}

}  // namespace tradelab::desktop::game

