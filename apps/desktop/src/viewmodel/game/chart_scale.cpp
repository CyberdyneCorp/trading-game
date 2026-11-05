#include "viewmodel/game/chart_scale.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr float kTagGrabPx = 8;

std::string move_text(const Texts& t, double open, double close) {
  if (!(open > 0) || close == open) return t.tr("chart.flat");
  const std::string pct = fmt::fmt_pct(std::fabs(close - open) / open, t.locale());
  return t.tr(close > open ? "chart.up" : "chart.down", {{"pct", pct}});
}

DrawnCandle merge(const std::vector<GameBar>& bars, std::size_t first, const std::vector<std::size_t>& kept) {
  DrawnCandle d;
  d.h = -kInf;
  d.l = kInf;
  for (const std::size_t j : kept) {
    d.h = std::max(d.h, bars[j].high);
    d.l = std::min(d.l, bars[j].low);
    d.v = std::max(d.v, bars[j].volume);
  }
  const std::size_t a = kept.front();
  const std::size_t b = kept.back();
  d.at = (static_cast<double>(a - first) + static_cast<double>(b - first)) / 2;
  d.o = bars[a].open;
  d.c = bars[b].close;
  return d;
}

}  // namespace

Range candle_range(const std::vector<GameBar>& bars, std::size_t first) {
  Range r{kInf, -kInf};
  for (std::size_t i = first; i < bars.size(); ++i) {
    r.lo = std::min(r.lo, bars[i].low);
    r.hi = std::max(r.hi, bars[i].high);
  }
  return r;
}

Range price_range(double lo, double hi, const std::vector<double>& levels) {
  const double span = std::max(hi - lo, hi * 0.01);
  const double reach = span * kNearFraction;
  double a = lo;
  double b = hi;
  for (const double p : levels) {
    if (p > 0 && p >= lo - reach && p <= hi + reach) {
      a = std::min(a, p);
      b = std::max(b, p);
    }
  }
  double pad = (b - a) * 0.08;
  if (pad == 0) pad = 1;
  return {a - pad, b + pad};
}

Pin pin_of(double price, double lo, double hi) {
  if (price > hi) return Pin::Above;
  if (price < lo) return Pin::Below;
  return Pin::None;
}

std::vector<float> spread_labels(const std::vector<float>& ys, float top, float bottom) {
  std::vector<std::size_t> order(ys.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return ys[a] < ys[b]; });
  std::vector<float> out(ys.size());
  const float lo = top + kLabelH / 2;
  const float hi = bottom - kLabelH / 2;
  if (order.size() > 1 && static_cast<float>(order.size() - 1) * kLabelH > hi - lo) {
    const float step = (hi - lo) / static_cast<float>(order.size() - 1);
    for (std::size_t k = 0; k < order.size(); ++k) out[order[k]] = lo + static_cast<float>(k) * step;
    return out;
  }
  float prev = -std::numeric_limits<float>::infinity();
  for (const std::size_t i : order) {
    out[i] = std::max({ys[i], prev + kLabelH, lo});
    prev = out[i];
  }
  float next = hi + kLabelH;
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    out[*it] = std::min(out[*it], next - kLabelH);
    next = out[*it];
  }
  return out;
}

bool hits_tag(float y, const std::vector<float>& tag_ys) {
  return std::any_of(tag_ys.begin(), tag_ys.end(), [&](float t) { return std::fabs(t - y) < kLabelH; });
}

std::vector<DrawnCandle> drawn_candles(const std::vector<GameBar>& bars, std::size_t first, double slot_w,
                                       bool skip_continuity) {
  const std::size_t n = slot_w >= 1 ? 1 : static_cast<std::size_t>(std::ceil(1 / slot_w));
  std::vector<DrawnCandle> out;
  std::vector<std::size_t> kept;
  for (std::size_t i = first; i < bars.size(); i += n) {
    kept.clear();
    for (std::size_t j = i; j < std::min(i + n, bars.size()); ++j) {
      if (!(skip_continuity && bars[j].continuity)) kept.push_back(j);
    }
    if (!kept.empty()) out.push_back(merge(bars, first, kept));
  }
  return out;
}

bool is_draggable_order(const Order& o) {
  return o.reduce_only && o.series == 0 && (o.type == "stop_market" || o.type == "take_profit");
}

std::optional<DragTarget> hit_test(const std::vector<DragZone>& zones, float px, float py, float plot_w) {
  std::optional<DragTarget> best;
  float best_d = std::numeric_limits<float>::infinity();
  for (const auto& z : zones) {
    const float on_line = std::fabs(py - z.line_y);
    const float line_d = on_line <= kLineGrabPx ? on_line : std::numeric_limits<float>::infinity();
    const float tag_d = px >= plot_w && std::fabs(py - z.tag_y) <= kTagGrabPx ? 0 : std::numeric_limits<float>::infinity();
    const float d = std::min(line_d, tag_d);
    if (std::isfinite(d) && d < best_d) {
      best_d = d;
      best = z.target;
    }
  }
  return best;
}

bool exit_on_valid_side(std::string_view side, std::string_view type, double price, double mark) {
  const bool below = price < mark;
  const bool is_stop = type == "stop_market";
  return side == "sell" ? below == is_stop : below != is_stop;
}

bool stop_moved_beyond_liquidation(std::string_view side, std::string_view type, double price,
                                   std::optional<double> liquidation) {
  if (type != "stop_market" || !liquidation) return false;
  return side == "sell" ? price <= *liquidation : price >= *liquidation;
}

BarPage bar_page(int total, std::optional<int> page, int size) {
  const int pages = std::max(1, (total + size - 1) / size);
  const int p = page ? std::min(std::max(0, *page), pages - 1) : pages - 1;
  const int end = total - (pages - 1 - p) * size;
  return {std::max(0, end - size), std::max(0, end), p, pages};
}

std::vector<std::string> candle_cells(const GameBar& b, Locale locale) {
  return {fmt::fmt_num(b.open, locale, 2), fmt::fmt_num(b.high, locale, 2), fmt::fmt_num(b.low, locale, 2),
          fmt::fmt_num(b.close, locale, 2), fmt::fmt_num(b.volume, locale, 0)};
}

std::string candle_text(const Texts& t, int i, const GameBar& b, const std::vector<std::string>& patterns) {
  const Locale L = t.locale();
  std::string base = t.tr("chart.candle", {{"i", std::to_string(i)},
                                           {"o", fmt::fmt_num(b.open, L, 2)},
                                           {"h", fmt::fmt_num(b.high, L, 2)},
                                           {"l", fmt::fmt_num(b.low, L, 2)},
                                           {"c", fmt::fmt_num(b.close, L, 2)},
                                           {"v", fmt::fmt_num(b.volume, L, 0)},
                                           {"move", move_text(t, b.open, b.close)}});
  if (patterns.empty()) return base;
  std::string names;
  for (const auto& p : patterns) names += (names.empty() ? "" : ", ") + p;
  return base + " " + t.tr("chart.candlePatterns", {{"names", names}});
}

std::string chart_summary(const Texts& t, const std::vector<GameBar>& bars) {
  if (bars.empty()) return t.tr("chart.noData");
  const Locale L = t.locale();
  const GameBar& last = bars.back();
  const std::size_t from = bars.size() > 20 ? bars.size() - 20 : 0;
  double lo = kInf;
  double hi = -kInf;
  for (std::size_t i = from; i < bars.size(); ++i) {
    lo = std::min(lo, bars[i].low);
    hi = std::max(hi, bars[i].high);
  }
  return t.tr("chart.summary", {{"n", std::to_string(bars.size())},
                                {"i", std::to_string(bars.size() - 1)},
                                {"close", fmt::fmt_num(last.close, L, 2)},
                                {"move", move_text(t, last.open, last.close)},
                                {"total", move_text(t, bars.front().close, last.close)},
                                {"k", std::to_string(bars.size() - from)},
                                {"lo", fmt::fmt_num(lo, L, 2)},
                                {"hi", fmt::fmt_num(hi, L, 2)}});
}

std::optional<int> step_focus(std::string_view key, std::optional<int> at, int total, int page) {
  if (total == 0) return std::nullopt;
  const int from = at.value_or(total - 1);
  int to = 0;
  if (key == "ArrowLeft") to = from - 1;
  else if (key == "ArrowRight") to = from + 1;
  else if (key == "PageUp") to = from - page;
  else if (key == "PageDown") to = from + page;
  else if (key == "Home") to = 0;
  else if (key == "End") to = total - 1;
  else return std::nullopt;
  return std::min(std::max(0, to), total - 1);
}

}  // namespace tradelab::desktop::game
