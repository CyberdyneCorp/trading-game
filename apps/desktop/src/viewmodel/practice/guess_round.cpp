#include "viewmodel/practice/guess_round.hpp"

#include <algorithm>

#include "viewmodel/practice/prng.hpp"

namespace tradelab::desktop::practice {

namespace {

constexpr std::int64_t kScale = 100'000'000;

bool digits_only(std::string_view s) {
  return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

std::string_view trim(std::string_view s) {
  const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
  while (!s.empty() && blank(s.front())) s.remove_prefix(1);
  while (!s.empty() && blank(s.back())) s.remove_suffix(1);
  return s;
}

std::int64_t digits_value(std::string_view s) {
  std::int64_t v = 0;
  for (const char c : s) v = v * 10 + (c - '0');
  return v;
}

struct PlanParts {
  bool side_ok = false;
  bool distance_ok = false;
  bool reward_ok = false;
};

// The invalidation on the losing side (10); its distance between 0.5 and 3 ATR (15); reward to risk of
// at least 1.5 with the target on the winning side (15). Distance and reward need the losing side.
PlanParts plan_parts(const Commitment& c, const RoundFacts& f) {
  const auto close = fixed(f.close);
  const auto atr = fixed(f.atr);
  const auto inv = fixed(c.invalidation);
  const auto target = fixed(c.target);
  if (!close || !atr || !inv) return {};
  const std::int64_t dir = c.decision == "long" ? 1 : -1;
  const std::int64_t risk = dir * (*close - *inv);
  PlanParts out;
  out.side_ok = risk > 0;
  out.distance_ok = out.side_ok && 2 * risk >= *atr && risk <= 3 * *atr;
  const std::int64_t reward = target ? dir * (*target - *close) : 0;
  out.reward_ok = out.side_ok && reward > 0 && 2 * reward >= 3 * risk;
  return out;
}

// Did this bar reach the invalidation, and the target?
std::pair<bool, bool> reached(const Commitment& c, const HighLow& b, std::optional<std::int64_t> inv,
                              std::optional<std::int64_t> target) {
  const auto hi = fixed(b.high);
  const auto lo = fixed(b.low);
  if (!hi || !lo) return {false, false};
  const bool long_side = c.decision == "long";
  const bool stop = inv && (long_side ? *lo <= *inv : *hi >= *inv);
  const bool tp = target && (long_side ? *hi >= *target : *lo <= *target);
  return {stop, tp};
}

}  // namespace

RoundPick pick_round(const std::vector<MissionInfo>& markets, const std::string& date, int index) {
  Mulberry32 next(fnv1a32("guess-v1:" + date + ":" + std::to_string(index)));
  const MissionInfo& m = markets[static_cast<std::size_t>(pick(next, static_cast<int>(markets.size())))];
  const std::uint32_t drawn = next();
  const std::uint32_t seed = drawn == 0 ? 1 : drawn;
  const int lo = m.warmup_bars + kGuessLead;
  const int hi = m.total_bars - 1 - kGuessHorizon;
  return {m.id, seed, lo + pick(next, std::max(1, hi - lo + 1))};
}

std::string typed_decimal(std::string_view v) {
  std::string s(trim(v));
  if (const auto comma = s.find(','); comma != std::string::npos) s[comma] = '.';
  return s;
}

std::optional<std::int64_t> fixed(std::string_view text) {
  std::string_view s = trim(text);
  const bool negative = !s.empty() && s.front() == '-';
  if (negative) s.remove_prefix(1);
  const std::size_t mark = s.find_first_of(".,");
  const std::string_view whole = s.substr(0, mark);
  const std::string_view frac = mark == std::string_view::npos ? std::string_view{} : s.substr(mark + 1);
  if (!digits_only(whole) || (mark != std::string_view::npos && !digits_only(frac))) return std::nullopt;
  std::string eight(frac.substr(0, 8));
  eight.resize(8, '0');
  const std::int64_t v = digits_value(whole) * kScale + digits_value(eight);
  return negative ? -v : v;
}

int calibration_part(int p, bool higher) {
  const int o = higher ? 100 : 0;
  return std::max(0, 5000 - (p - o) * (p - o));
}

bool consistent(std::string_view decision, int p) {
  if (decision == "long") return p >= 55;
  if (decision == "short") return p <= 45;
  return p >= 40 && p <= 60;
}

GuessScore score_round(const Commitment& c, const RoundFacts& f) {
  GuessScore s;
  s.calibration = calibration_part(c.p, f.higher);
  s.consistency = consistent(c.decision, c.p) ? 1000 : 0;
  if (c.decision == "none") {
    s.plan = c.p >= 40 && c.p <= 60 ? 4000 : 0;
  } else {
    const PlanParts parts = plan_parts(c, f);
    s.plan = (parts.side_ok ? 1000 : 0) + (parts.distance_ok ? 1500 : 0) + (parts.reward_ok ? 1500 : 0);
    s.side_ok = parts.side_ok;
    s.distance_ok = parts.distance_ok;
    s.reward_ok = parts.reward_ok;
  }
  s.total = s.calibration + s.plan + s.consistency;
  return s;
}

std::string first_hit(const Commitment& c, const std::vector<HighLow>& bars) {
  if (c.decision == "none") return "neither";
  const auto inv = fixed(c.invalidation);
  const auto target = fixed(c.target);
  for (const HighLow& b : bars) {
    const auto [stop, tp] = reached(c, b, inv, target);
    if (stop && tp) return "both";
    if (stop) return "invalidation";
    if (tp) return "target";
  }
  return "neither";
}

std::vector<CalibrationRow> calibration_table(const std::vector<StatedOutcome>& rounds) {
  std::vector<CalibrationRow> rows;
  for (int from = 0; from < 100; from += 10) {
    CalibrationRow row{from, 0, 0, 0};
    int stated = 0;
    int higher = 0;
    for (const StatedOutcome& r : rounds) {
      if (std::min(90, (r.p / 10) * 10) != from) continue;
      ++row.rounds;
      stated += r.p;
      higher += r.higher ? 1 : 0;
    }
    if (row.rounds == 0) continue;
    row.stated = static_cast<double>(stated) / row.rounds;
    row.observed = 100.0 * higher / row.rounds;
    rows.push_back(row);
  }
  return rows;
}

std::vector<int> probabilities() {
  std::vector<int> out;
  for (int p = kGuessPMin; p <= kGuessPMax; p += kGuessPStep) out.push_back(p);
  return out;
}

}  // namespace tradelab::desktop::practice
