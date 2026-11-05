// Advisor (spec advisor, design D2). Reads only the visible market at st.step; see advisor.hpp.

#include "tradelab/advisor.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <vector>

#include "accounting.hpp"
#include "tradelab/candle_patterns.hpp"
#include "tradelab/indicators.hpp"

namespace tradelab {
namespace {

constexpr std::int64_t kOne = Price::kScale;  // RSI and ADX are 0..100 in Price units
constexpr std::int64_t kAdxTrend = 25 * kOne;
constexpr std::int64_t kAdxRange = 20 * kOne;
constexpr std::int64_t kRsiLow = 30 * kOne;
constexpr std::int64_t kRsiHigh = 70 * kOne;
constexpr std::int64_t kCrowdedFunding = 6'000'000;  // 0.06% per funding interval (Rate raw, 1e-10)
constexpr std::uint8_t kCredible = 70;               // headline credibility that moves the market
constexpr std::uint32_t kPricedBars = 3;             // a surprise released this recently is in the price
constexpr std::uint32_t kPatternBars = 2;            // a pattern this many bars old still counts
constexpr std::uint32_t kSqueezeBars = 30;           // ATR at its lowest of this many bars
constexpr std::int32_t kStanceVotes = 2;
constexpr std::uint32_t kEventAheadBars = 3;         // an announced release this close: toward wait

enum class Regime : std::uint8_t { None, Up, Down, Range };

int sign(std::int64_t v) {
  if (v == 0) return 0;
  return v > 0 ? 1 : -1;
}

struct Reader {
  const Scenario& sc;
  std::uint32_t s;
  std::vector<AdviceReason> reasons;
  int damp = 0;  // reasons pushing toward wait

  void vote(AdviceReasonCode code, int v, std::int32_t ref = 0) {
    reasons.push_back({code, static_cast<std::int8_t>(v), ref});
  }
  void toward_wait(AdviceReasonCode code, std::int32_t ref = 0) {
    vote(code, 0, ref);
    damp += 1;
  }
  [[nodiscard]] Price close() const { return sc.bars[s].close; }
};

Regime read_regime(Reader& r, const Adx& a) {
  const auto& adx = a.adx[r.s];
  const auto& plus = a.plus_di[r.s];
  const auto& minus = a.minus_di[r.s];
  if (!adx || !plus || !minus) return Regime::None;
  if (adx->raw < kAdxRange) {
    r.vote(AdviceReasonCode::RegimeRange, 0);
    return Regime::Range;
  }
  if (adx->raw <= kAdxTrend || *plus == *minus) return Regime::None;
  const bool up = *plus > *minus;
  r.vote(up ? AdviceReasonCode::RegimeTrendUp : AdviceReasonCode::RegimeTrendDown, up ? 1 : -1);
  return up ? Regime::Up : Regime::Down;
}

// In a range only: fade an RSI extreme and a close outside the Bollinger bands.
void range_votes(Reader& r, const Series& rsi14, const Bollinger& bb) {
  if (const auto& v = rsi14[r.s]; v && (v->raw < kRsiLow || v->raw > kRsiHigh)) {
    r.vote(AdviceReasonCode::RsiExtreme, v->raw < kRsiLow ? 1 : -1);
  }
  const Price c = r.close();
  const std::optional<Price>& lower = bb.lower[r.s];
  const std::optional<Price>& upper = bb.upper[r.s];
  if (lower.has_value() && c < lower.value()) r.vote(AdviceReasonCode::BandFade, 1);
  else if (upper.has_value() && c > upper.value()) r.vote(AdviceReasonCode::BandFade, -1);
}

// In a trend only: follow the MACD histogram's sign.
void trend_votes(Reader& r, const Macd& m) {
  if (const auto& h = m.histogram[r.s]; h && h->raw != 0) r.vote(AdviceReasonCode::MacdMomentum, sign(h->raw));
}

// The direction the regime favors: the trend, or back toward the middle band in a range.
// A pattern agrees with its context when it reverses the move that led into it (a hammer after a
// decline) and, in a trend, points the trend's way (a pullback ending). Unknown regime: no context.
bool in_context(const Reader& r, Regime regime, const PatternHit& h) {
  const std::uint32_t first = h.step + 1 - h.bars;
  if (regime == Regime::None || first < 6) return false;
  const int before = sign(r.sc.bars[first - 1].close.raw - r.sc.bars[first - 6].close.raw);
  const int trend = regime == Regime::Up ? 1 : regime == Regime::Down ? -1 : h.bias;
  return h.bias == -before && h.bias == trend;
}

// The latest directional pattern ending in the last kPatternBars bars; double weight in context.
void pattern_vote(Reader& r, Regime regime) {
  std::optional<PatternHit> latest;
  for (const PatternHit& h : detect_patterns(r.sc, r.s)) {
    if (h.bias != 0 && h.step + kPatternBars >= r.s) latest = h;
  }
  if (!latest) return;
  const int weight = in_context(r, regime, *latest) ? 2 : 1;
  r.vote(AdviceReasonCode::Pattern, latest->bias * weight, static_cast<std::int32_t>(latest->step));
}

// Released credible surprises are already priced (no vote, toward wait). A fast wire delivers
// a headline up to news_lead bars early: that one votes in the surprise's direction.
void news_reasons(Reader& r, std::uint8_t news_lead) {
  for (const NewsItem& n : r.sc.news) {
    const int surprise = n.actual - n.expectation;
    if (n.credibility < kCredible || surprise == 0) continue;
    const auto ref = static_cast<std::int32_t>(n.release_step);
    if (n.release_step <= r.s && r.s - n.release_step < kPricedBars) r.toward_wait(AdviceReasonCode::NewsPriced, ref);
    else if (n.release_step > r.s && n.release_step <= r.s + news_lead) r.vote(AdviceReasonCode::NewsEarly, 2 * sign(surprise), ref);
  }
}

// An announced release (G3) within the next kEventAheadBars bars: its outcome is unknown and most of
// a surprise lands in the opening gap, so the read moves one step toward wait. Reads only what the
// state lists as upcoming: the release step, never the actual.
void event_ahead_reason(Reader& r) {
  for (const NewsItem& n : r.sc.news) {
    const bool announced = n.announce_lead > 0 && r.s + n.announce_lead >= n.release_step;
    if (announced && n.release_step > r.s && n.release_step <= r.s + kEventAheadBars) {
      r.toward_wait(AdviceReasonCode::EventAhead, static_cast<std::int32_t>(n.release_step));
      return;
    }
  }
}

// Published funding above the crowding threshold: a warning against joining the crowded side
// (ref +1 longs pay, -1 shorts pay). A warning, not a vote: the crowd is usually right until the
// squeeze (README "What can the player predict?"), so it neither fades nor dampens the read.
void funding_reason(Reader& r) {
  const std::size_t visible = std::min<std::size_t>(r.s + 1, r.sc.funding_rate.size() - 1);
  const Rate rate = r.sc.funding_rate[visible];
  if (rate.raw <= kCrowdedFunding && rate.raw >= -kCrowdedFunding) return;
  r.vote(AdviceReasonCode::FundingCrowded, 0, sign(rate.raw));
}

// ATR at its lowest of the last kSqueezeBars bars: a breakout is likely, its direction is not.
bool squeeze_reason(Reader& r, const Series& atr14, Price now) {
  if (r.s + 1 < kSqueezeBars) return false;
  for (std::uint32_t i = r.s + 1 - kSqueezeBars; i <= r.s; ++i) {
    const std::optional<Price>& v = atr14[i];
    if (!v.has_value() || v.value() < now) return false;
  }
  r.toward_wait(AdviceReasonCode::LowVolSqueeze);
  return true;
}

std::int32_t vote_sum(const Reader& r) {
  std::int32_t sum = 0;
  for (const AdviceReason& x : r.reasons) sum += x.vote;
  return sum;
}

// Stop 2 x ATR from the mark; size risks 1% of equity at that stop, rounded down to the lot.
void stop_and_size(const Scenario& sc, const RunState& st, Price atr, bool squeeze, Advice& a) {
  const bool buy = a.stance == AdviceStance::Long;
  const Price mark = sc.bars[st.step].close;
  const std::int64_t raw = buy ? mark.raw - 2 * atr.raw : mark.raw + 2 * atr.raw;
  const Rounding toward_mark = buy ? Rounding::Floor : Rounding::Ceil;
  a.stop = std::max(detail::unwrap(round_to_step(Price::from_raw(raw), sc.contract.tick_size, toward_mark)), sc.contract.tick_size);
  const Money equity = account_view(sc, st).equity;
  const Price distance = Price::from_raw(buy ? mark.raw - a.stop.raw : a.stop.raw - mark.raw);
  if (equity.raw <= 0 || distance.raw <= 0) return;
  const Money per_unit = detail::unwrap(mul<Money>(sc.contract.multiplier, distance, Rounding::Ceil));
  const Qty raw_qty = detail::unwrap(div<Qty>(Money::from_raw(equity.raw / 100), per_unit, Rounding::Floor));
  const Qty qty = Qty::from_raw(squeeze ? raw_qty.raw / 2 : raw_qty.raw);
  const Qty lots = detail::unwrap(round_to_step(qty, sc.contract.lot_size, Rounding::Floor));
  a.size = lots < sc.contract.min_qty ? Qty{} : lots;
}

AdviceStance stance_of(std::int32_t sum) {
  if (sum >= kStanceVotes) return AdviceStance::Long;
  if (sum <= -kStanceVotes) return AdviceStance::Short;
  return AdviceStance::Wait;
}

}  // namespace

Advice advise(const Scenario& sc, const RunState& st) {
  Reader r{sc, st.step, {}, 0};
  const Series rsi14 = rsi(sc, st.step, 14);
  const Series atr14 = atr(sc, st.step, 14);
  const Bollinger bb = bollinger(sc, st.step, 20, 2);
  const Regime regime = read_regime(r, adx(sc, st.step, 14));
  if (regime == Regime::Range) range_votes(r, rsi14, bb);
  if (regime == Regime::Up || regime == Regime::Down) trend_votes(r, macd(sc, st.step, 12, 26, 9));
  pattern_vote(r, regime);
  news_reasons(r, st.options.news_lead);
  event_ahead_reason(r);
  funding_reason(r);
  const std::optional<Price>& atr_now = atr14[st.step];
  const bool squeeze = atr_now.has_value() && squeeze_reason(r, atr14, atr_now.value());

  std::int32_t sum = vote_sum(r);
  sum = sign(sum) * std::max(0, std::abs(sum) - r.damp);

  Advice a;
  a.step = st.step;
  a.stance = atr_now.has_value() ? stance_of(sum) : AdviceStance::Wait;  // no stop without an ATR
  a.confidence_bp = std::min<std::uint32_t>(kAdviceConfidenceCap, 5000 + 400 * static_cast<std::uint32_t>(std::abs(sum)));
  a.reasons = std::move(r.reasons);
  if (a.stance != AdviceStance::Wait && atr_now.has_value()) stop_and_size(sc, st, atr_now.value(), squeeze, a);
  return a;
}

}  // namespace tradelab
