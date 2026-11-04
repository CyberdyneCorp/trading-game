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
    """Runs a mission to the end without trading; returns the final state and the funding shown per step."""
    run = te.Run(mission, seed)
    state = run.state()
    funding = {}
    while state["status"] == "active":
        funding[state["step"]] = float(state["account"]["nextFundingRate"])
        state = run.advance(1)
    return state, funding


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
    opens = [float(b[0]) for b in state["bars"]]
    closes = [float(b[3]) for b in state["bars"]]
    for item in state["news"]:
        i = item["step"]
        surprise = item["actual"] - item["expectation"]
        key = (mission, i, surprise)
        table[key]["follow"].add(sign(surprise) or item["tone"], math.log(closes[i + NEWS_HORIZON] / opens[i + 1]))
        table[key]["gap"].append(math.log(opens[i + 1] / closes[i]))


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
    news_table = collections.defaultdict(lambda: {"follow": Tally(), "gap": []})
    returns = collections.defaultdict(list)
    worst_bar = collections.defaultdict(collections.Counter)
    for mission in [m["id"] for m in te.missions()]:
        for seed in range(1, seeds + 1):
            state, funding = play(mission, seed)
            technical(signals, state, funding)
            news(news_table, mission, state)
            closes = [float(b[3]) for b in state["bars"]]
            w = state["warmupBars"]
            returns[mission] += [math.log(closes[i] / closes[i - 1]) for i in range(w, len(closes))]
            worst_bar[mission][min(range(w, len(closes)), key=lambda i: closes[i] / closes[i - 1])] += 1

    print(f"{seeds} seeds per mission; signals entered at the next open, {HORIZON}-bar horizon.\n")
    print("| Signal | Samples | Goes the predicted way | Average move in its favor |\n|---|---|---|---|")
    for name, t in sorted(signals.items(), key=lambda kv: -kv[1].hits / kv[1].n):
        print(t.row(name))
    print(f"\n| News (mission, step, surprise) | Gap at the next open | Follow it from the next open, {NEWS_HORIZON} bars | Average move |\n|---|---|---|---|")
    for (mission, step, surprise), v in news_table.items():
        t = v["follow"]
        print(f"| {mission} @{step}, surprise {surprise:+d} | {100 * statistics.fmean(v['gap']):+.2f}% | {100 * t.hits / t.n:.0f}% | {100 * statistics.fmean(t.moves):+.2f}% |")
    print("\n| Mission | Excess kurtosis | Autocorr. of returns | Autocorr. of abs returns | Biggest drop: most common bar (share of seeds) |\n|---|---|---|---|---|")
    for mission, r in returns.items():
        bar, count = worst_bar[mission].most_common(1)[0]
        top3 = sum(c for _, c in worst_bar[mission].most_common(3))
        print(f"| {mission} | {kurtosis(r):.1f} | {autocorr(r):+.2f} | {autocorr([abs(a) for a in r]):+.2f} | {bar} ({100 * count / seeds:.0f}%; top 3 bars {100 * top3 / seeds:.0f}%) |")


if __name__ == "__main__":
    main()
