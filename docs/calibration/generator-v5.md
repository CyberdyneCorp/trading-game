# Generator calibration report — generator v5

**An educational plausibility model, not a predictive one.** TradeLab's markets are generated so that the
player's tools behave the way they do in real markets — mostly a coin flip, with small, unreliable edges in
the right regime — and so that the statistical texture (fat tails, clustered volatility, drawdowns) looks
familiar. Nothing here says anything about any real asset's future.

- Engine 0.1.0, execution policy 17, generator v5; 17 built-in missions × 100 seeds (1–100), hourly bars, log returns of closes.
- Reproduce: `cd bindings/python && PYTHONPATH=. uv run --python 3.12 python ../../tools/calibration_report.py --seeds 100` (deterministic: the same engine writes the same file).
- Distribution columns standardize each run's returns by its own volatility before pooling, because missions
  differ in scale on purpose; autocorrelations are averaged over runs; drawdowns are per run.

## Stylized facts

| Fact (hourly returns of liquid markets) | Target | What the generator shows |
|---|---|---|
| Heavy tails | excess kurtosis well above 0; \|z\| > 3 far more often than the normal 0.27% | +9.0 excess kurtosis, 1.26% beyond 3 standard deviations (pooled) |
| No linear predictability | ACF of returns close to 0 (\|ρ(1)\| < 0.1) | ρ(1) = +0.04 (pooled) |
| Volatility clustering | ACF of \|r\| positive and decaying slowly with the lag | +0.22 at lag 1, +0.12 at 5, +0.06 at 10 (pooled) |
| Drawdowns | deep, long drawdowns occur without a crash being scheduled | median max drawdown 9.6% per mission run, p90 19.1% |

Sources for the targets: R. Cont, *Empirical properties of asset returns: stylized facts and statistical
issues*, Quantitative Finance 1 (2001). They are qualitative targets, not fitted values.

## By mission

| Series | Bars | Vol (ann.) | Skew | Excess kurtosis | \|z\| > 3 | ACF r(1) | ACF \|r\|(1) | ACF \|r\|(5) | ACF \|r\|(10) | Max DD median | Max DD p90 | Longest underwater (median, bars) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| m01-false-breakout | 14,000 | 80% | -1.14 | +7.1 | 1.23% | +0.04 | +0.17 | +0.12 | +0.07 | 11.1% | 15.9% | 62 |
| m02-funding-squeeze | 16,000 | 87% | -0.64 | +3.5 | 1.55% | +0.06 | +0.22 | +0.12 | +0.07 | 12.0% | 19.4% | 54 |
| m03-volatility-shock | 15,000 | 143% | -1.54 | +21.8 | 2.41% | +0.06 | +0.40 | +0.24 | +0.16 | 15.4% | 28.1% | 62 |
| m04-options-buyer | 14,000 | 71% | +0.94 | +7.3 | 2.04% | +0.14 | +0.32 | +0.22 | +0.11 | 5.4% | 17.1% | 59 |
| m05-options-writer | 15,000 | 118% | -2.79 | +26.4 | 1.58% | +0.01 | +0.33 | +0.09 | +0.07 | 17.2% | 25.6% | 91 |
| m06-thin-book | 17,000 | 59% | +0.16 | +1.4 | 0.93% | +0.03 | +0.19 | +0.10 | +0.06 | 7.3% | 14.7% | 76 |
| m07-trend-trailing | 16,000 | 66% | -0.00 | +2.0 | 1.15% | +0.03 | +0.22 | +0.11 | +0.06 | 8.1% | 18.0% | 65 |
| m08-regime-switch | 18,000 | 71% | +0.04 | +1.0 | 0.82% | +0.01 | +0.17 | +0.11 | +0.05 | 12.2% | 17.7% | 120 |
| m09-event-straddle | 15,000 | 50% | -0.17 | +5.8 | 0.70% | +0.01 | +0.13 | +0.04 | +0.01 | 6.1% | 10.7% | 84 |
| m10-hedged-book | 13,600 | 100% | -3.36 | +29.2 | 1.20% | +0.01 | +0.17 | +0.10 | +0.04 | 12.5% | 17.5% | 49 |
| m11-news-calendar | 20,000 | 80% | -0.41 | +6.0 | 1.30% | +0.01 | +0.17 | +0.06 | +0.03 | 11.3% | 19.8% | 114 |
| m12-capstone | 23,000 | 74% | -0.20 | +6.5 | 1.18% | -0.00 | +0.16 | +0.08 | +0.04 | 11.8% | 22.5% | 116 |
| m13-call-spread | 15,100 | 50% | +0.12 | +1.1 | 0.81% | +0.00 | +0.16 | +0.08 | +0.02 | 5.3% | 8.9% | 66 |
| m14-covered-call | 18,100 | 57% | +1.47 | +14.7 | 0.69% | +0.02 | +0.16 | +0.08 | +0.03 | 6.2% | 9.6% | 87 |
| m15-protective-put | 16,100 | 68% | -1.37 | +12.3 | 1.11% | +0.03 | +0.19 | +0.08 | +0.01 | 7.7% | 14.6% | 60 |
| m16-range-condor | 13,100 | 83% | -0.74 | +12.0 | 2.28% | +0.15 | +0.33 | +0.22 | +0.15 | 16.4% | 26.8% | 70 |
| m17-before-expiry | 15,600 | 71% | +0.01 | +1.0 | 0.87% | +0.01 | +0.20 | +0.11 | +0.05 | 10.1% | 15.2% | 104 |
| **All missions** | 274,600 | 78% | -0.51 | +9.0 | 1.26% | +0.04 | +0.22 | +0.12 | +0.06 | 9.6% | 19.1% | 72 |

## Reference data

No authorized reference series is compared yet. The historical source (Market4Me, design D5a) holds
venue-derived data whose commercial use and offline redistribution are not cleared (task 1b.5), so this
first report compares against the stylized facts above only. Once a licensed series is available, run the
script with `--reference <file.csv>` to add its row to the table below and commit the new report.

## Reading the numbers

- The heaviest tails are in m10-hedged-book, m05-options-writer, m03-volatility-shock (scripted shocks and drawn incidents); the
  calmest in m17-before-expiry, m08-regime-switch. Volatility clustering comes from the GARCH-like variance
  and from event volatility multipliers; it decays within about ten bars, faster than in real markets
  (a known limit of the model).
- Return autocorrelation stays near zero: following the last bar's move is no edge. The exception is m04-options-buyer, m16-range-condor, whose scripted breakout carries momentum by design.
  The learnable structure (ranges, pullbacks, news surprises) is regime-dependent and is measured by the
  realism and learnability tests (`engine/tests/test_mission_realism.cpp`), not by a pooled autocorrelation.
- A generator version without its report must not be used in published packs (`tools/packer build
  --publish` refuses a synthetic source whose generator version has no report in `docs/calibration/`).
