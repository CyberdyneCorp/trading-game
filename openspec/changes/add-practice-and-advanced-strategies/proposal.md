# Proposal: add-practice-and-advanced-strategies

## Why

The game teaches the basics of perpetuals, options and five defined-risk structures, but stops where many real decisions start.
- **The terminal is too simple for the strategies players now meet.** Builder legs are market orders on one expiry, so a calendar or diagonal spread cannot be built. Rolling or adjusting a live structure means rebuilding it leg by leg, and the combo link is lost. Every order works until it fills or is cancelled: there is no time in force. Only bracket legs can cancel each other. Nothing shows the position's total delta, gamma, vega and theta across the perpetual and the options. The trailing stop exists, but it is hidden in the protective section of an open position and taught nowhere.
- **Several core lessons have no mission.** Delta hedging and gamma scalping, funding carry, volatility around an announced event, calendar spreads, managing a position after entry, and the classic behavioral traps (revenge trading, FOMO, averaging down).
- **Practice depends on full missions.** A player who has ten minutes cannot drill a skill. Nobody can compare a run with a fixed market of the day. Nothing trains a decision without P&L noise. The debrief lists findings, but nothing walks the player through the run or lets them retry the moment that went wrong.

## What Changes

The work is split into three waves. Each wave ships on web **and** desktop before the next one starts.

**Wave 1: builder and terminal.**
- **Builder legs.**
  - Limit-priced legs, with a time in force for the combo.
  - Legs on different expiries, for calendar and diagonal templates.
  - A one-click **roll** of an option position or combo to a later expiry.
  - **Adjusting** a live combo: close one leg or add a hedge leg. The combo id is kept.

  All of these stay client sequences of ordinary orders, pre-checked by the engine's dry run (design D3).
- **P&L heatmap.** A grid over price and time, from a report-only extension of the engine's what-if.
- **Time in force** on every order: good-till-cancelled, good for N bars ("day"), and immediate-or-cancel and fill-or-kill for limit orders. Defined for a bar engine: "immediate" means the next open.
- **Player-built OCO groups** linking any two working orders.

  Time in force and OCO groups are engine rules, so they come with **execution policy v18** and checkpoint format 6.
- **Portfolio view.** Total delta, gamma, vega and theta across the perpetual and the options, the next funding payment, and per-mission risk limits with warnings. All numbers come from the engine's what-if on the run's holdings.
- **The existing trailing stop, made easy to find and taught.**
  - A visible "Trailing" choice in the ticket, including a planned trailing stop offered once an entry fills.
  - A chart label with the rule and the best close.
  - A lesson step, a tutorial card and a glossary example.

  There are no new trailing order types.

**Wave 2: eight missions (m18–m25).** Each mission comes with:
- an Academy lesson or lesson step;
- tutorial cards with art;
- rubric questions (`reviewed: false`);
- learnability bands with reference players;
- a seeded reveal and a golden;
- campaign or elective placement.

The missions:
- **m18 gamma scalping:** a long straddle delta-hedged with the perpetual; realized against implied volatility pays for rehedging.
- **m19 funding carry:** earn funding on a hedged short perpetual (the hedge is a synthetic long from options), and see when carry turns against you: the funding flips, costs exceed the carry, or the isolated perpetual leg is liquidated although the book is hedged.
- **m20 event volatility:** buy implied volatility before an announced release and watch the crush after it.
- **m21 calendar and diagonal spreads:** needs the wave 1 multi-expiry legs and a per-mission calendar margin rule.
- **m22 position management:** scale in and out, move the stop to breakeven, take partial profit, trail the rest.
- **m23–m25 psychology:** markets built to tempt revenge trading, FOMO entries and averaging down. The debrief names the bias with evidence from the action log.

New curriculum stages:
- **Trade management** (m22), after Risk.
- **Advanced options and carry** (m18–m21), an elective stage after Strategies, off the campaign path.
- **Psychology** (m23–m25), after Events and before the capstone.

**Wave 3: practice formats.**
- **Daily drills:** two-minute quizzes on payoffs, Greeks, sizing, liquidation prices and funding. Each answer is computed with the lesson check functions, which tests compare with the engine. Items are chosen from the spaced review.
- **Daily challenge:** the same mission and seed for everyone, picked by the local date, with a personal-best history and no leaderboard.
- **"Guess the next move" rounds** on dedicated round markets: read a chart, commit a plan and a probability, then see the reveal. The score is decision quality (calibration and plan coherence), never P&L.
- **Coach mode:** the advisor's voice reviews a finished run step by step. The moments come from a report-only engine call that uses only what was visible at each step.
- **Mistake replay:** from a flagged moment, retry from that bar as an assisted attempt that never counts for progress. The debrief then says whether the mistake was avoided or repeated.

## Capabilities

### New Capabilities
- `portfolio-risk`: the portfolio Greeks view, the next funding payment, per-mission risk limits and their warnings, and the hedge-to-neutral helper.
- `daily-drills`: the drill content format, item generation, timing, scoring, and how items are chosen from the spaced review.
- `daily-challenge`: picking the mission and seed from the local date, the run rules, and the personal-best history.
- `guess-rounds`: round markets, the round flow, decision-quality scoring and the reveal.
- `coach-review`: the engine's step-by-step review moments and the coach mode screen.
- `mistake-replay`: retrying from a flagged moment as an assisted attempt, and the avoided or repeated comparison.

### Modified Capabilities
Earlier changes that are not archived get ADDED requirements in delta files named after the capability, as add-academy-and-strategies did. Archived capabilities (`order-execution`, `options`, `mission-tutorial`) get ADDED requirements too.
- `order-execution` (archived): time in force, player-built OCO groups, policy v18 and checkpoint format 6.
- `options` (archived): calendar margin as a mission flag, multi-expiry what-if analytics, the heatmap, valuation of the holdings, and limit legs in the dry run.
- `options-whatif` (add-academy-and-strategies): the heatmap and multi-expiry display in the lab.
- `strategy-builder` (add-academy-and-strategies): limit legs, multi-expiry templates, roll and adjust. Replaces the "one expiry" and "market legs only" limits.
- `terminal-ui` (add-web-client): the time-in-force and OCO controls, the trailing stop's place in the ticket and on the chart.
- `web-client` (add-web-client): progress version 4 and the practice formats in the menu.
- `market-generator` (add-scenarios-and-gameplay): learnability bands for m18–m25, round markets.
- `mission-scoring` (add-scenarios-and-gameplay): scoring v14 report-only facts (bias findings, position episodes, the risk-limit finding).
- `gameplay-loop` (add-scenarios-and-gameplay): the strategy comparison rules of m18–m22, and the debrief's bias and episode views.
- `curriculum` (add-scenarios-and-gameplay): the three stages, elective stages and the campaign order snapshot.
- `mission-tutorial` (archived): tutorials of m18–m25 with art, and the trailing card.
- `academy` (add-academy-and-strategies): new lessons, the trailing step and new check kinds.
- `glossary` (add-academy-and-strategies): new terms.
- `game-api` (add-cyberdyne-identity-and-backend): replay of policy v18 logs and of v17 logs through the policy cache.
- `desktop-client` (add-native-clients): the desktop ships every item of the three waves.

## Affected

- **Clients.** Web (`apps/web`) and desktop (`apps/desktop`, Linux and macOS), every item. iPad gets the engine behavior through the bindings when it ships.
- **Engine.**
  - Execution policy **17 → 18** (wave 1): time in force and player-built OCO groups (design D6, D7).
    - Every frozen state digest changes, but only through the policy number and the new order fields appended at their defaults.
    - Every event digest stays the same, and a property test checks it for every golden.
    - v17 runs keep replaying on the client's pinned engine and, in the API, through the policy cache.
  - Checkpoint format **5 → 6**: the new order fields.
  - Generator stays at **5**: every new market uses existing capabilities. Design D11 names the only allowed exception and its rule.
  - Scoring **13 → 14** (wave 2), all report-only:
    - bias findings, position episodes and the risk-limit finding;
    - the strategy comparison rules of m18–m22;
    - the coach review moments.

    None of these is a score input or a digest input.
  - C ABI stays at **1**: new functions (`tl_place_order_tif`, `tl_place_option_order_tif`, `tl_link_oco`, `tl_coach_review`) and optional request and reply fields only.
  - Pack schema stays at **2**: packs cannot declare `calendarMargin` or risk limits yet, as with `spreadMargin`.
  - New goldens:
    - one per new mission;
    - one per round market;
    - a policy v18 run (time in force and OCO);
    - `whatif` fixtures for a calendar and a heatmap;
    - a new `coach` fixture kind.
- **Bindings.** WASM worker requests, Python (`Run.place_order(..., tif=)`, `Run.link_oco`, `Run.coach_review`) and the desktop adapter. Run files and the shared action applier accept the new action fields.
- **Services.** `services/api` replays v18 logs with the current engine and v17 logs through the policy cache. No new endpoint.
- **Storage.**
  - Web `meta.progress` **version 4**, migrated: the campaign run's order snapshot, `drillReviews`, `drillHistory`, `dailyChallenges` and `guessHistory`.
  - Run records gain a `daily` link and an assisted kind `mistake`.
  - The desktop's SQLite progress row follows the same JSON.
  - IndexedDB `DB_VERSION` and the SQLite schema are unchanged.

## Non-goals

- Login, sync, any leaderboard or shared ranking, online comparable challenges, and real-market data (Market4Me).
- New trailing order types (stop-limit trailing, a trailing take-profit, or a trailing stop attached to an entry in the engine).
- An engine combo order, net-price combo limits, or all-or-nothing execution across legs (legging risk stays, design D3).
- A spot instrument or dated futures. Carry is taught with the perpetual's funding and a synthetic forward from options.
- Cross margin or portfolio margin. The perpetual stays isolated, which m19 teaches on purpose.
- American options, early exercise and interest rates.
- These product-owner decisions stay as they are:
  - the protective put counted as protection in m15 scoring;
  - the covered-call margin rule on m13–m17;
  - bar numbering;
  - the default lesson recommendation;
  - glossary ranking;
  - the plan-notes default in exports.
- The human review of rubric questions: every new rubric ships `reviewed: false`.
- Changing m01–m17, their markets, goldens' event digests or bands. One exception: the m07 tutorial gains a glossary link for the trailing stop, which is client text only.
- Streaks, timers that punish, or rewards for frequency. Drills and the daily challenge never pressure the player to come back.
