#!/usr/bin/env python3
"""Generator calibration report (spec market-generator "Calibration report"; task 3.4).

Generates every built-in mission for N seeds through the engine (the same bars a pack or a run gets)
and writes a Markdown report: the return distribution, autocorrelation of returns and of absolute
returns (volatility clustering) and the drawdown profile, per mission and pooled, next to the
stylized facts of real markets and, when given, the same measurements on an authorized reference
series. It is deterministic: the same engine and arguments write the same file.

Usage (from bindings/python, after building tradelab_capi):
    PYTHONPATH=. uv run --python 3.12 python ../../tools/calibration_report.py [--seeds 100]
        [--reference authorized.csv] [-o ../../docs/calibration/generator-v5.md]

The reference CSV has a header with `close` (and any other columns): one row per hourly bar, oldest
first. Only data whose license allows this use may be passed (design D5a): the report names it.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import math
import statistics
import struct
import sys
import tarfile
from dataclasses import dataclass
from pathlib import Path

import tradelab_engine as te

HEADER = struct.Struct("<4sIII")
HOURS_PER_YEAR = 8760


def mission_bars(mission: str, seed: int) -> tuple[list[float], int]:
    """Closes of a mission as generated for a seed (from its pack source), and the generator version."""
    result, archive = te.pack_source_mission(mission, seed)
    if not result["ok"]:
        raise SystemExit(f"cannot generate {mission}: {result['reason']}")
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        raw = tar.extractfile("bars.bin").read()
        manifest = json.loads(tar.extractfile("manifest.json").read())
    magic, rows, columns, _ = HEADER.unpack_from(raw)
    assert magic == b"TLB1" and columns == 5
    closes = struct.unpack_from(f"<{rows}q", raw, HEADER.size + 3 * 8 * rows)
    return [c / 1e8 for c in closes], int(manifest["provenance"]["generatorVersion"])


def log_returns(closes: list[float]) -> list[float]:
    return [math.log(b / a) for a, b in zip(closes, closes[1:])]


def autocorr(xs: list[float], lag: int) -> float:
    n = len(xs)
    if n <= lag + 1:
        return 0.0
    mean = statistics.fmean(xs)
    den = sum((x - mean) ** 2 for x in xs)
    if den == 0:
        return 0.0
    return sum((xs[i] - mean) * (xs[i + lag] - mean) for i in range(n - lag)) / den


def max_drawdown(closes: list[float]) -> tuple[float, int]:
    """Deepest peak-to-trough fall (fraction) and the longest stretch under a previous peak (bars)."""
    peak, deepest, longest, since = closes[0], 0.0, 0, 0
    for c in closes:
        if c >= peak:
            peak, since = c, 0
        else:
            since += 1
            longest = max(longest, since)
            deepest = max(deepest, 1 - c / peak)
    return deepest, longest


@dataclass
class Stats:
    bars: int
    vol_annual: float
    skew: float
    kurtosis: float
    tail_3sd: float
    acf_r1: float
    acf_abs1: float
    acf_abs5: float
    acf_abs10: float
    dd_median: float
    dd_p90: float
    underwater_median: float


def moments(rets: list[float]) -> tuple[float, float, float]:
    mean = statistics.fmean(rets)
    sd = statistics.pstdev(rets)
    if sd == 0:
        return 0.0, 0.0, 0.0
    skew = statistics.fmean([((r - mean) / sd) ** 3 for r in rets])
    kurt = statistics.fmean([((r - mean) / sd) ** 4 for r in rets]) - 3
    return sd, skew, kurt


def quantile(xs: list[float], q: float) -> float:
    s = sorted(xs)
    return s[min(len(s) - 1, int(q * len(s)))]


def measure(paths: list[list[float]]) -> Stats:
    """Distribution statistics standardized per path (missions differ in scale), clustering and
    autocorrelation averaged over paths, drawdowns per path."""
    pooled, r1, a1, a5, a10, dds, uws = [], [], [], [], [], [], []
    sds = []
    for closes in paths:
        rets = log_returns(closes)
        mean, sd = statistics.fmean(rets), statistics.pstdev(rets)
        sds.append(sd)
        pooled.extend((r - mean) / sd for r in rets if sd > 0)
        absr = [abs(r) for r in rets]
        r1.append(autocorr(rets, 1))
        a1.append(autocorr(absr, 1))
        a5.append(autocorr(absr, 5))
        a10.append(autocorr(absr, 10))
        dd, uw = max_drawdown(closes)
        dds.append(dd)
        uws.append(uw)
    _, skew, kurt = moments(pooled)
    tail = sum(abs(z) > 3 for z in pooled) / len(pooled)
    return Stats(
        bars=sum(len(p) for p in paths), vol_annual=statistics.fmean(sds) * math.sqrt(HOURS_PER_YEAR), skew=skew, kurtosis=kurt,
        tail_3sd=tail, acf_r1=statistics.fmean(r1), acf_abs1=statistics.fmean(a1), acf_abs5=statistics.fmean(a5),
        acf_abs10=statistics.fmean(a10), dd_median=statistics.median(dds), dd_p90=quantile(dds, 0.9),
        underwater_median=statistics.median(uws),
    )


def reference_paths(path: Path, window: int) -> list[list[float]]:
    """Reference closes cut into windows of the missions' typical length, so drawdowns compare."""
    with path.open(newline="") as f:
        closes = [float(row["close"]) for row in csv.DictReader(f)]
    return [closes[i:i + window] for i in range(0, len(closes) - window + 1, window)] or [closes]


def pct(x: float, digits: int = 1) -> str:
    return f"{100 * x:.{digits}f}%"


def num(x: float, digits: int = 2) -> str:
    return f"{x:+.{digits}f}"


def row(name: str, s: Stats) -> str:
    return (f"| {name} | {s.bars:,} | {pct(s.vol_annual, 0)} | {num(s.skew)} | {num(s.kurtosis, 1)} | {pct(s.tail_3sd, 2)} | "
            f"{num(s.acf_r1)} | {num(s.acf_abs1)} | {num(s.acf_abs5)} | {num(s.acf_abs10)} | {pct(s.dd_median)} | "
            f"{pct(s.dd_p90)} | {s.underwater_median:.0f} |")


TABLE_HEAD = ("| Series | Bars | Vol (ann.) | Skew | Excess kurtosis | \\|z\\| > 3 | ACF r(1) | ACF \\|r\\|(1) | ACF \\|r\\|(5) "
              "| ACF \\|r\\|(10) | Max DD median | Max DD p90 | Longest underwater (median, bars) |\n"
              "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")

STYLIZED = """| Fact (hourly returns of liquid markets) | Target | What the generator shows |
|---|---|---|
| Heavy tails | excess kurtosis well above 0; \\|z\\| > 3 far more often than the normal 0.27% | {kurt} excess kurtosis, {tail} beyond 3 standard deviations (pooled) |
| No linear predictability | ACF of returns close to 0 (\\|ρ(1)\\| < 0.1) | ρ(1) = {r1} (pooled) |
| Volatility clustering | ACF of \\|r\\| positive and decaying slowly with the lag | {a1} at lag 1, {a5} at 5, {a10} at 10 (pooled) |
| Drawdowns | deep, long drawdowns occur without a crash being scheduled | median max drawdown {dd} per mission run, p90 {dd90} |"""


def build_report(seeds: int, reference: Path | None) -> str:
    per_mission, all_paths, version = [], [], 0
    for m in te.missions():
        paths = []
        for seed in range(1, seeds + 1):
            closes, version = mission_bars(m["id"], seed)
            paths.append(closes)
        per_mission.append((m["id"], measure(paths)))
        all_paths.extend(paths)
    pooled = measure(all_paths)
    lines = [
        f"# Generator calibration report — generator v{version}",
        "",
        "**An educational plausibility model, not a predictive one.** TradeLab's markets are generated so that the",
        "player's tools behave the way they do in real markets — mostly a coin flip, with small, unreliable edges in",
        "the right regime — and so that the statistical texture (fat tails, clustered volatility, drawdowns) looks",
        "familiar. Nothing here says anything about any real asset's future.",
        "",
        f"- Engine {te.engine_version()['engine']}, execution policy {te.engine_version()['executionPolicy']},"
        f" generator v{version}; {len(per_mission)} built-in missions × {seeds} seeds (1–{seeds}), hourly bars,"
        " log returns of closes.",
        "- Reproduce: `cd bindings/python && PYTHONPATH=. uv run --python 3.12 python ../../tools/calibration_report.py"
        f" --seeds {seeds}` (deterministic: the same engine writes the same file).",
        "- Distribution columns standardize each run's returns by its own volatility before pooling, because missions",
        "  differ in scale on purpose; autocorrelations are averaged over runs; drawdowns are per run.",
        "",
        "## Stylized facts",
        "",
        STYLIZED.format(kurt=num(pooled.kurtosis, 1), tail=pct(pooled.tail_3sd, 2), r1=num(pooled.acf_r1), a1=num(pooled.acf_abs1),
                        a5=num(pooled.acf_abs5), a10=num(pooled.acf_abs10), dd=pct(pooled.dd_median), dd90=pct(pooled.dd_p90)),
        "",
        "Sources for the targets: R. Cont, *Empirical properties of asset returns: stylized facts and statistical",
        "issues*, Quantitative Finance 1 (2001). They are qualitative targets, not fitted values.",
        "",
        "## By mission",
        "",
        TABLE_HEAD,
        *[row(mid, s) for mid, s in per_mission],
        row("**All missions**", pooled),
        "",
        "## Reference data",
        "",
    ]
    if reference is None:
        lines += [
            "No authorized reference series is compared yet. The historical source (Market4Me, design D5a) holds",
            "venue-derived data whose commercial use and offline redistribution are not cleared (task 1b.5), so this",
            "first report compares against the stylized facts above only. Once a licensed series is available, run the",
            "script with `--reference <file.csv>` to add its row to the table below and commit the new report.",
        ]
    else:
        ref = measure(reference_paths(reference, 200))
        lines += [
            f"Reference: `{reference.name}` (authorized for this use), cut into 200-bar windows.",
            "",
            TABLE_HEAD,
            row("Reference", ref),
            row("All missions", pooled),
        ]
    heavy = sorted(per_mission, key=lambda m: -m[1].kurtosis)
    trending = [mid for mid, s in per_mission if abs(s.acf_r1) >= 0.1]
    lines += [
        "",
        "## Reading the numbers",
        "",
        f"- The heaviest tails are in {', '.join(mid for mid, _ in heavy[:3])} (scripted shocks and drawn incidents); the",
        f"  calmest in {', '.join(mid for mid, _ in heavy[-2:])}. Volatility clustering comes from the GARCH-like variance",
        "  and from event volatility multipliers; it decays within about ten bars, faster than in real markets",
        "  (a known limit of the model).",
        "- Return autocorrelation stays near zero: following the last bar's move is no edge."
        + (f" The exception is {', '.join(trending)}, whose scripted breakout carries momentum by design." if trending else ""),
        "  The learnable structure (ranges, pullbacks, news surprises) is regime-dependent and is measured by the",
        "  realism and learnability tests (`engine/tests/test_mission_realism.cpp`), not by a pooled autocorrelation.",
        "- A generator version without its report must not be used in published packs (`tools/packer build",
        "  --publish` refuses a synthetic source whose generator version has no report in `docs/calibration/`).",
        "",
    ]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--seeds", type=int, default=100)
    p.add_argument("--reference", type=Path, help="authorized hourly CSV with a close column")
    p.add_argument("-o", "--output", type=Path, help="write the report here (default: stdout)")
    args = p.parse_args(argv)
    report = build_report(args.seeds, args.reference)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report)
    else:
        sys.stdout.write(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
