#!/usr/bin/env python3
"""How much do the player's tools predict in the generated missions? Prints the README tables.

Every signal is read at a bar's close and entered at the next bar's open, which is the earliest
price a player can get. "Hit" means the price moved the signal's way by the horizon's close.

Usage (from bindings/python, after building tradelab_capi):
    PYTHONPATH=. uv run --python 3.12 python ../../tools/market_study.py [seeds-per-mission]
"""
import collections
import math
import statistics
import sys

import tradelab_engine as te

HORIZON = 5        # bars, technical signals
NEWS_HORIZON = 10  # bars, news
EXTREME_FUNDING = 0.0006
ADVICE_EVERY = 5   # bars between advisor requests (as in test_mission_realism.cpp)
WIRE_LEADS = (1, 2)  # Fast wire I and II: headlines this many bars before their release


class Tally:
    def __init__(self):
        self.n = self.hits = 0
        self.moves = []

    def add(self, direction, move):
        if direction == 0 or move == 0:
            return
        self.n += 1
        self.hits += (move > 0) == (direction > 0)
        self.moves.append(direction * move)

    def row(self, name):
        return f"| {name} | {self.n:,} | {100 * self.hits / self.n:.1f}% | {100 * statistics.fmean(self.moves):+.2f}% |"


def sign(x):
    return (x > 0) - (x < 0)


def play(mission, seed):
    """Runs a mission to the end without trading, asking the advisor every ADVICE_EVERY bars.

    Returns the final state (whose advisor.advice lists every read), the funding shown per step and
    the debrief (whose "regimes" table is the signals by revealed regime, scoring v9).
    """
    run = te.Run(mission, seed, te.RunOptions(advice_allowance=10_000))
    state = run.state()
    funding = {}
    cid = 1
    while state["status"] == "active":
        funding[state["step"]] = float(state["account"]["nextFundingRate"])
        if (state["step"] - state["warmupBars"]) % ADVICE_EVERY == 0:
            run.request_advice(cid)
            cid += 1
        state = run.advance(1)
    return state, funding, run.debrief()


def buy_and_hold_positive(mission, seed):
    """1x long from the first decision's market order, held to the end: did it make money?"""
    with te.Run(mission, seed) as run:
        state = run.state()
        run.set_leverage(1, state["contract"]["leverageCap"])
        qty = math.floor(10_000 / float(state["bars"][-1][3]) * 10) / 10
        run.place_order(2, 0, 0, f"{qty:.1f}", None, False)
        state = run.advance(100_000)
    return float(state["account"]["equity"]) > float(state["initialBalance"])


class RegimeTally:
    """Signals by revealed regime (the debrief table), pooled per mission and regime label."""

    SIGNALS = ("rsiFade", "bollingerFade", "macdFollow", "smaFollow")

    def __init__(self):
        self.hits = collections.Counter()
        self.samples = collections.Counter()

    def add(self, row):
        for k in self.SIGNALS:
            self.hits[k] += row[k]["hits"]
            self.samples[k] += row[k]["samples"]

    def row(self, name):
        cells = [f"{100 * self.hits[k] / self.samples[k]:.0f}% ({self.samples[k]})" if self.samples[k] else "–" for k in self.SIGNALS]
        return f"| {name} | " + " | ".join(cells) + " |"


class AdvisorTally:
    """Advisor reads: long/short hits from the next open to HORIZON bars later, waits, stated confidence."""

    def __init__(self):
        self.hits = Tally()
        self.requests = self.waits = 0
        self.confidence = []

    def add(self, state):
        opens = [float(b[0]) for b in state["bars"]]
        closes = [float(b[3]) for b in state["bars"]]
        for a in state["advisor"]["advice"]:
            i = a["step"]
            if i + HORIZON >= len(closes):
                continue
            self.requests += 1
            if a["stance"] == "wait":
                self.waits += 1
                continue
            self.hits.add(1 if a["stance"] == "long" else -1, math.log(closes[i + HORIZON] / opens[i + 1]))
            self.confidence.append(a["confidenceBp"] / 100)

    def row(self, name):
        h = self.hits
        return (f"| {name} | {h.n:,} | {100 * h.hits / h.n:.1f}% | {statistics.fmean(self.confidence):.1f}% "
                f"| {100 * self.waits / self.requests:.0f}% |")


def technical(signals, state, funding):
    opens = [float(b[0]) for b in state["bars"]]
    closes = [float(b[3]) for b in state["bars"]]
    ind = {k: [None if x is None else float(x) for x in v] for k, v in state["indicators"].items() if isinstance(v, list) and k != "patterns"}
    for i in range(state["warmupBars"], len(closes) - HORIZON):
        move = math.log(closes[i + HORIZON] / opens[i + 1])
        rsi, hist = ind["rsi14"][i], ind["macdHist"][i]
        signals["RSI < 30 buy, > 70 sell"].add((rsi < 30) - (rsi > 70), move)
        signals["Bollinger: fade a close outside the bands"].add((closes[i] < ind["bbLower"][i]) - (closes[i] > ind["bbUpper"][i]), move)
        signals["MACD histogram: follow its sign"].add(sign(hist), move)
        signals["MACD cross: follow"].add((hist > 0 >= ind["macdHist"][i - 1]) - (hist < 0 <= ind["macdHist"][i - 1]), move)
        signals["Close vs SMA20: follow"].add(sign(closes[i] - ind["sma20"][i]), move)
        adx_dir = sign(ind["plusDi14"][i] - ind["minusDi14"][i]) if ind["adx14"][i] > 25 else 0
        signals["ADX > 25: follow the stronger DI"].add(adx_dir, move)
        signals["Last bar's color, next bar only"].add(sign(closes[i] - opens[i]), math.log(closes[i + 1] / opens[i + 1]))
        rate = funding.get(i, 0.0)
        signals["Extreme funding (> 0.06%): fade the crowd"].add(-sign(rate) if abs(rate) > EXTREME_FUNDING else 0, move)
    for p in state["indicators"]["patterns"]:
        i = p["step"]
        if state["warmupBars"] <= i < len(closes) - HORIZON:
            signals["Candle pattern: follow its bias"].add(p["bias"], math.log(closes[i + HORIZON] / opens[i + 1]))


def news(table, mission, state):
    """Follows each headline from the first open after it is visible: publicly (release step i), or
    with a fast wire of lead L (visible at i - L, entered at the open of i - L + 1, before the gap).
    The bars are the same with or without the perk, so one run measures all three."""
    opens = [float(b[0]) for b in state["bars"]]
    closes = [float(b[3]) for b in state["bars"]]
    for item in state["news"]:
        i = item["step"]
        surprise = item["actual"] - item["expectation"]
        key = (mission, item["kind"], surprise)
        direction = sign(surprise) or item["tone"]
        table[key]["follow"].add(direction, math.log(closes[i + NEWS_HORIZON] / opens[i + 1]))
        table[key]["gap"].append(math.log(opens[i + 1] / closes[i]))
        for lead in WIRE_LEADS:
            table[key][f"wire{lead}"].add(direction, math.log(closes[i + NEWS_HORIZON] / opens[i - lead + 1]))


def kurtosis(x):
    mu, var = statistics.fmean(x), statistics.pvariance(x)
    return sum((a - mu) ** 4 for a in x) / len(x) / var**2 - 3


def autocorr(x, lag=1):
    mu = statistics.fmean(x)
    den = sum((a - mu) ** 2 for a in x)
    return sum((x[i] - mu) * (x[i - lag] - mu) for i in range(lag, len(x))) / den


def main():
    seeds = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    signals = collections.defaultdict(Tally)
    news_table = collections.defaultdict(lambda: {"follow": Tally(), "gap": [], "wire1": Tally(), "wire2": Tally()})
    returns = collections.defaultdict(list)
    worst_bar = collections.defaultdict(collections.Counter)
    advisor = collections.defaultdict(AdvisorTally)
    by_regime = collections.defaultdict(RegimeTally)
    holds = collections.Counter()
    for mission in [m["id"] for m in te.missions()]:
        for seed in range(1, seeds + 1):
            state, funding, debrief = play(mission, seed)
            for row in debrief.get("regimes", []):
                by_regime[(mission, row["label"])].add(row)
            holds[mission] += buy_and_hold_positive(mission, seed)
            technical(signals, state, funding)
            news(news_table, mission, state)
            advisor[mission].add(state)
            advisor["all missions"].add(state)
            closes = [float(b[3]) for b in state["bars"]]
            w = state["warmupBars"]
            returns[mission] += [math.log(closes[i] / closes[i - 1]) for i in range(w, len(closes))]
            worst_bar[mission][min(range(w, len(closes)), key=lambda i: closes[i] / closes[i - 1])] += 1

    print(f"{seeds} seeds per mission; signals entered at the next open, {HORIZON}-bar horizon.\n")
    print("| Signal | Samples | Goes the predicted way | Average move in its favor |\n|---|---|---|---|")
    for name, t in sorted(signals.items(), key=lambda kv: -kv[1].hits / kv[1].n):
        print(t.row(name))
    print(f"\n| Advisor, every {ADVICE_EVERY} bars | Long/short reads | Right | Mean stated confidence | Wait |\n|---|---|---|---|---|")
    for name, t in sorted(advisor.items(), key=lambda kv: kv[0] == "all missions"):
        print(t.row(name))
    print(f"\n| News (mission, headline, surprise) | Gap at the next open | Follow it from the next open, {NEWS_HORIZON} bars | Average move |\n|---|---|---|---|")
    for (mission, kind, surprise), v in news_table.items():
        t = v["follow"]
        print(f"| {mission} {kind}, surprise {surprise:+d} | {100 * statistics.fmean(v['gap']):+.2f}% | {100 * t.hits / t.n:.0f}% | {100 * statistics.fmean(t.moves):+.2f}% |")
    print(f"\n| News, followed for {NEWS_HORIZON} bars after release | Public (next open) | Fast wire I (1 bar early) | Fast wire II (2 bars early) |\n|---|---|---|---|")
    for (mission, kind, surprise), v in news_table.items():
        cells = [f"{100 * t.hits / t.n:.0f}%, {100 * statistics.fmean(t.moves):+.2f}%" for t in (v["follow"], v["wire1"], v["wire2"])]
        print(f"| {mission} {kind}, surprise {surprise:+d} | " + " | ".join(cells) + " |")
    print("\n| Mission | Excess kurtosis | Autocorr. of returns | Autocorr. of abs returns | Biggest drop: most common bar (share of seeds) |\n|---|---|---|---|---|")
    for mission, r in returns.items():
        bar, count = worst_bar[mission].most_common(1)[0]
        top3 = sum(c for _, c in worst_bar[mission].most_common(3))
        print(f"| {mission} | {kurtosis(r):.1f} | {autocorr(r):+.2f} | {autocorr([abs(a) for a in r]):+.2f} | {bar} ({100 * count / seeds:.0f}%; top 3 bars {100 * top3 / seeds:.0f}%) |")
    print("\n| Mission | Buy-and-hold (1x long from the first open) positive |\n|---|---|")
    for mission, n in holds.items():
        print(f"| {mission} | {100 * n / seeds:.0f}% |")
    print("\n| Mission, revealed regime | RSI fade | Bollinger fade | MACD follow | SMA20 follow |\n|---|---|---|---|---|")
    for (mission, label), t in sorted(by_regime.items()):
        print(t.row(f"{mission}, {label}"))


if __name__ == "__main__":
    main()
