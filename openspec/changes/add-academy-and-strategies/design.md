# Design: add-academy-and-strategies

## Context

**What exists.**
- The Academy runner (`academy.ts`, `LessonRunner.svelte`, `LessonChart.svelte`) runs one lesson, Foundations' "Reading a bar". Its data and texts live in TypeScript. The desktop evaluates that module with the TypeScript compiler API to generate `academyLesson.*` strings and the `LESSONS` content (`scripts/screen_sources.mjs`).
- Option tickets are single-leg. `optionsMath.ts` mirrors the engine's formulas for display: payoff, breakeven, fee, write margin and position Greeks.
- The engine prices with Black-76 at a zero rate (`options_pricing.cpp`). Greeks are quantized to Rate (1e-10) for delta and gamma, and to Price (1e-8) for theta per step and vega per volatility point.
- Each short option is margined on its own (`short_margin` in `options_exec.cpp`). At an open, option orders execute in placement order (`option_open_phase`). Margin is checked at acceptance and again at execution (`required_for` against `available_now`).
- Mission design follows design D8 of add-scenarios-and-gameplay:
  - story, lesson and objectives;
  - reveal;
  - tutorial cards;
  - learnability bands with reference players;
  - one new golden per mission, so existing goldens stay byte-identical.

**The gaps.** Thirteen of the fourteen stages have no lesson. There is no glossary. The terminal has tutorial cards but no guided tour. Strategies with several legs can be built only by hand, one leg at a time, without a combined view.

## Goals / Non-goals

Goals:
- Every concept is correct, explained in plain PT-BR and en, shown with the game's own visuals and practiced with a decision and feedback.
- One content source for web and desktop.
- Every option number shown comes from the engine kernel.
- No change to existing digests.

Non-goals: see the proposal.

## Decisions

### D1. Content lives in JSON next to the web code, one source for both clients

`apps/web/src/lib/content/` holds:
- `academy/<lesson-id>.json` (one file per lesson);
- `academy/index.json` (the lesson order and their stages);
- `glossary.json`;
- `coach.json` (the guided tour).

Each file carries structure and text together, with PT-BR and en side by side per item, so a reviewer reads one file per lesson.

- **Web.** It imports the index eagerly and each lesson lazily (`import()` per file), so lessons stay out of the initial bundle (the 1 MB gzip budget). A typed loader (`content/academy.ts`) validates each file against the schema and replaces today's `academy.ts` data. The runner's exports (`lessonById`, `lessonsOf`, `firstTryScore`) keep their names.
- **Desktop.** `scripts/gen_i18n.mjs` reads the same JSON files directly, which is simpler than evaluating TypeScript. It writes:
  - `academyLesson.<id>.*`, `glossary.<id>.*` and `coach.<id>.*` rows into `strings.csv`;
  - the structure (steps, widgets, answers) into `content.json`.

  `check_i18n.mjs` fails on drift, as it does today.
- **Why not a top-level `content/` folder.** The web is already the reference for every text, and the desktop generator already reads web sources. Vite imports JSON natively, while a folder outside `src/` needs alias and `fs.allow` configuration. Keeping it under the web adds no new build path.
- **Why not TypeScript.** Sixteen lessons with widgets would make `academy.ts` thousands of lines long. Evaluating TypeScript in the generator is fragile; `screen_sources.mjs` already needs a transpile step for `LESSONS`. JSON is also easy for a content reviewer to edit.
- **Migration.** The Foundations lesson moves to `academy/foundations-bar.json` first, byte-identical in content. Its existing unit tests and e2e must pass unchanged.

### D2. Lesson format, widgets and the lesson list

Each step has:
- a `phase` (`concept`, `demo`, `decision`, `transfer`);
- an optional `widget`;
- an optional `question`;
- texts per locale: `title`, `body`, `right`, `wrong`, per-choice `feedback`, and choice labels.

A wrong answer explains why (per-choice feedback when the distractor has a typical misconception), and the player chooses again. The end screen shows the answers that were right on the first try (as today).

**Question types.**
- `choice` (today's `bar`, `pct` and `text` choices).
- `number`: a typed answer with a tolerance, accepting either decimal mark, as the ticket does.
- `set`: the player moves a widget until an output meets a target, e.g. "size the trade so the risk is 1% of equity". The step declares the output field, the target and the tolerance.

**Widgets.** Each widget is a view component fed by step data. None of them reads a run.

| Widget | Shows | Numbers from |
|---|---|---|
| `chart` (today's `LessonChart`, extended) | Illustrative candles, the hatched future, and optional horizontal lines: entry, stop, target, liquidation, mark and last, strike | Step data |
| `payoff` | Payoff at expiry of 1–4 legs (an optional perpetual leg), breakevens, strikes, the price range | `tl_option_whatif` (D4) through the worker |
| `whatif` | The lab (D4) with chosen sliders unlocked: price, time, IV | `tl_option_whatif` |
| `ticket` | A sandbox perpetual ticket: equity, price, side, quantity or risk %, leverage, stop. Outputs: notional, margin, liquidation price, risk at the stop, R of a target | `sizing.ts` and `stops.ts` (the web's display mirrors, already tested against engine vectors) |
| `builder` | The strategy builder (D3) on a synthetic chain: no submission, analysis only | `tl_option_whatif` |
| `table` | A small data table (e.g. a trade list to compute expectancy) | Step data |

**Correct numbers by construction.** A lesson file may declare `checks`. Each check is a fact the text states, e.g. `{"kind":"payoff","legs":[…],"at":"110","expect":"6"}` or `{"kind":"liquidation","entry":"100","leverage":5,"side":"long","expect":"…"}`. Two test suites verify them:
- Vitest checks the arithmetic kinds (P&L, return, R, expectancy, drawdown recovery, size from the stop, liquidation) with the web mirrors.
- `bindings/python/tests/test_academy_numbers.py` checks the option kinds (value, Greeks, breakeven, maximum profit and loss) through the Python binding's `option_whatif`.

A unit test also requires that every number written in a question's answer or feedback is produced by a check or by the widget's own output. A lesson cannot state an option price the kernel does not produce.

**Lessons.** There is one per stage; Risk and Options II have two each. Foundations exists; 15 are new, 16 in total. Examples use a fictitious market at prices near 100 and a second market for the transfer exercise (curriculum "Transfer on new market").

| Id | Stage | Concept → demonstration | Decision(s) | Transfer |
|---|---|---|---|---|
| `structure-zones` | Structure | Support and resistance are zones, not lines. Higher highs and higher lows. A break against a retest | Mark the zone that held twice; pick the bar that broke structure | Another market, 4h |
| `orders-execution` | Orders | Market, limit and stop orders: accepted is not filled. Bid/ask spread, slippage, capacity (fills in pieces) | `ticket`: cost of a market round trip vs a limit; which order guarantees a fill, which a price | A thin market: a stop that slipped |
| `futures-pnl` | Futures I | A linear contract: long P&L = (exit − entry) × qty, short the reverse; notional; fees on both sides | `number`: P&L of a short 3 units from 100 to 94 (= +18 before fees) | Another price level, same rule |
| `futures-margin` | Futures II | Initial and maintenance margin; leverage = notional / margin; liquidation price; isolated margin; why a stop must sit before liquidation; mark price vs last price (liquidation and funding use the mark) | `ticket` `set`: the highest leverage that keeps liquidation beyond a stop 4% away; a wick in last that the mark did not follow | 10× on another market |
| `futures-funding` | Futures III | Funding moves cash between longs and shorts every interval; who pays when it is positive; crowding; carry against price | `number`: funding paid on 10,000 notional at +0.05% (= 5 per interval); which side is crowded | A negative rate: shorts pay |
| `risk-sizing` | Risk sizing | Size from the stop: qty = risk budget / stop distance. Leverage does not change the risk at the stop. R multiples | `set`: equity 10,000, 1% risk, entry 100, stop 96 → 25 units; R of a 108 target (= 2R) | A short on another market |
| `risk-expectancy` | Risk sizing | Expectancy = win% × avg win − loss% × avg loss (in R); losing streaks are normal; drawdown recovery (−20% needs +25%, −50% needs +100%); risk of ruin grows quickly with size per trade | `table`: a trade list's expectancy (40% × 2R − 60% × 1R = +0.2R); the chance of 5 losses in a row at 60% losing trades (0.6⁵ ≈ 7.8%) | A 30-trade sample: "too few to conclude" |
| `options-payoff` | Options I | Call and put rights; premium; intrinsic and time value; payoff at expiry; breakeven; the buyer's maximum loss is the premium | `payoff`: call 100 for 4 → breakeven 104, P&L at 110 = +6, at 95 = −4; put 100 for 3 → breakeven 97 | A put on another market |
| `options-greeks` | Options II | Delta (sensitivity to price, ≈ hedge ratio), gamma (how delta changes, largest at the money near expiry), theta (value lost per bar), vega (per IV point) | `whatif`: move the price and read delta; which Greek explains a loss with a flat price | Another strike and expiry |
| `options-iv` | Options II | Implied against realized volatility; IV prices the expected move; time decay speeds up near expiry; IV crush after an event; a buyer needs the move to beat the premium | `whatif`: crush IV by 15 points after a release: which leg loses most; the implied move of an ATM straddle ≈ its price | An event on another market |
| `options-writing` | Options III | Writing: premium received, obligations, margin (per short leg in most missions, defined-risk spread margin where a mission declares it; maintenance margin; buy-back on a margin call); cash settlement at TWAP (European, no assignment before expiry); covered call; protective put; spreads as defined risk | `payoff`: covered call (long 100 + short 105 call for 2 → max profit 7, breakeven 98); protective put (long 100 + 95 put for 2 → max loss 7) | Bull call spread 100/110 for 4: max profit 6, max loss 4, breakeven 104 |
| `strategies-shapes` | Strategies | Templates as payoff shapes: verticals, straddle, strangle, iron condor; defined against undefined risk; legging risk; costs of several legs | `builder`: iron condor 90/95/105/110 for a 3 credit → breakevens 92 and 108, max loss 2; a short strangle 95/105 for 4 → breakevens 91 and 109, loss unbounded on the upside | Choose a structure for a stated view (moderately up, quiet, big move either way) |
| `patterns-lag` | Patterns & Indicators | Indicators summarize the past; regime first (trend against range); a failing engulfing pattern and a high-cost example (curriculum "Failure examples") | Which tool for this regime; spot the failed signal | Another regime |
| `events-surprise` | Events | The consensus is priced; only the surprise moves value; the gap; stops jump through gaps; good news can sell off | Flat, small or hedged into a release; where a stop filled after a gap | Another release |
| `mind-plan` | Evaluation | Thesis, invalidation, size before entry; the journal; FOMO, revenge trading, overconfidence; calibration (a 70% call should be right about 70% of the time); one run is one sample | Spot the revenge trade in a journal; calibrate 10 past forecasts | A new journal |

The Evaluation lesson carries planning and psychology because Evaluation is about judging a process over many attempts. Stages that already have missions get lessons as preparation (D9), not as gates.

**No spoilers.** Lessons never use a mission's market or describe a mission's outcome. Examples are fictitious markets labeled illustrative, as today. A unit test fails if a lesson file names a mission id.

### D3. Strategy builder: client-side legs, pre-validated by an engine dry run

**Choice: sequential ordinary orders from the client, not an engine combo order.**

The builder sends each leg as a normal logged command (`tl_place_option_order`, `tl_place_order` for a perpetual leg) at the same decision step, so replay, checkpoints, the backend's re-simulation and both clients need nothing new. Legs are ordered as follows:
1. long option legs;
2. the perpetual leg, if any;
3. short option legs.

The ordering matters for three reasons:
- A placement is checked against the state at once. If a leg is refused (series settling, writing not allowed, insufficient margin), the client cancels the legs already accepted for this combo with `tl_cancel_order` before the player can advance, so nothing executes. A combo is all or nothing at placement.
- At the next open, option orders execute in placement order (`option_open_phase`), so the protective longs fill before the shorts they protect. With `spread_margin` (D5) the short is then margined as a spread. If the long was not filled, the short's execution check margins it naked, and it is cancelled `insufficient_margin` if that does not fit. The engine never leaves a short the player could not afford.
- A leg can still fail at execution: no bid for a market sell, or margin that no longer fits after the open moved. The client compares the combo's fills after the advance. When some legs filled and others did not, it marks the combo **incomplete** and shows a notice naming the open legs and the risk they now carry (e.g. "the short 105 call is now uncovered: loss unbounded above 107"). It offers "Close the open legs" (market orders, after a confirmation) and "Keep". It never acts on its own, and the risk text comes from the engine (`tl_option_whatif` on the legs actually held).

**Why not an engine combo order.**
- It needs a new command in the IDL, the C ABI, WASM, Python and the desktop adapter.
- It needs a checkpoint format bump.
- It bumps execution policy to v18, which changes every state digest.
- It needs all-or-nothing execution rules across the perpetual and option books.

All that buys protection against one rare case: a leg cancelled at execution after the dry run accepted it. Legging risk is also real and worth teaching; the notice does that.

**Market legs only.** Limit legs could fill on different bars and leave a combo half-built for several bars. Single-leg limit orders stay in the existing ticket.

**One expiry.** Templates and free legs share one expiry. That keeps the expiry payoff piecewise linear, with exact breakevens. Calendars are out of scope; rolling (m17) is two separate trades.

**Templates.**

| Template | Legs (qty n, strikes from the chain nearest the forward F) | Needs writing |
|---|---|---|
| Bull call spread | +C(K1 ≈ F), −C(K2 one to three steps above) | yes |
| Bear put spread | +P(K1 ≈ F), −P(K2 one to three steps below) | yes |
| Covered call | +perp n × multiplier (or "use my long"), −C(K above F) | yes |
| Protective put | +perp n × multiplier (or "use my long"), +P(K at or below F) | no |
| Straddle | +C(K ≈ F), +P(same K) | no |
| Strangle | +C(K above), +P(K below) | no |
| Iron condor | +P(K1), −P(K2), −C(K3), +C(K4), with K1 < K2 < F < K3 < K4 | yes |

A template is a starting point: every leg's strike, side and quantity stays editable. "Short" versions (short straddle, short strangle) are the same templates with the sides flipped by a toggle. When writing is not allowed (the mission's rules or a campaign lock), templates that need it are shown disabled, with the reason and the stage that unlocks writing.

**The pre-trade analysis.** All of it comes from the engine:
- the payoff at expiry and today, the breakevens, the maximum profit and loss (or "unbounded") and the net Greeks, from `tl_option_whatif` (D4) on the legs as they would fill: the ask for buys, the bid for sells, at today's quotes;
- the net premium, fees, margin and the available balance after each leg, from `tl_combo_preview` (D4).

The submit button is disabled while the preview has any refused leg.

**Combos in the log.** Each leg's logged action carries an optional `combo` tag (`"combo":"c<n>"`). The engine adapter ignores it, and a test checks that a log replays to the same digest with and without tags. The journal and the debrief group legs by tag ("Iron condor · 4 legs"). The desktop logs the same field.

### D4. Engine: report-only what-if and combo dry run (no digest change)

There are three new C ABI functions. All are additions, so `kAbiVersion` stays 1. None mutates a run or appears in a log or digest.

**`tl_option_whatif(json)` (no run).** For the Academy and lessons.

Input:
```json
{"forward":"100","minutesPerStep":1440,"multiplier":"1",
 "legs":[{"kind":"call","side":"buy","qty":"1","strike":"100","minutesToExpiry":43200,"iv":"0.60","entry":"4.12"},
         {"kind":"perp","side":"buy","qty":"1","entry":"100"}],
 "shift":{"forward":"105","minutesElapsed":1440,"ivPoints":"-5"},
 "grid":{"lo":"70","hi":"130","points":121}}
```

Output:
- per leg: model value per unit, IV used, delta, gamma, theta per step, vega per volatility point;
- `net`: position value, P&L against the entries, and net Greeks, at the shifted point;
- `expiry`: when every option leg shares one expiry, the breakevens, `maxProfit` and `maxLoss`, and the payoff curve;
- `today`: the curve at the shifted time and IV.

**`tl_run_option_whatif(run, json)`.** For the terminal. The legs name series ids (and the perpetual), and the shifts are a forward, a number of bars forward and IV points. The engine builds the inputs with the run's own IV model, using `make_inputs` at the current step with the shifted forward and time:
- the base IV is the one published at the current step, never a later one: no look-ahead;
- the term factor follows the shifted time;
- skew and smile follow the shifted forward;
- IV points are added last and floored at 5%, as the model does.

With every shift at zero, each leg's value rounded half-even to the premium tick equals that series' quote mid, and its Greeks equal the quote's bit for bit (property test). The terminal lab therefore always agrees with the chain.

**`tl_combo_preview(run, legs_json)`.** The engine copies the `RunState` and applies the legs, in the builder's order, with the same functions the commands use. For each leg it reports:
- accepted or refused, with the engine reason;
- the quote it would cross;
- the premium or margin reserved, and the fee;
- the available balance after the leg.

It also reports totals. The copy is discarded; a test checks that `tl_state` and the state digest are identical before and after. Like the ticket, it is an estimate of the next open: execution uses the next open and the IV published at this close.

**Shared kernel.** `black76`, `make_inputs` and the quantization move behind one internal function. `quote()` and both what-if calls use it, so they cannot drift. `quote()`'s output is unchanged: the OPT01 grid and every golden hold.

**Numeric representation** (new quantities):

| Quantity | Type, scale | Rounding |
|---|---|---|
| Leg value per unit, entry, breakeven | `Price`, 1e-8 | Half-even (the model value is not tick-rounded; breakevens are exact roots, rounded half-even) |
| Leg Greeks per unit | as `OptionQuote`: delta and gamma `Rate` 1e-10; theta per step and vega per vol point `Price` 1e-8 | Half-even (the existing `quantize`) |
| Position value, P&L, `maxProfit`, `maxLoss`, net theta, net vega | `Money`, 1e-8 | Half-even, from the quantized per-unit values × units (`units_value`) |
| Net delta, net gamma | Decimal at 1e-8 (Qty scale) | Half-even, Σ units × quantized per-unit Greek |
| Preview margin and fees | `Money`, 1e-8 | As the engine's margin and fee (Ceil), since they are the engine's own functions |

`maxProfit` and `maxLoss` are exact on the piecewise-linear expiry payoff:
- the value at each strike and at a price of 0 (prices cannot go negative, so a short put's or a long perpetual's maximum loss is finite);
- the slope beyond the highest strike: non-zero and unfavorable is reported as `null`, meaning unbounded.

**Fixtures.** A new shared fixture kind, `"whatif"`, holds an input and its expected output strings. `tests/golden/index.json` lists them. `test_golden.cpp` (native and WASM), the Python golden test and the desktop golden test replay them. Three fixtures: a single call with every shift, an iron condor with breakevens and bounded losses, and a covered call with a perpetual leg.

**As built (D4).**
- `src/options_kernel.hpp`: `price_unit` (Black-76 and the quote's quantization) and `series_inputs`; `quote()` calls them, OPT01 and every golden unchanged.
- A sale's requirement in the dry run is what the command checks; option buys still reserve nothing while they work, so the dry run also reports the margins and the balance once the accepted legs fill at today's quotes (`optionMarginAfterFill`, `positionMarginAfterFill`, `availableAfterFill`), which the builder shows.
- A run leg without `entry` is valued against the price it would cross now (ask, bid, last close). A run-less perpetual leg's units are qty x multiplier.
- `maxLoss` is reported as a positive amount for a loss (`null` = unbounded, above).
- The web logs `strategy` (the structure name) next to `combo` on each leg, and tags the cancels of a withdrawn combo; replay ignores both.
- A perpetual leg may carry `stop` (its working protective stop). The expiry analytics treat it as closed at the stop beyond it (a long worth max(s, stop), a short min(s, stop); one more kink, no gap); value, P&L, Greeks and the "today" curve ignore it, and a stop on an option leg is invalid input. Found in the web playtest: a covered call written on a held 48-unit long with its own stop planned the whole long's loss to a price of 0 ("one entry risked ~4,650 cr (> 2%)"); the clients now pass the long's stop, so the combo's planned risk runs to it (D3). An optional request field, so `kAbiVersion` stays 1 and no fixture changes.

### D5. Engine: defined-risk (vertical spread) margin, declared per mission

`OptionSpec.spread_margin` (bool, default false). When true:
- **Pairing.** A short option position is paired with long positions of the same kind and expiry. The requirement of a paired unit is its worst loss at expiry:
  - a short call against a long call: max(0, K_long − K_short);
  - a short put against a long put: max(0, K_short − K_long).

  The requirement is 0 when the long fully covers the short, as in a debit spread: a bull call spread's short upper call needs no margin. Pairing is greedy and deterministic, not an optimizer. Shorts are taken in series-id order, and each takes the long with the lowest requirement first (series id on ties), up to the long quantity not yet used. For the builder's templates the greedy pairing reaches the minimum, which a test checks.
- **Both sides of one expiry (condors).** Within one expiry, the call pairs that can lose (requirement > 0) lose only above their short strikes, and the put pairs only below theirs. When every such short put strike is ≤ every such short call strike, both sides cannot lose at once. Their combined requirement is then the larger of the two sides' sums, not their total. Otherwise it is the total.
- **Margin of the paired quantities.** Initial = min(per-leg initial of those shorts, the requirement above × units). Maintenance = min(per-leg maintenance, the same requirement × units). Rounding is Ceil, in `Money`. The unpaired remainder keeps the per-leg formula.

  Example: an iron condor 90/95/105/110 is margined at 5 × units, its width. Its maximum loss is width − credit; the credit is already in the wallet.
- **At acceptance.** Working buy orders in the same kind and expiry placed earlier at the same step also count as longs for pairing. At execution only filled longs count. With the D3 ordering the longs have filled by then, and if they have not, the short is margined naked.
- **Margin calls** are unchanged. They buy back the short with the largest maintenance first. A paired short's maintenance is at most its requirement.

Why the worst loss at expiry: a covered short cannot lose more than that requirement from here. Before expiry, equity already marks the long at bid and the short at its mark, so the requirement holds at every step. Requiring more would margin a defined-risk spread like a naked short, which is what m16 is meant to contrast. Real venues have similar spread rules (strategy-based margin). Portfolio margin across the perpetual and options stays a non-goal.

**Why a mission flag and not a policy bump.**
- m01–m12 and packs keep per-leg margin. m10's tutorial already teaches "each short leg is margined on its own", and that stays true there.
- Only the new missions declare the flag, so no existing digest changes. The same pattern as design D8.1.
- The briefing states the mission's margin rule, from `missionRules`, a new field `spreadMargin`.

**Tests (property).**
- A vertical held to expiry never needs more margin than its width.
- The unpaired remainder is margined per leg.
- Pairing is order-independent for the same positions.
- A short whose long failed to fill is margined naked at execution.
- With the flag off, `option_book` is identical to today's on every mission (m05 and m10 goldens).

**As built (D5).** `src/spread_margin.cpp`. A sale's requirement is the holdings' initial margin with the sale minus without it (never below 0), so selling a long that covered a short is charged too. Within the condor rule the side with the smaller sum keeps no paired margin (ties keep the call side), per measure (initial and maintenance). Packs do not carry the flag yet (`instruments.options` has no `spreadMargin`): a mission declaring it must add it to the pack manifest before it can be packed.

### D6. Guided first trade (coach marks)

`coach.json` lists the steps. Each has a target widget name and texts, and some wait for an event:

| # | Target | Text (gist) | Completes when |
|---|---|---|---|
| 1 | chart | What is known (closed bars) and what is not; read the last bar | Next |
| 2 | ticket | Side, size and order type; accepted is not filled | Next |
| 3 | ticket stop field | Set the stop before the entry: where the idea is wrong | Next, or a stop is typed |
| 4 | risk line (ticket estimate) | The money at risk at the stop, as % of equity; keep it ≤ 1% | Next |
| 5 | advance control | Time moves only when you advance; waiting is a decision | The player advances one bar, or Next |
| 6 | debrief score/findings | Shown when this run's debrief opens: process before outcome; where to look first | Next or Done |

- **When.** On the first campaign mission's terminal (m06), once per device (`progress.coachDone` contains `first-trade`). Free play shows no tour unless replayed. Steps 1–5 run in the terminal and step 6 waits for that run's debrief. Leaving the run before the debrief ends the tour there.
- **Skip and replay.**
  - "Skip tour" on every step, and `Esc`, end it and mark it done.
  - "Guided tour" in the help overlay and on the briefing replays it in the current run.
- **Non-modal.** The game stays usable. The callout is a `role="dialog"` without `aria-modal`, labeled by its title. Its target gets a visible outline and `aria-describedby` pointing at the callout text. Opening a step moves focus to the callout, and `Next` returns focus to the target. Steps are announced through a polite live region. Reduced motion turns off the outline animation.
- **Keys.** While the callout has focus, `Enter` and `Space` act on its buttons and never advance the market. Every other shortcut keeps working when the focus is elsewhere.
- **Not the tutorial.** Tutorial cards say *what* the mission teaches. The tour says *where* things are, so it never describes the market and cannot spoil it.

The desktop draws the callout as an overlay panel anchored to the named widget's rectangle (the names are already shared constants). It uses the same texts, keys and stored flag (`prefs.json`).

### D7. Glossary

`glossary.json` holds about 70 terms. Each term has:
- an `id` and the `stage` that teaches it;
- `related` ids;
- per locale: `term`, `aliases` (forms used in the UI and texts), `short` (≤ 2 sentences, shown in place), `long` (a paragraph) and an optional `example` (a worked number, covered by a `check` like lessons, D2).

**Coverage.** Every term the UI marks and every term a lesson or tutorial links must exist in both locales (unit test). The term list covers the perpetual, margin and liquidation, mark and last, funding, order types, spread and slippage, capacity, the risk terms, every option term in D2, the seven strategies, settlement and TWAP, IV and realized volatility, IV crush, consensus, surprise, gap, regime, the indicators shown, and roll.

**In place.** A `Term` component wraps marked labels: ticket fields, rail figures, option chain headers, builder outputs, debrief findings.
- Hover or keyboard focus shows `short` in a tooltip tied by `aria-describedby`.
- A tap shows it on touch.
- "More" opens the glossary at that term.

Lesson and tutorial texts link terms with `[[id|shown text]]`. The runner and the tutorial cards render the link; the desktop generator strips the markup to plain text and keeps the term ids for its tooltip list.

**Glossary screen.** From the menu and the Academy: search in the current locale (accents ignored), alphabetical, the stage tag, related terms.

**Desktop.** The glossary screen, Raven tooltips (`Rwidget_set_tooltip`) on hover, and on keyboard focus the definition in the panel's help line and as the widget's accessible description.

### D8. Strategies stage and missions m13–m17

**Curriculum.** A new stage, `strategies`, comes after Options III and before Patterns & Indicators. It requires `options3` and unlocks nothing new: writing is already unlocked by Options III. Its assessment missions are m13, m14, m15, m16 and m17.

Campaign order: m06, m01, m03, m02, m07, m04, m09, m05, m10, **m13, m14, m15, m16, m17**, m08, m11, m12. The capstone stays last.

Every new mission comes after Options III, so it meets no lock and the existing "no lock the engine cannot enforce" test keeps passing. The stage count becomes 14.

**Common to m13–m17.**
- Synthetic, hourly bars, warm-up 60, starting balance 10,000 (campaign: carried capital).
- Perpetual at 3×.
- Default option spec plus `spread_margin`.
- Chain listed at the first decision.
- They use only existing generator capabilities (segments, random-direction drift, G1–G4, event channels), so `kGeneratorVersion` stays 5.
- One new golden each (default options).
- Lesson questions in `engine/rubrics/lessons.json` with `reviewed: false`.
- Titles are neutral about the outcome.

The segment values below are starting points, tuned at build time until the bands pass (as D8.2 did). If a band cannot hold, it is changed only by a recorded "as built" decision in this design.

| | m13-call-spread | m14-covered-call | m15-protective-put | m16-range-condor | m17-before-expiry |
|---|---|---|---|---|---|
| Title PT / EN | Missão 13 · Trava / Mission 13 · Spread | Missão 14 · Renda / Mission 14 · Income | Missão 15 · Seguro / Mission 15 · Insurance | Missão 16 · Prêmio / Mission 16 · Premium | Missão 17 · Vencimento / Mission 17 · Expiry |
| Campaign slot | 10th | 11th | 12th | 13th | 14th |
| Options | buy and write | buy and write | **buy only** | buy and write | buy and write |
| Regimes | Range with IV bid (iv_mult 1.3), then a drift of drawn direction (up in 60–70%, tilted by the headline) with drawn intensity, then range | Slow drift up and ranges, with a drawn rally (strong in about 35% of seeds) from an unannounced headline | Uptrend and range; an unannounced incident of drawn severity (severe ≈ 30%, mild ≈ 30%, none ≈ 40%), with timing set by the per-seed lead | Long quiet range with IV above realized (iv_mult 1.35), then a breakout of drawn direction whose timing (lead −10..+15) falls before or after the first expiry | G1 chain of Range, Trend and Chop (dwell 20–40) |
| Event | `InstitutionalAdoption` +1, credibility 70 | `InstitutionalAdoption`, actual drawn (0: 65%, +2: 35%) | `ExchangeIncident` or `NetworkOutage`, actual drawn (−2/−1/0) | None in half the seeds, else an unannounced `RegulationRumor` at the break | None |
| Chain (expiries after listing; strikes) | +40, +90; ±4 × 2.5 | +24, +48, +72, +96, +120; ±3 × 2.5 | +40, +100; ±4 × 2.5 | +30, +70; ±5 × 2.5 | +20, +45, +70, +95; ±3 × 2.5 |
| Capability | G2, G4, `spread_margin` | G2, G4, `spread_margin` | G2, G4 | G2, G4, `spread_margin` | G1, G4, `spread_margin` |

**m13 Spread: a bull call spread against a naked call.**
- **Story.** Options are priced rich after a quiet range. A credible adoption headline tilts the drift up in most seeds, but how far the market goes is drawn: modest in most seeds, large in some, down in the rest.
- **Lesson.**
  - A debit spread (long call K1, short call K2 > K1) costs the call's premium minus the short call's premium.
  - Its breakeven is K1 + debit, lower than the naked call's K1 + premium.
  - Its maximum profit is (K2 − K1) − debit, and its maximum loss is the debit.
  - Its delta, vega and theta are smaller. Selling the upper call gives away the upside beyond K2 and finances the trade, which also lowers the exposure to rich IV.
  - In a modest move the spread returns more on premium. In a large move the naked call wins.
- **Objectives.**
  - Before entering, write both structures' breakevens and maximum loss.
  - Debit at risk ≤ 2% of equity.
  - Never leave the short call uncovered: close it before or with the long.
- **Reveal.**
  - This seed's drift direction and intensity.
  - The strategy comparison: the naked call and the spread from the player's first option entry, or the reference entry bar.
- **Tutorial cards** (4): `spread` (new art: a payoff with a cap), `payoff` (two breakevens compared), `volatility` (rich IV and why selling a leg helps), `margin` (here a short leg covered by a long needs margin only for what the spread can still lose; a call spread bought for a debit needs none, a short call alone does).
- **Lesson questions:** numeric, the 100/105 spread's breakeven for a 2.10 debit (102.10); choice, its maximum profit (2.90 per unit).

**m14 Income: a covered call.**
- **Story.** A market that mostly grinds. In some seeds an unannounced headline sets off a strong rally. Short expiries come every 24 bars.
- **Lesson.**
  - A covered call (long underlying + short OTM call) collects premium that lowers the breakeven (entry − premium) and caps the gain at K − entry + premium.
  - It keeps almost all the downside: the premium is a small cushion, not protection.
  - It has the same payoff shape as a short put.
  - Here the short call is margined on its own, and the perpetual pays or earns funding.
  - Rolling the call each expiry is a new decision each time.
- **Objectives.**
  - Write the capped maximum profit before selling the call.
  - Compare the premium collected with the funding paid.
  - Planned risk on the long ≤ 1%, with a stop.
- **Reveal.**
  - Whether this seed drew the rally.
  - The comparison: long only against the covered call.
  - Premium collected against funding paid.
- **Tutorial cards** (4): `covered` (new art), `payoff`, `margin` (the call is margined on its own; covered in P&L, not in margin), `funding`.
- **Lesson questions:** numeric, the maximum profit of long 100 + short 105 call for 2 (7); choice, what protects a covered call against a 20% fall (nothing beyond the premium).

**m15 Insurance: a protective put.**
- **Story.** A rising market. In some seeds an unannounced incident hits, severe or mild, at a time the player cannot know. In many seeds nothing happens. Writing is off, which keeps the focus on the buyer.
- **Lesson.**
  - A protective put sets a floor: the maximum loss is entry − K + premium + costs.
  - The premium is the price of the insurance and is lost in most seeds.
  - The strike is the deductible: an OTM put is cheaper and has a lower floor.
  - The tenor must cover the risk window.
  - A stop costs nothing up front but can fill far beyond its trigger after a gap.
- **Objectives.**
  - Write the floor before buying.
  - Premium ≤ 2% of equity.
  - Choose and record a tenor.
- **Reveal.**
  - This seed's incident: severity and bar.
  - The comparison: the unhedged hold, a 2×ATR stop, an ATM put and a 5% OTM put.
- **Tutorial cards** (4): `insurance` (new art: a floor and the premium), `risk` (stops gap), `chain` (strike as deductible, tenor), `decay` (the cost while nothing happens).
- **Lesson questions:** numeric, the floor of long 100 + 95 put for 2 (a maximum loss of 7 per unit); choice, why the put lost money in a calm seed (the premium, i.e. the time value, decayed).

**m16 Premium: an iron condor against a short strangle.**
- **Story.** A long, quiet range, with options priced above what the range realizes. At some point, before or after the first expiry depending on the seed, the market breaks out in a drawn direction, sometimes on a headline.
- **Lesson.**
  - Selling premium wins most of the time and loses big sometimes.
  - A short strangle's loss is unbounded beyond its breakevens and is margined per leg.
  - An iron condor buys wings: less credit, but a maximum loss of width − credit. Here its margin is the width (the worst loss before the credit), so it can be sized by its worst case.
  - Gamma grows as the market nears a short strike.
  - Four legs pay four spreads.
- **Objectives.**
  - Size by maximum loss (≤ 2% of equity for a defined-risk structure).
  - Write the exit rule for a breakout before entering.
  - Keep a buffer above maintenance margin.
- **Reveal.**
  - The range and the breakout: bar and direction.
  - IV against realized volatility during the range.
  - The comparison: the strangle and the condor at the same quantity.
- **Tutorial cards** (5): `condor` (new art: defined against undefined risk), `volatility` (implied above realized), `margin` (per-leg margin against spread margin), `risk` (size by the worst case), `decay`.
- **Lesson questions:** numeric, the condor 90/95/105/110 maximum loss for a 3 credit (2); choice, which structure's loss is unbounded (the short strangle).

**m17 Expiry: managing before expiry.**
- **Story.** A market that switches between ranges, trends and chop on a drawn path. Expiries come every 25 bars, so a position always has a near and a next expiry.
- **Lesson.**
  - Theta per bar of an ATM option grows as expiry nears (∝ 1/√T), and so does gamma: in the last bars a small move swings the delta from near 0 to near 1.
  - The premium left to earn shrinks relative to the risk of one bar.
  - Closing or rolling early gives up that small premium to remove the swing.
  - A roll is two trades (two spreads) and a new decision. Judge it as if opening fresh.
  - Rolling a loser to wait for it to come back adds time and cost.
- **Objectives.**
  - Decide before the last 5 bars of an expiry whether to close, roll or hold, and record why.
  - Do not roll without a new thesis.
  - Planned risk ≤ 1%.
- **Reveal.**
  - The regime path.
  - Theta and gamma of the player's options over their life.
  - The comparison: hold to settlement against close 5 bars before against roll 5 bars before.
- **Tutorial cards** (4): `roll` (new art: closing near, opening next), `decay` (theta speeds up), `chain` (choosing the next expiry), `plan` (decide before the last bars).
- **Lesson questions:** choice, what happens to the theta of an ATM option in its last bars (it grows in size); choice, what a roll is (closing one position and opening another: two trades).

**Covered writing (scoring v13, report only).** The `wrote_options` finding, and the good-process credit it withholds, apply only to an uncovered short option. After each step with a fill, a short is covered by long options of the same kind and expiry, and a short call also by long perpetual units. A bull call spread, an iron condor or a covered call is defined risk, so its debrief no longer says "you wrote options uncovered" (found by playing m13 through the builder). Closing the long that covered a short leaves it naked and raises the finding.

**Strategy comparison (scoring v13, report only).** Each of m13–m17 declares a comparison: a list of named structures with legs relative to the forward (strike steps, expiry index, quantity rule) and an exit rule. After the run, the debrief values each structure on this seed's path:
- entry at the next open after the player's first option entry, or at the mission's reference bar if they made none;
- the same quantity rule;
- exit at the mission's exit rule (hold to settlement, or close N bars before expiry);
- the engine's own quotes and fees.

The rows go into `debrief.strategyComparison` (name code, entry bar, exit bar, net P&L as `Money`, maximum drawdown as `Money`, and margin calls). The block is report-only: not a score input and in no digest. It is computed after the run, so it uses no future at any decision. Clients show it in the reveal, next to the player's own result, labeled as computed on this seed.

**Learnability bands** (60 seeds; the D8.3 rules hold: buy-and-hold positive in < 75%, every strategy that can lose loses in ≥ 15% except payoff properties).

| Mission | What a disciplined player can learn |
|---|---|
| m13 | The 100/105-style spread (ATM / +2 steps, first expiry, entered after the headline, closed 3 bars before expiry) costs 35–65% of the naked ATM call. In seeds where the underlying rises less than K2 − K1 by the exit, the spread's P&L ≥ the call's in > 70%. Where it rises more than 2 × (K2 − K1), the call's P&L > the spread's in > 80%. Neither wins in more than 75% of all seeds. In seeds where both lose, the spread loses less in ≥ 90%. Buy-and-hold positive in 45–75% |
| m14 | Long + a 5%-OTM call sold every expiry (rolled at settlement) beats long-only in > 65% of seeds without the rally. Long-only beats it in > 80% of rally seeds. Its worst seed is ≥ 85% of long-only's worst seed (a small cushion only). The standard deviation of its returns is below long-only's. Buy-and-hold positive in 45–75% |
| m15 | Long + an equal ATM put (second expiry) never loses more than (entry − strike + premium + costs) × quantity (100%, payoff property). In seeds without an incident the hedged result is below the unhedged one in > 80%. In severe seeds a 2×ATR stop fills > 2% beyond its trigger in ≥ 40%. A 5% OTM put costs 30–70% of the ATM put. The put's own mean P&L over 60 seeds is negative (insurance costs on average). Buy-and-hold positive in 40–70% |
| m16 | An iron condor (short ±2 steps, wings ±4, first expiry, held to settlement) never loses more than (width − credit) × units + costs (100%, payoff property) and is never margin-called at a quantity whose maximum loss is ≤ 20% of equity. A short strangle at the same short strikes and quantity wins in 50–80% of seeds; its worst loss is ≥ 3× the condor's worst loss; it is margin-called in ≥ 25% of seeds where the breakout comes before the first expiry. Their win rates differ by ≤ 15 points. Buy-and-hold positive in 35–65% |
| m17 | On the generated paths, an ATM option's theta per bar 3 bars before expiry is ≥ 2× its theta 20 bars before, and its gamma ≥ 2× (kernel property, every expiry). A short ATM put held to settlement has a mean one-bar P&L swing in its last 5 bars ≥ 2× its swing 20–25 bars before, while the premium still to earn 5 bars before is ≤ 40% of the entry premium. Rolling 5 bars before pays ≥ 1.8× the costs of holding. A long OTM call held to settlement loses its whole time value in > 60% of seeds. Every reference strategy loses in ≥ 15%. Buy-and-hold positive in 35–65% |

The pooled realism bands (indicator 42–58%, news follow < 72%, fat tails, advisor bands) must still hold over the 17 missions. The advisor needs no change: it gives no option-structure advice.

**As built (m13–m17, `engine/src/missions.cpp`, `tests/test_mission_learnability_c.cpp`).** Generator v5, no new capability; every earlier golden is byte-identical and each mission adds its golden. Measured over seeds 1–60:

- **Chains.** As the table, except m14 and m17 list ±6 strikes (not ±3): after a rally or a trend, a call about 5% out of the money (m14's roll) or an at-the-money strike of the next expiry (m17's roll) must still be listed. The last expiry falls on the last bar of every mission (m13 151 bars, m14 181, m15 161, m16 131, m17 156).
- **Margin.** `MissionDef.spread_margin` is declared for m13, m14, m16 and m17 and copied into `OptionSpec::spread_margin` (task E4). The goldens trade sizes that both rules accept, so their digests are the same under either rule.
- **m13.** The reference spread is ATM / **+1 strike** (2.5, not +2 steps): at the first expiry's tenor (about 29 bars), a call two steps up is worth only 15–20% of the ATM call, so the spread would cost 80% of the call, outside the 35–65% band. The lesson's arithmetic (the 100/105 question) is unchanged. The headline's intensity is drawn in 0.4–1.8 with a small transient reaction (drift 0.0015, aligned with p 0.55), so following it stays under the 72% realism band. Measured: the spread costs 57% of the call; rise below the width: spread ≥ call in 27 of 27; rise above twice the width: call > spread in 16 of 16; spread ≥ call in 33 of 60; when both lose the spread loses less in 22 of 22; buy-and-hold positive 62%.
- **m14.** The rally is a drawn +2 adoption headline (35%) with a strong fair-value impact (0.05) and a quiet reaction (p_none 0.5, p_align 0.5), followed by a segment with no anchor, so a rally keeps its level instead of being pulled back to the range. The headline window is volatile (vol × 2) with implied volatility damped (× 0.6), so the next written call is not priced off the storm. Measured: 23 rally seeds, long only better in 19 (83%); 37 other seeds, the covered call better in 32 (86%); worst seed −782 against −845 (93%); standard deviation 507 against 650; buy-and-hold 67%.
- **m15.** `iv_premium` 0.25 (insurance is priced rich). Measured: within the floor 60 of 60; no incident in 29 seeds, hedged worse in 28; the put's mean P&L −49; 5% OTM put 39% of the ATM put; the 2×ATR stop gapped more than 2% in 18 of 18 severe seeds; buy-and-hold 50%.
- **m16.** A margin call buys back the strangle before its loss can grow, so one quantity cannot show both "worst loss ≥ 3× the condor's" and "margin-called in ≥ 25% of early breakouts". The test compares the two structures at 150 units (no margin call cuts the strangle short) and counts the strangle's margin calls at 440 units (per-leg initial margin about 95% of equity). The condor's margin calls are checked at the 20%-of-equity size under spread margin (below). `iv_premium` is 0.35: at the default 0.10 the shorts two strikes out (about 1.5 standard deviations at 30 bars) collected about 0.1 per unit, a condor risking 49 to make 1. With it the credit is 0.36–0.87. The breakout moves mostly by jumps (drift 0.007, jump probability 0.12) so return autocorrelation stays under 0.25, and its direction leans only weakly on the rumor (`tilt` 8): with the default tilt the rumor at the break set the direction, and following a severe rumor was right 83% of the time over 100 seeds. Measured: condor within width − credit + costs 60 of 60; wins: condor 65%, strangle 67%; worst loss strangle −3,273 against condor −705 (4.6×); breakout before the first expiry in 27 seeds, the 440-unit strangle margin-called in 10 (37%); buy-and-hold 48%; following the rumor pooled over its outcomes is right 46%.
- **m17.** The kernel band holds at one implied-volatility level (240 of 240 expiries, the IV of 20 bars before used 3 bars before). On the generated path the IV level moves with the regime, so theta doubles in 233 of 240 and gamma in 213: the test asks ≥ 90% and ≥ 80% there. The swing band is replaced by the swing against the premium still to earn: an ATM put ends in or out of the money, so its one-bar swing barely grows (0.29 to 0.29 per unit), while the premium left 5 bars before is 12% of the entry premium; the swing per unit of premium left grows from 0.15 to over 8. Rolling 5 bars before costs 2.9× holding; the OTM call ends worthless in 43 of 60; buy-and-hold 43%.
- **m16 margin rule (E8).** Under the mission's `spread_margin` the condor is sized per seed at the largest quantity whose maximum loss (width − the credit at the chain's mids) is 20% of the starting equity (432–517 units over seeds 1–60) and is margin-called in none of the 60 seeds. The test requires that m16 declares `spread_margin`.

**As built (strategy comparison, E9; `src/strategy_comparison.cpp`).** Scoring stays v13 (the comparison joins the covered-writing rule as a report-only fact). The structures are declared per mission in the engine (a table keyed by mission id, like the rubrics) as legs relative to the close at entry (moneyness in basis points, then strike steps; an expiry index among the expiries that still leave the exit rule's bars) and an exit rule: hold (options to settlement, the perpetual to the end), close N bars before the first expiry, or roll N bars before each expiry into the next (0 = at settlement). Each is played on a fresh `RunState` of the same scenario, seed and start options with `apply_command` and `advance`: options longs first, then shorts, then the perpetual at the mission's leverage cap. Entry: the decision step of the player's first option fill, at that order's quantity; with no option traded, the reference step (m13 the headline's release step, as its learnability player enters; the others the first decision) and 10 contracts. A leg refused at entry reports the command's reason and no numbers (the structure is not resized). Net P&L is equity at the end minus the run's initial capital; maximum drawdown is the run metric; margin calls are counted from the events; the exit bar is the last closing fill or settlement (the last bar while something is still open). `tl_debrief` writes it as `strategyComparison` (`{basis, entryStep, qty, rows[]}`; null for other missions and pack runs), only once the run has ended. `tests/test_strategy_comparison.cpp` reproduces each row with a scripted run of the same structure (m13, m15, m16, m17), checks the player's entry and quantity, a refused oversized structure and that the player's run is untouched. The m14 covered call is rolled at each settlement and m17's roll repeats before every expiry, as the learnability players do.

### D9. Curriculum, Academy progress and spaced review

- **Lessons never gate.** Lessons on a stage that has assessment missions are preparation. They are shown on the curriculum map with "lessons x/y done" and a start button, but passing such a stage and its unlocks depend on its missions only. A lesson-only stage (Foundations) still passes when its lessons are done. `stagePassed` changes accordingly. The existing tests for Foundations and for unlocks keep passing, and a new test checks that an unfinished lesson never locks a stage or an unlock.
- **Recommended lesson.** The debrief's next-lesson recommendation names a stage today. It now opens that stage's first Academy lesson the player has not completed, or the stage's first lesson.
- **Academy screen.** Lessons by stage, with done, review due, retake and the glossary. Reached from the menu and the curriculum map.
- **Spaced review (local, light).** Completing a lesson schedules a review at +1 day (box 1). A review asks only that lesson's decision and transfer questions, in order.
  - All right on the first try moves the lesson up a box (+3, +7, +21 days); box 4 done is retired.
  - Any wrong answer sends it back to box 1.
  - The menu shows "N lessons to review" when any is due. It is dismissable until the next day and never blocks play.
  - The clock is the device clock; the client only, never the engine.
- **Storage.**
  - Web: `meta.progress` version 3 adds `lessonReviews` (`{id: {box, due}}`, `due` an ISO date), `coachDone` (string ids) and `glossarySeen` (unused for gating, only for a "new" mark). `migrateProgress` fills defaults from version 2 and keeps everything else.
  - Desktop: `prefs.json` gains the same keys, and a damaged file still reads as defaults.

### D10. Desktop

The desktop follows the web, with the same content and engine calls:
- **Content.** The generator reads the JSON (D1).
- **Academy runner.** The widgets are drawn by the view layer. `payoff` and `whatif` use the chart painter's primitives; `ticket` and `builder` reuse the terminal viewmodels' ports (`sizing`, `stops`, the options viewmodel). Their numbers come from the engine adapter (`tl_option_whatif`, `tl_run_option_whatif`, `tl_combo_preview`), which runs on the `EngineHost` worker like every engine call.
- **Strategy builder.** A panel of the option ticket: templates, legs, analysis, submit. It uses the same ordering and partial-failure notice (D3). The logged actions carry `combo`.
- **Coach marks.** An overlay layer anchored to the named widgets (`game_widgets.hpp`). Skip and replay as on the web.
- **Glossary** screen and tooltips (D7).
- **Missions m13–m17** in the menu, campaign, briefing (the `spreadMargin` rule), debrief (the strategy comparison) and playtest.

Windowed reference captures are per platform (macOS and Linux lavapipe, see `apps/desktop/README.md`). Every new screen adds captures on both.

**As built (web builder playtest, D3).** The builder sizes a structure from the engine's maximum loss: a risk budget in % of equity becomes the contracts per leg (`contractsForRisk`, down to the option lot), like the perpetual ticket's risk sizing; it showed 1 contract by default and nothing to size with. On a held long ("use my long") the size follows the long, and the highest working reduce-only stop that closes all of it goes into the what-if as the perpetual leg's `stop` (`heldLong`, `longStop`), so the plan's risk runs to it; the builder says so, or that the long has no stop.

**As built (desktop builder and lab; tasks D2, D4, D5).**
- **Engine calls.** `EngineCommands::analyze(kind, request, reply)` queues a report-only call (`tl_option_whatif`, `tl_run_option_whatif`, `tl_combo_preview`) on the `EngineHost` worker after the commands sent before it; the reply reaches the UI thread like a snapshot and is dropped once the host is gone. An engine that cannot answer replies `{"error":"unsupported"}` and the lab and builder say so, as the web's feature detection does. Every snapshot carries the run's logged actions (shared, one copy per change), from which the builder rebuilds its combos.
- **Ports.** `strategies.ts`, `combos.ts` and `whatifLab.ts` are ported (`viewmodel/game/strategies`, `combos`, `whatif_lab`) and checked by the web parity fixtures `strategies.json`, `combos.json` and `whatifLab.json` (the chart geometry from the engine's own `whatif` golden answers). They caught two differences: JavaScript's `toFixed` rounds the exact binary value (87.65 × 0.7 is 61.35499… and gives `61.35`), and the position legs carry `String(Math.abs(q))` ("1.5", not "1.50000000").
- **Requests and replies.** Each analysis takes a ticket; a reply to an older request (a slider moved meanwhile) is dropped. The builder asks again when its legs or the state they are valued in change (bar, events, available balance).
- **Builder.** A third tab of the option ticket; template buttons wrap in the 340 px column. A combo's plan is one dialog: the engine gets one plan per leg (the risk split evenly, an option leg named by the combo), the journal one entry with the combo id; the legs then go one at a time with their tags, and a refused leg puts the cancels of the accepted ones first in the queue, so no advance can run in between. The incomplete notice sits in the book panel; its close asks for a confirmation in the terminal's dialog.
- **Lab.** One `LabPanel` (sliders, summary, payoff, table) serves the terminal's dialog and the Academy lab screen (`views/lab.xml`, from the menu's ⚗ button); the payoff is a display list for the scene painter. The builder shows a compact line per leg (the full table needs the dialog's width).
- **Finished on the desktop (D3, D9, X1).** The lesson `whatif` widget is the shared `LabPanel` ("lesson-lab-*") over a second `LabViewModel` set from the step's legs and `unlocked` sliders; the `builder` widget is `academy::LessonBuilder` (port of `builderAcademyLegs`/`pricedLegs`: price at the model, then analyse, both `tl_option_whatif`, each pick taking a ticket). The builder's sizing aid and the held long's `stop` port `contractsForRisk`, `longStop` and `heldLong` (parity cases in `strategies.json`). The briefing shows the margin rule where writing is allowed; the debrief shows `strategyComparison` (port of `comparisonLines`, fixture `comparison.json`) as a table whose structure cell keeps the short name, with each structure's rule and any refusal listed under it (Raven table cells do not wrap). The tutorial cards of m13–m17 draw the web's `spread`, `covered`, `insurance`, `condor` and `roll` art as a display list on the 160x100 view box. The X1 comparison (m16 seed 3, iron condor sized at 1%) found two desktop-only defects, both fixed with regression cases: what-if quantities of 10 or more went to the engine in exponent form (`1e+01`), and leg labels rounded the contracts (85,4 read 85).
- **Debrief and journal.** The trades table puts each combo's row (name, state, net premium, summed result, one R) above its legs; a combo's journal entry shows its state and net premium.

### D11. Testing

| Layer | Tests |
|---|---|
| Engine | What-if equals `quote()` at zero shift (value to the tick, Greeks bit-exact) for every series of every mission at 5 steps; the expiry analytics (breakevens and bounds) against brute force on a fine grid; no look-ahead (the run what-if at step s is unchanged when bars and the IV base after s are altered); `tl_combo_preview` leaves state and digest unchanged and matches a real placement leg by leg; the `spread_margin` properties (D5); m13–m17 goldens; `test_mission_learnability_c.cpp` (D8 bands); realism over 17 missions; OPT01 and every existing golden unchanged |
| Bindings | Python `option_whatif`, `Run.option_whatif`, `Run.combo_preview`; `whatif` fixtures in every golden reader; `test_academy_numbers.py` (D2 checks); a log with `combo` tags replays to the same digest |
| Web unit | Content schema and both locales present for every lesson, glossary term and tour step; no mission id in a lesson; the arithmetic checks; review scheduling (boxes, due dates, retire, reset); `stagePassed` with lessons; builder ordering, cancel-on-refusal and incomplete-combo detection with a fake worker; templates' legs from a chain; progress v3 migration |
| Web e2e | A lesson from each stage family (perpetual, options, strategies) to the end with a wrong-then-right answer; the review prompt with a mocked clock; the what-if lab (sliders change engine numbers; zero shift equals the chain); the builder (iron condor previewed, submitted, filled; a refused leg cancels the others; a forced no-bid leg shows the incomplete notice); the tour (shown once on the first campaign mission, skip, replay, keyboard only, axe); the glossary (search, tooltip on focus); m13–m17 played to the debrief |
| Desktop | Generator drift check; unit tests for each new viewmodel (ported web vectors); headless: every new view loads and binds, a lesson, the lab, the builder flow, the tour by keyboard, the glossary; windowed captures on both platforms; playtest extended to 17 missions; goldens include the `whatif` fixtures |

### As built (web Academy, glossary and tour; tasks W1–W4, W8, W9)

- **Content files.** `academy/index.json` carries each lesson's title and summary as well as its order and stage, so the map and the Academy list lessons without loading them. A lesson may declare `charts` and reuse one per step with `{"ref": …, …overrides}`; the web loader and the desktop generator resolve it the same way. `givens` lists numbers a text gives rather than computes (a convention such as RSI 70, the Black-76 model name).
- **Number checks.** `content/academy.test.ts` fails on any number in a lesson text (body, feedback, choices, table texts) that no check, widget, chart, answer or given produces; integers up to 31 count as counts (bars, days, steps).
- **Option lessons before W5.** The `payoff` widget draws the expiry payoff from `content/payoff.ts` (intrinsic value minus premium: exact arithmetic, no model value), and its breakevens and maximum profit and loss are checked by Vitest. No lesson states a model value or a kernel Greek yet: `options-greeks` and `options-iv` give Greeks as labeled hypothetical inputs and ask only arithmetic on them (delta × move, vega × IV points). W5/W6 replace them with the engine's `whatif` widget and add `test_academy_numbers.py`.
- **Option lessons with the engine (W5, W6).** Two more widgets: `whatif` (the lab on fixed legs, `LessonWhatIf`; only the step's `unlocked` sliders move) and `builder` (`LessonBuilder`: the templates on a synthetic chain of one expiry and one IV, each leg priced at the engine's model value rounded to the cent, then the engine's analysis; nothing is sent). A `whatif` check names a step's what-if widget, a slider position and a field of the engine's result (`{"kind":"whatif","step":"move","shift":{"price":2},"field":"legs.0.value","expect":6.83,"digits":2}`); `whatifCheckRequest` builds exactly the widget's request. `content/academyEngine.test.ts` values every one through the WASM build and `bindings/python/tests/test_academy_numbers.py` through the Python binding (with a mirror of `academyRequest` guarded by a shape test); both also check every expiry arithmetic check of the lessons and the glossary against the engine's expiry analytics. `options-greeks` now reads an at-the-money call's value and Greeks off the engine (and the effect of +2% and 5 days), its transfer asks the engine's gain of a put when IV rises 4 points (vega × 4 is within the tolerance); `options-iv` shows theta speeding up on the lab's time slider; `strategies-shapes`' transfer embeds the builder. The desktop generator copies the new widgets into `content.json`, and the desktop draws them (task D3, "Finished on the desktop" above).
- **Strategies stage.** Added with its lesson and no mission; since W10 merged, m13–m17 assess it and its lesson prepares, like every stage with missions.
- **Ticket sandbox.** No fees, spread or slippage, so a lesson's arithmetic reads exactly; the widget says so. A `set` question is checked with a Check button against the sandbox's current value.
- **In-place definitions.** A marked term is a button: hover or focus shows the short definition (also its `aria-describedby`, on a hidden element so it never joins a label's name), the first tap on touch shows it, and a click or Enter is "more" (the glossary dialog at that term). The terminal ignores its shortcuts while the dialog is open.
- **Coach marks.** Opening a step moves focus to the callout's Next button; when the tour leaves the screen (skipped, ended, or waiting for the debrief) focus returns to the last target. A mark waits while a calibration question is open. The tour counts as done once its terminal part is finished or skipped; leaving the terminal before that ends it without marking it, and a run that ends during the terminal steps jumps to the debrief step.
- **Review prompt.** The "not today" dismissal is a per-device convenience in localStorage (`tradelab.reviewDismissed.v1`), not a progress key.
- **Contrast fix.** The ticket's unselected side was drawn at 55% opacity, which failed WCAG AA contrast under axe in the campaign terminal; it is now dashed instead of faded.
- **Desktop.** The generator now reads the lesson JSON (no TypeScript evaluation of `academy.ts`, which is gone) and strips `[[id|text]]` links; the desktop's `stage_passed` follows the D9 rule. Its viewmodels for the new widgets and question types are task D3 (done).

## Risks / Trade-offs

- **Lesson correctness depends on authors.** The `checks` mechanism catches wrong numbers, not wrong explanations. Lesson texts go through the same human review as the rubrics (out of scope here). Lessons say "illustrative" and carry no `reviewed` flag, because they are not scored.
- **Legging risk remains.** A combo can be left incomplete at execution. This is intended, mitigated by the ordering and the dry run, and explained by the notice. If playtests show it is common, an engine combo order can still be added later (with a policy bump).
- **Two margin rules.** m05 and m10 keep per-leg margin while m13–m17 use spread margin. Each briefing states its rule, and m10's tutorial already teaches per-leg margin. Unifying them is a policy bump left for later.
- **Sticky surface in the lab.** The terminal lab uses the run's IV model with today's base. The next bars' quotes will differ when the base moves. The lab says "what-if at today's implied volatility level".
- **Bundle size.** Lazy lesson loading keeps the initial JS within budget. `budget:size` guards it.
- **Campaign length.** 17 missions make a campaign long. Capital carries and the campaign can be ended by choice. If playtests show fatigue, Strategies can move to free play plus one assessment mission without changing the stage model (`missions` is data).

## Migration

- `academy.ts` data → `content/academy/foundations-bar.json`; the runner API names are kept.
- Web `meta.progress` v2 → v3 (defaults for the new keys). Desktop `prefs.json` gains keys with defaults.
- Logged option actions may carry `combo`. Older logs have none and replay unchanged.
- The curriculum map shows the new Strategies stage as open or locked by the usual rules. Players who passed Options III find it open.

## Open Questions

- Should lesson texts get the same `reviewed` sign-off flag as rubrics once a content reviewer exists? This change keeps them unflagged.
- Whether the spaced review should include glossary terms (flashcards) later.
