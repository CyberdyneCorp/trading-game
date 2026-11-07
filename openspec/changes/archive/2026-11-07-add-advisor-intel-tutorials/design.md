# Design: add-advisor-intel-tutorials

## Context

Missions start with a briefing (objectives, costs, rules) and teach mostly through the debrief. The engine already computes, per decision step, everything an experienced trader would read: indicators (`indicators.hpp`), detected candle patterns (`candle_patterns.hpp`), released news (filtered by `release_step` in `c_api.cpp`) and the published funding rate. Market studies (`tools/market_study.py`, `test_mission_realism.cpp`) show that no single signal is far from a coin flip in the missions, so an honest advisor cannot be a reliable oracle, and it must not pretend to be one.

## Goals / Non-goals

Goals:
- Teach the market reading the player could not do yet.
- Reward good process.
- Keep every run deterministic and re-simulatable by the backend.

Non-goals: see the proposal.

## Decisions

### D1. The advisor lives in the engine and is a logged command

`Advice advise(const Scenario&, const RunState&)` is a pure function of data visible at `st.step`:
- closed bars up to `step`;
- news with `release_step <= step + news_lead`;
- the published funding rate;
- detected patterns ending at or before `step`.

`CommandType::RequestAdvice = 8` stores the result in `RunState.advice` (step, stance, confidence, stop, reasons) and decrements `advice_left`. It is rejected with `NoAdviceLeft` when the allowance is 0, and it does not advance time.

Why the engine and not the web client:
- Native clients get identical advice.
- The backend can verify advisor use.
- The realism harness can measure the advisor's hit rate across missions, like any other signal.

Alternative rejected: client-side TypeScript advice. It would duplicate the indicator logic, drift from the native clients, and could not be verified.

### D2. How the advisor reasons

Each reason is a signed vote with a fixed weight:

| Reason code | When it votes |
|---|---|
| `RegimeTrendUp` / `RegimeTrendDown` | ADX(14) > 25, in the direction of the stronger DI |
| `RegimeRange` | ADX(14) < 20; turns on the fade reasons |
| `RsiExtreme` | only in a range: fade RSI < 30 or > 70 |
| `BandFade` | only in a range: fade a close outside Bollinger(20, 2) |
| `MacdMomentum` | the MACD histogram's sign, in a trend only |
| `Pattern` | a pattern at or after `step - 2`, its bias, weighted up (2) when it agrees with the regime context: it reverses the 5-bar move that led into it (e.g. a hammer after a decline) and, in a trend, points the trend's way (a pullback ending) |
| `NewsPriced` | a credible surprise released within the last 3 bars: no vote for its direction. Pushes toward **wait** and explains that the gap already priced it |
| `NewsEarly` | with a fast-wire perk only, a credible headline that is not yet public: a vote in the surprise's direction |
| `FundingCrowded` | the published funding is above 0.06% (the generator's default `squeeze_funding`, published in the README): a warning against joining the crowded side. It is shown but neither votes nor pulls toward wait, because the crowd is usually right until the squeeze (fading extreme funding is right 38% of the time in the missions) |
| `LowVolSqueeze` | ATR(14) at a 30-bar low: "a breakout is likely, its direction is not"; pushes toward wait or a smaller size |

- **Weights:** trend, MACD, RSI and band fades 1; a pattern 1 or 2 in context; an early headline 2. `RegimeRange` and `FundingCrowded` vote 0. `NewsPriced` and `LowVolSqueeze` vote 0 and each move the sum one step toward 0.
- **Stance:** long if the vote sum is ≥ +2, short if ≤ −2, otherwise wait.
- **Confidence:** `5000 + 400 × |votes|` basis points, capped at 6500.
- **Stop:** 2 × ATR(14) from the mark.
- **Size:** risk 1% of equity at that stop, rounded down to the lot size (halved in a low-volatility squeeze; 0 below the minimum order). A wait has no stop and no size.

All arithmetic uses the engine's fixed-point and deterministic math. Confidence is an integer in basis points.

The confidence cap of 65% is a teaching statement: the advisor never claims to know. The realism test (D6) checks that its real hit rate stays within its stated range.

### D3. Advisor points

The engine reports `advisor_points_earned` in the score report. It is computed from `build_trades`, so it is deterministic and verifiable:
- +1 for each closed trade with a recorded plan (`planned_risk > 0`) and a protective stop order working at some point while it was open;
- +1 more if that trade's `net > 0`;
- +2 if `status == Finished`;
- the total is capped at 6 per run.

The client keeps the balance in IndexedDB (`meta` store, key `progress`):
- it starts at 3 and is capped at 10;
- at start, the run's allowance is the balance;
- when the run ends, the balance becomes `balance − used + earned`, applied once per `runId`: the settled run ids are stored in the same `progress` record as the balance (`settledRuns`, last 100), so the balance and its idempotency key are written together.

Points are integers. No money quantity is added, so no new monetary representation is needed.

### D4. Intel perks and the look-ahead rule

`RunOptions { std::uint32_t advice_allowance; std::uint8_t news_lead; bool calendar; }` is passed to `start_run`. It is stored in `RunState` and in the client run record, and the backend replays with it.

- **Calendar:** the state view lists upcoming news items whose kind is scheduled (`RateDecision`), with step only.
- **Fast wire:** an item is visible when `release_step <= step + news_lead`. It is marked `early` until `release_step`.

**Look-ahead rule.** The project rule forbids specifying behavior that uses information from after the decision step. Fast wire is a deliberate, flagged exception, limited to the headline:
- The bars, the gap and the market's reaction are generated independently of perks (Player independence), and are not shown early.
- The engine never reveals a bar after `step`.
- The headline is scenario data that a faster feed would deliver earlier.

The debrief and the run record label the run "with intel". Scores are still computed, and they are shown with that label.

Perks unlock by the number of distinct finished missions (#31's `finishedMissions`):

| Distinct finished missions | Perk |
|---|---|
| 1 | Economic calendar |
| 2 | Fast wire I (`news_lead` = 1) |
| 4 | Fast wire II (`news_lead` = 2) |

Each unlocked perk is a toggle on the briefing and is on by default.

As built (phase 3):
- **State view only.** `news_lead` and `calendar` change what `tl_state` serializes (`news[].early`, a top-level `calendar` array of release steps not visible yet), not the execution: no policy bump, every digest unchanged (`test_intel.cpp` compares bars and event digests with and without perks in every mission).
- **Free play only.** Campaign missions and practice packs start without perks, so campaign runs (whose capital carries on) stay comparable; the campaign briefing says so. Unlocks count distinct built-in missions finished in any mode (`finishedMissions`, packs excluded).
- **Toggles restart the unplayed run.** Perks are start options, so turning one on or off on the briefing restarts the run (no action yet) on the same seed with the new options and updates its record.
- **Explained once.** `meta.progress.perksIntroduced` (a new key of version 2) records the perks a free-play briefing has explained; the card shows until the player opens the terminal from it.
- **Server.** The sync body carries the run record's `options`; the API stores them with the run (fixed by the first sync, a different value later is a conflict), replays with them, and lists `withIntel`.

### D5. Tutorials are client content

`missions.ts` gains `tutorial: Card[]` per mission and locale. Each card has a title, 1–3 sentences and an illustration key (`chart`, `ticket`, `news`, `funding`, `options`, `advisor`, `risk`), drawn as small inline SVGs in a `TutorialCard` component.

- The first mission's cards introduce the advisor and points.
- The first mission that unlocks a perk shows a card explaining it.
- **Flow:** `startMission` shows the tutorial screen when the mission id is not in `progress.tutorialsSeen`, then the briefing. "Skip" and finishing both mark it seen. The briefing has a "Tutorial" button to replay it.

### D6. Testing

- **Golden vectors.** m01 and m05 with default options must keep their event digests. Their state digests change only because the state digest starts with the policy version and v11 appends the (default) options and advice fields; the event digests prove a default run executes exactly as under v10. A new golden runs m02 with `{allowance 3, news_lead 1, calendar}` and two `RequestAdvice` commands.
- **Engine property tests:**
  - **No look-ahead.** Advice at step s is identical when every bar after s is altered, and when news released after `s + news_lead` is altered.
  - **Allowance.** The allowance is enforced, and a rejected request changes no state.
  - **Points.** The points formula and its cap hold.
- **Advisor realism** (`test_mission_realism.cpp`). Over 5 missions × 60 seeds, advice is taken every 5 bars and checked against the next open, 5 bars later:
  - non-wait stances are right between 45% and 65% in every mission;
  - "wait" is chosen at least 20% of the time;
  - the mean stated confidence is within 8 points of the measured hit rate.
- **Web:**
  - unit tests for points settlement (idempotent per run) and perk unlocks;
  - e2e tests for: tutorial shown once, skip and replay, the advisor button disabled at 0 points, the perk toggle showing an early headline, and the debrief listing advice and perks.

## Risks / Trade-offs

- **Fast wire makes news trades very profitable** (credible shocks gap 4–8%). This is intended as a reward. It is mitigated by the "with intel" label and by perks being opt-in per run. If it proves too strong, `news_lead` can drop to 1 bar at most without changing the spec structure.
- **A rule-based advisor can be gamed by learning its rules.** That is fine and even desirable: its rules are standard textbook reads, shown as reasons.
- **The policy bump to v11 invalidates no stored run**, since default options replay identically. A run stored at v10 resumes with options defaulted (the client shows its usual "recorded with policy v10" notice). Checkpoints carry the options and advice (checkpoint format 2) and, as before, only restore under the same policy.

## Migration

- IndexedDB `meta.progress` is created on first read with balance 3 and empty sets.
- Old run records without `options` replay with defaults.
- `DB_VERSION` is unchanged (new keys in existing stores).
