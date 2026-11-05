#include "tradelab/candle_patterns.hpp"

#include "gaps.hpp"

#include <algorithm>
#include <array>

namespace tradelab {
namespace {

constexpr std::uint32_t kLookback = 10;     // bars used for average body/range
constexpr std::uint32_t kTrendSpan = 5;     // trend-before window
constexpr std::uint32_t kFirstBar = kLookback + kTrendSpan + 5;  // enough history for any pattern

struct Shape {
  std::int64_t o, h, l, c;
  [[nodiscard]] std::int64_t body() const { return c > o ? c - o : o - c; }
  [[nodiscard]] std::int64_t range() const { return h - l; }
  [[nodiscard]] std::int64_t top() const { return std::max(o, c); }
  [[nodiscard]] std::int64_t bottom() const { return std::min(o, c); }
  [[nodiscard]] std::int64_t upper() const { return h - top(); }
  [[nodiscard]] std::int64_t lower() const { return bottom() - l; }
  [[nodiscard]] bool bull() const { return c > o; }
  [[nodiscard]] bool bear() const { return c < o; }
  [[nodiscard]] bool doji() const { return range() > 0 && body() * 10 <= range(); }
};

Shape shape(const Bar& b) { return {b.open.raw, b.high.raw, b.low.raw, b.close.raw}; }

struct Ctx {
  const Scenario& sc;
  std::uint32_t i;  // bar that would complete the pattern
  std::int64_t avg_body;
  std::int64_t avg_range;

  // Bar k positions before the completing bar (0 = the completing bar itself).
  [[nodiscard]] Shape at(std::uint32_t k) const { return shape(sc.bars[i - k]); }
  [[nodiscard]] bool long_body(const Shape& s) const { return s.body() >= avg_body; }

  // Trend before a pattern of `len` bars: -1 down, +1 up, 0 none.
  [[nodiscard]] int trend(std::uint32_t len) const {
    const std::uint32_t before = i - len;
    const std::int64_t move = sc.bars[before].close.raw - sc.bars[before - kTrendSpan].close.raw;
    if (move > avg_body) return 1;
    if (move < -avg_body) return -1;
    return 0;
  }
};

// ---- shared two-bar relations ---------------------------------------------------------------

bool engulfs_up(const Shape& p, const Shape& c) { return p.bear() && c.bull() && c.o <= p.c && c.c >= p.o && c.body() > p.body(); }
bool engulfs_down(const Shape& p, const Shape& c) { return p.bull() && c.bear() && c.o >= p.c && c.c <= p.o && c.body() > p.body(); }
bool harami_up(const Ctx& x, const Shape& p, const Shape& c) {
  return p.bear() && x.long_body(p) && c.bull() && c.top() <= p.o && c.bottom() >= p.c && c.body() * 2 <= p.body();
}
bool harami_down(const Ctx& x, const Shape& p, const Shape& c) {
  return p.bull() && x.long_body(p) && c.bear() && c.top() <= p.c && c.bottom() >= p.o && c.body() * 2 <= p.body();
}
bool lower_wick_shape(const Shape& s) { return !s.doji() && s.lower() * 3 >= s.range() * 2 && s.upper() * 10 <= s.range(); }
bool upper_wick_shape(const Shape& s) { return !s.doji() && s.upper() * 3 >= s.range() * 2 && s.lower() * 10 <= s.range(); }
bool marubozu_shape(const Ctx& x, const Shape& s) {
  return s.body() * 2 >= x.avg_body * 3 && s.upper() * 20 <= s.range() && s.lower() * 20 <= s.range();
}
bool small_inside(const Shape& ref, const Shape& s) { return s.body() * 2 <= ref.body() && s.h <= ref.h && s.l >= ref.l; }

// ---- one bar ---------------------------------------------------------------------------------

bool doji(const Ctx& x) { return x.at(0).doji() && x.trend(1) != 0; }
bool hammer(const Ctx& x) { return x.trend(1) < 0 && lower_wick_shape(x.at(0)); }
bool hanging_man(const Ctx& x) { return x.trend(1) > 0 && lower_wick_shape(x.at(0)); }
bool inverted_hammer(const Ctx& x) { return x.trend(1) < 0 && upper_wick_shape(x.at(0)); }
bool shooting_star(const Ctx& x) { return x.trend(1) > 0 && upper_wick_shape(x.at(0)); }
bool bullish_marubozu(const Ctx& x) { return x.at(0).bull() && marubozu_shape(x, x.at(0)); }
bool bearish_marubozu(const Ctx& x) { return x.at(0).bear() && marubozu_shape(x, x.at(0)); }

// ---- two bars --------------------------------------------------------------------------------

bool bullish_engulfing(const Ctx& x) { return x.trend(2) < 0 && engulfs_up(x.at(1), x.at(0)); }
bool bearish_engulfing(const Ctx& x) { return x.trend(2) > 0 && engulfs_down(x.at(1), x.at(0)); }
bool bullish_harami(const Ctx& x) { return x.trend(2) < 0 && harami_up(x, x.at(1), x.at(0)); }
bool bearish_harami(const Ctx& x) { return x.trend(2) > 0 && harami_down(x, x.at(1), x.at(0)); }

bool piercing_line(const Ctx& x) {
  const Shape p = x.at(1);
  const Shape c = x.at(0);
  return x.trend(2) < 0 && p.bear() && x.long_body(p) && c.bull() && c.o <= p.c && c.c * 2 > p.o + p.c && c.c < p.o;
}

bool dark_cloud_cover(const Ctx& x) {
  const Shape p = x.at(1);
  const Shape c = x.at(0);
  return x.trend(2) > 0 && p.bull() && x.long_body(p) && c.bear() && c.o >= p.c && c.c * 2 < p.o + p.c && c.c > p.o;
}

bool tweezer_bottom(const Ctx& x) {
  const Shape p = x.at(1);
  const Shape c = x.at(0);
  const std::int64_t d = p.l > c.l ? p.l - c.l : c.l - p.l;
  return x.trend(2) < 0 && p.bear() && c.bull() && d * 20 <= x.avg_range;
}

bool tweezer_top(const Ctx& x) {
  const Shape p = x.at(1);
  const Shape c = x.at(0);
  const std::int64_t d = p.h > c.h ? p.h - c.h : c.h - p.h;
  return x.trend(2) > 0 && p.bull() && c.bear() && d * 20 <= x.avg_range;
}

bool bullish_kicker(const Ctx& x) {
  const Shape p = x.at(1);
  const Shape c = x.at(0);
  return x.trend(2) <= 0 && p.bear() && c.bull() && c.o > p.o && x.long_body(p) && x.long_body(c);
}

bool bearish_kicker(const Ctx& x) {
  const Shape p = x.at(1);
  const Shape c = x.at(0);
  return x.trend(2) >= 0 && p.bull() && c.bear() && c.o < p.o && x.long_body(p) && x.long_body(c);
}

// ---- three bars ------------------------------------------------------------------------------

bool morning_star(const Ctx& x) {
  const Shape a = x.at(2);
  const Shape b = x.at(1);
  const Shape c = x.at(0);
  return x.trend(3) < 0 && a.bear() && x.long_body(a) && b.body() * 3 <= a.body() && b.top() <= a.c && c.bull() &&
         c.c * 2 > a.o + a.c;
}

bool evening_star(const Ctx& x) {
  const Shape a = x.at(2);
  const Shape b = x.at(1);
  const Shape c = x.at(0);
  return x.trend(3) > 0 && a.bull() && x.long_body(a) && b.body() * 3 <= a.body() && b.bottom() >= a.c && c.bear() &&
         c.c * 2 < a.o + a.c;
}

// Three long candles in one direction, each opening inside the previous body and closing near its extreme.
bool soldiers(const Ctx& x, bool up) {
  for (std::uint32_t k = 0; k < 3; ++k) {
    const Shape s = x.at(k);
    const bool dir = up ? s.bull() : s.bear();
    const std::int64_t tail = up ? s.upper() : s.lower();
    if (!dir || !x.long_body(s) || tail * 3 > s.body()) return false;
    if (k == 2) continue;
    const Shape p = x.at(k + 1);
    const bool progress = up ? s.c > p.c : s.c < p.c;
    if (!progress || s.o < p.bottom() || s.o > p.top()) return false;
  }
  return true;
}

bool three_white_soldiers(const Ctx& x) { return soldiers(x, true); }
bool three_black_crows(const Ctx& x) { return soldiers(x, false); }
bool three_inside_up(const Ctx& x) { return x.trend(3) < 0 && harami_up(x, x.at(2), x.at(1)) && x.at(0).bull() && x.at(0).c > x.at(2).o; }
bool three_inside_down(const Ctx& x) { return x.trend(3) > 0 && harami_down(x, x.at(2), x.at(1)) && x.at(0).bear() && x.at(0).c < x.at(2).o; }
bool three_outside_up(const Ctx& x) { return x.trend(3) < 0 && engulfs_up(x.at(2), x.at(1)) && x.at(0).bull() && x.at(0).c > x.at(1).c; }
bool three_outside_down(const Ctx& x) {
  return x.trend(3) > 0 && engulfs_down(x.at(2), x.at(1)) && x.at(0).bear() && x.at(0).c < x.at(1).c;
}

bool bullish_abandoned_baby(const Ctx& x) {
  const Shape a = x.at(2);
  const Shape b = x.at(1);
  const Shape c = x.at(0);
  return x.trend(3) < 0 && a.bear() && x.long_body(a) && b.doji() && b.h < a.l && c.bull() && c.l > b.h;
}

bool bearish_abandoned_baby(const Ctx& x) {
  const Shape a = x.at(2);
  const Shape b = x.at(1);
  const Shape c = x.at(0);
  return x.trend(3) > 0 && a.bull() && x.long_body(a) && b.doji() && b.l > a.h && c.bear() && c.h < b.l;
}

// ---- five bars -------------------------------------------------------------------------------

bool three_methods(const Ctx& x, bool up) {
  const Shape first = x.at(4);
  const Shape last = x.at(0);
  const bool ends = up ? (first.bull() && last.bull() && last.c > first.c) : (first.bear() && last.bear() && last.c < first.c);
  if (x.trend(5) != (up ? 1 : -1) || !ends || !x.long_body(first) || !x.long_body(last)) return false;
  for (std::uint32_t k = 1; k <= 3; ++k) {
    if (!small_inside(first, x.at(k))) return false;
  }
  return true;
}

bool rising_three_methods(const Ctx& x) { return three_methods(x, true); }
bool falling_three_methods(const Ctx& x) { return three_methods(x, false); }

struct Detector {
  std::string_view id;
  std::uint8_t bars;
  std::int8_t bias;
  bool (*fn)(const Ctx&);
};

constexpr std::array kDetectors{
    Detector{"doji", 1, 0, doji},
    Detector{"hammer", 1, 1, hammer},
    Detector{"hanging-man", 1, -1, hanging_man},
    Detector{"inverted-hammer", 1, 1, inverted_hammer},
    Detector{"shooting-star", 1, -1, shooting_star},
    Detector{"bullish-marubozu", 1, 1, bullish_marubozu},
    Detector{"bearish-marubozu", 1, -1, bearish_marubozu},
    Detector{"bullish-engulfing", 2, 1, bullish_engulfing},
    Detector{"bearish-engulfing", 2, -1, bearish_engulfing},
    Detector{"bullish-harami", 2, 1, bullish_harami},
    Detector{"bearish-harami", 2, -1, bearish_harami},
    Detector{"piercing-line", 2, 1, piercing_line},
    Detector{"dark-cloud-cover", 2, -1, dark_cloud_cover},
    Detector{"tweezer-bottom", 2, 1, tweezer_bottom},
    Detector{"tweezer-top", 2, -1, tweezer_top},
    Detector{"bullish-kicker", 2, 1, bullish_kicker},
    Detector{"bearish-kicker", 2, -1, bearish_kicker},
    Detector{"morning-star", 3, 1, morning_star},
    Detector{"evening-star", 3, -1, evening_star},
    Detector{"three-white-soldiers", 3, 1, three_white_soldiers},
    Detector{"three-black-crows", 3, -1, three_black_crows},
    Detector{"three-inside-up", 3, 1, three_inside_up},
    Detector{"three-inside-down", 3, -1, three_inside_down},
    Detector{"three-outside-up", 3, 1, three_outside_up},
    Detector{"three-outside-down", 3, -1, three_outside_down},
    Detector{"bullish-abandoned-baby", 3, 1, bullish_abandoned_baby},
    Detector{"bearish-abandoned-baby", 3, -1, bearish_abandoned_baby},
    Detector{"rising-three-methods", 5, 1, rising_three_methods},
    Detector{"falling-three-methods", 5, -1, falling_three_methods},
};

Ctx context_at(const Scenario& sc, std::uint32_t i) {
  i128 bodies = 0;
  i128 ranges = 0;
  for (std::uint32_t k = i - kLookback; k < i; ++k) {
    const Shape s = shape(sc.bars[k]);
    bodies += s.body();
    ranges += s.range();
  }
  return {sc, i, static_cast<std::int64_t>(bodies / kLookback), static_cast<std::int64_t>(ranges / kLookback)};
}

}  // namespace

namespace {

std::vector<PatternHit> detect_all(const Scenario& sc, std::uint32_t last) {
  std::vector<PatternHit> hits;
  for (std::uint32_t i = kFirstBar; i <= last && i < sc.bars.size(); ++i) {
    if (sc.bars[i].continuity) continue;  // no candle: the market did not trade
    const Ctx x = context_at(sc, i);
    for (const Detector& d : kDetectors) {
      if (d.fn(x)) hits.push_back({i, d.id, d.bars, d.bias});
    }
  }
  return hits;
}

}  // namespace

// Under GapPolicy::Skip, patterns read the trading bars only (policy v17) and are reported at the
// real bar that completes them; no pattern ever completes at a continuity bar.
std::vector<PatternHit> detect_patterns(const Scenario& sc, std::uint32_t last) {
  if (!detail::skips_gaps(sc, last)) return detect_all(sc, last);
  const detail::TradingBars t = detail::trading_bars(sc, last);
  std::vector<PatternHit> hits = detect_all(t.sc, static_cast<std::uint32_t>(t.index.size() - 1));
  for (PatternHit& h : hits) h.step = t.index[h.step];
  return hits;
}

}  // namespace tradelab
