// What the player's tools are worth in the built-in missions (README "What can the player
// predict?"). Every signal is read at a bar's close and entered at the next bar's open, the first
// price a player can get. The bands keep the missions readable but never easy: no indicator far
// from a coin flip, news priced on arrival rather than drifting in, scripted shocks that do not
// land on the same bars every run, and the fat tails and volatility clustering of real markets.
// tools/market_study.py prints the same measurements as tables.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "tradelab/advisor.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/indicators.hpp"

using namespace tradelab;

namespace {

constexpr std::uint64_t kSeeds = 60;
constexpr std::uint32_t kHorizon = 5;
constexpr std::uint32_t kNewsHorizon = 10;

struct Hits {
  int n = 0;
  int wins = 0;
  void add(int direction, double move) {
    if (direction == 0 || move == 0.0) return;
    ++n;
    wins += (move > 0.0) == (direction > 0) ? 1 : 0;
  }
  [[nodiscard]] double rate() const { return n > 0 ? static_cast<double>(wins) / n : 0.0; }
};

double num(const Price& p) { return static_cast<double>(p.raw) / Price::kScale; }
double num(const std::optional<Price>& v) { return v ? num(*v) : 0.0; }
int sign(double x) { return (x > 0.0) - (x < 0.0); }

struct Market {
  Scenario s;
  std::uint32_t last = 0;
  [[nodiscard]] double close(std::uint32_t i) const { return num(s.bars[i].close); }
  [[nodiscard]] double open(std::uint32_t i) const { return num(s.bars[i].open); }
};

std::vector<Market> all_markets() {
  std::vector<Market> out;
  for (const MissionInfo& m : mission_catalog()) {
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
      Market mk;
      REQUIRE(build_mission(m.id, seed, mk.s));
      mk.last = static_cast<std::uint32_t>(mk.s.bars.size() - 1);
      out.push_back(std::move(mk));
    }
  }
  return out;
}

const std::vector<Market>& markets() {
  static const std::vector<Market> cache = all_markets();
  return cache;
}

struct Signals {
  Hits rsi_fade;
  Hits bollinger_fade;
  Hits macd_follow;
  Hits sma_follow;
};

void read_signals(const Market& mk, Signals& out) {
  const Series r = rsi(mk.s, mk.last, 14);
  const Bollinger bb = bollinger(mk.s, mk.last, 20, 2);
  const Macd md = macd(mk.s, mk.last, 12, 26, 9);
  const Series ma = sma(mk.s, mk.last, 20);
  for (std::uint32_t i = mk.s.mission.warmup_bars; i + kHorizon <= mk.last; ++i) {
    const double move = std::log(mk.close(i + kHorizon) / mk.open(i + 1));
    const double rv = num(r[i]);
    const double c = mk.close(i);
    out.rsi_fade.add((rv < 30.0) - (rv > 70.0), move);
    out.bollinger_fade.add((c < num(bb.lower[i])) - (c > num(bb.upper[i])), move);
    out.macd_follow.add(sign(num(md.histogram[i])), move);
    out.sma_follow.add(sign(c - num(ma[i])), move);
  }
}

double kurtosis(const std::vector<double>& x) {
  double mu = 0.0;
  for (const double a : x) mu += a / static_cast<double>(x.size());
  double m2 = 0.0;
  double m4 = 0.0;
  for (const double a : x) {
    m2 += (a - mu) * (a - mu) / static_cast<double>(x.size());
    m4 += std::pow(a - mu, 4) / static_cast<double>(x.size());
  }
  return m4 / (m2 * m2) - 3.0;
}

double autocorr(const std::vector<double>& x) {
  double mu = 0.0;
  for (const double a : x) mu += a / static_cast<double>(x.size());
  double num_sum = 0.0;
  double den = 0.0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    den += (x[i] - mu) * (x[i] - mu);
    if (i > 0) num_sum += (x[i] - mu) * (x[i - 1] - mu);
  }
  return num_sum / den;
}

}  // namespace

TEST_CASE("Mission realism: no indicator alone is far from a coin flip") {
  Signals s;
  for (const Market& mk : markets()) read_signals(mk, s);
  MESSAGE("RSI fade " << s.rsi_fade.rate() << ", Bollinger fade " << s.bollinger_fade.rate() << ", MACD follow "
                      << s.macd_follow.rate() << ", SMA20 follow " << s.sma_follow.rate());
  for (const Hits* h : {&s.rsi_fade, &s.bollinger_fade, &s.macd_follow, &s.sma_follow}) {
    CHECK(h->n > 1000);
    CHECK(h->rate() > 0.42);
    CHECK(h->rate() < 0.58);
  }
}

TEST_CASE("Mission realism: news is priced on arrival; following it afterwards is no free trade") {
  // Before: following m01's denial, m03's incident or m05's outage from the next open won 86-98%.
  std::map<std::string, Hits> follow;
  std::map<std::string, double> gap;
  for (const Market& mk : markets()) {
    for (const NewsItem& n : mk.s.news) {
      const std::uint32_t i = n.release_step;
      const int surprise = n.actual - n.expectation;
      const std::string key = mk.s.id + "/" + std::to_string(static_cast<int>(n.kind));
      follow[key].add(sign(surprise) != 0 ? sign(surprise) : n.tone, std::log(mk.close(i + kNewsHorizon) / mk.open(i + 1)));
      if (n.credibility >= 80 && std::abs(surprise) >= 2) gap[key] += std::log(mk.open(i + 1) / mk.close(i)) / kSeeds;
    }
  }
  for (const auto& [key, h] : follow) {
    MESSAGE(key << ": follow from the next open " << h.rate());
    CHECK(h.rate() < 0.72);
  }
  REQUIRE(gap.size() == 3);  // m01 denial, m03 incident, m05 outage
  for (const auto& [key, g] : gap) {
    MESSAGE(key << ": average gap at the next open " << g);
    CHECK(g < -0.03);  // most of a credible -2 surprise is in the gap
  }
}

TEST_CASE("Mission realism: a scripted shock does not land on the same bars every run") {
  // Before: m03's and m05's biggest drop fell on the same three bars in 96-99% of seeds.
  std::map<std::string, std::map<std::uint32_t, int>> worst;
  for (const Market& mk : markets()) {
    std::uint32_t at = mk.s.mission.warmup_bars;
    for (std::uint32_t i = at; i <= mk.last; ++i) {
      if (mk.close(i) / mk.close(i - 1) < mk.close(at) / mk.close(at - 1)) at = i;
    }
    worst[mk.s.id][at] += 1;
  }
  for (const auto& [id, bars] : worst) {
    std::vector<int> counts;
    for (const auto& [bar, n] : bars) counts.push_back(n);
    std::sort(counts.rbegin(), counts.rend());
    int top3 = 0;
    for (std::size_t k = 0; k < std::min<std::size_t>(3, counts.size()); ++k) top3 += counts[k];
    MESSAGE(id << ": top 3 bars hold " << top3 << " of " << kSeeds << " biggest drops");
    CHECK(top3 < static_cast<int>(kSeeds) / 2);
  }
}

TEST_CASE("Mission realism: fat tails and volatility clustering, little bar-to-bar trend") {
  for (const MissionInfo& m : mission_catalog()) {
    std::vector<double> r;
    std::vector<double> abs_r;
    for (const Market& mk : markets()) {
      if (mk.s.id != m.id) continue;
      for (std::uint32_t i = mk.s.mission.warmup_bars; i <= mk.last; ++i) {
        r.push_back(std::log(mk.close(i) / mk.close(i - 1)));
        abs_r.push_back(std::abs(r.back()));
      }
    }
    MESSAGE(m.id << ": kurtosis " << kurtosis(r) << ", autocorr " << autocorr(r) << ", autocorr |r| " << autocorr(abs_r));
    CHECK(kurtosis(r) > 1.0);      // fatter tails than a normal distribution
    CHECK(autocorr(abs_r) > 0.1);  // calm follows calm, storms follow storms
    CHECK(autocorr(r) < 0.25);     // real markets: about 0
  }
}

// Spec advisor "Honest advisor": advice every 5 bars, outcome from the next open to 5 bars later.
TEST_CASE("Mission realism: the advisor is honest, not an oracle") {
  struct Tally {
    Hits hits;
    int requests = 0;
    int waits = 0;
    double confidence = 0.0;  // sum over long/short advice
    int stances = 0;
  };
  std::map<std::string, Tally> by_mission;
  for (const Market& mk : markets()) {
    Tally& t = by_mission[mk.s.id];
    RunState st = start_run(mk.s, 1);
    for (std::uint32_t i = mk.s.mission.warmup_bars; i + kAdviceHorizon <= mk.last; i += 5) {
      st.step = i;
      const Advice a = advise(mk.s, st);
      t.requests += 1;
      if (a.stance == AdviceStance::Wait) {
        t.waits += 1;
        continue;
      }
      t.hits.add(static_cast<int>(a.stance), std::log(mk.close(i + kAdviceHorizon) / mk.open(i + 1)));
      t.confidence += a.confidence_bp / 100.0;
      t.stances += 1;
    }
  }
  Tally all;
  for (const auto& [id, t] : by_mission) {
    const double hit = 100.0 * t.hits.rate();
    MESSAGE(id << ": advisor right " << hit << "% of " << t.hits.n << ", wait " << 100.0 * t.waits / t.requests
               << "%, mean confidence " << t.confidence / t.stances);
    CHECK(t.hits.n > 50);
    CHECK(hit >= 45.0);
    CHECK(hit <= 65.0);
    all.hits.n += t.hits.n;
    all.hits.wins += t.hits.wins;
    all.requests += t.requests;
    all.waits += t.waits;
    all.confidence += t.confidence;
    all.stances += t.stances;
  }
  const double hit = 100.0 * all.hits.rate();
  const double confidence = all.confidence / all.stances;
  MESSAGE("all missions: right " << hit << "%, wait " << 100.0 * all.waits / all.requests << "%, mean confidence " << confidence);
  CHECK(all.waits * 5 >= all.requests);          // waits at least 20% of the time
  CHECK(std::abs(confidence - hit) <= 8.0);      // says what it knows, no more
}
