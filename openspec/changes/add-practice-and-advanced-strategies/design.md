# Design: add-practice-and-advanced-strategies

## Context

**What exists** (add-academy-and-strategies and earlier changes):

- **Builder.**
  - The strategy builder sends market legs on one expiry as ordinary orders at one decision step: option buys, then the perpetual, then option sells.
  - It is pre-checked by `tl_combo_preview` and valued by `tl_option_whatif` / `tl_run_option_whatif`.
  - Legs carry a `combo` tag that replay ignores.
  - A combo left incomplete at execution is named, never closed automatically.
- **Option orders.** They already take `market` or `limit` (`PlaceOptionOrderCommand.type`). A working option limit is tried at every open phase until it fills, is cancelled, its series enters the settlement window (`series_settling`) or the run ends.
- **OCO.** OCO exists only between bracket legs: `Order.oco_group` = the parent's id, and a fill reduces the siblings by the filled quantity.
- **Trailing stop.** It exists as an order type with a fixed distance or a rate of the best close (policy v16). The web places it only from the protective section of an open position (`TrailField`). The chart labels it `TRAIL`.
- **What-if.** Expiry analytics (breakevens, bounds) exist only when every option leg shares one expiry; otherwise `expiry` is `null`.
- **Spread margin.** `OptionSpec.spread_margin` pairs a short with longs of the same kind and **expiry**.
- **Missions and the curriculum.**
  - Missions m01–m17. The campaign is derived from the stage order (`campaignMissions()` flattens `STAGES`), and a campaign run stores only an index into it.
  - `assessed` holds the missions finished in a campaign run.
- **Debrief data.** Findings carry a `step`; decision records (`act`/`hold`/`wait`) exist per step.
- **Assisted practice.** It rewinds by replaying a prefix of the log into a new run flagged `assisted` (`{"from","kind":"rewind","step"}`), which never counts.
- **Spaced review.** It schedules lessons in boxes 1/3/7/21 days (`lessonReviews`).
- **Versions.** Execution policy 17, scoring 13, generator 5, checkpoint 5, ABI 1, pack schema 2, web progress 3.

## Goals / Non-goals

Goals:
- Every item ships on web and desktop with the same numbers for the same seed and actions.
- The engine stays the only source of fills, values, Greeks, margins, findings and moments.
- Keep version bumps to the minimum each feature truly needs: report-only calls and per-mission flags first, one execution-policy bump for the two rules that cannot be built any other way.
- Every number in lessons, tutorials, drills and rubrics is checked by a test. Nothing reveals a market before its debrief.

Non-goals: see the proposal.

## Decisions

### D1. Three waves, one change

The change is ordered into waves with hard dependencies:
- **Wave 1** (builder and terminal) comes first, because m21 needs multi-expiry legs, m18 and m19 need the portfolio view and the hedge helper, and m22 needs time in force and OCO.
- **Wave 2** (missions) needs wave 1.
- **Wave 3** (practice formats) needs wave 2's scoring v14 for coach moments and mistake replay. Drills, the daily challenge and guess rounds need only wave 1.

Each wave closes with a cross-check (X) on both clients before the next one is ticked.

Rejected alternative: three separate changes. They would split one data-model migration (progress v4) and one scoring version across changes and make the dependency graph implicit.

### D2. Builder limit legs: per-leg limit premiums and one time in force per combo, no net-price combo order

- **Market or limit.** Each leg may be `market` (as today) or `limit` with a premium on the premium tick.
  - The default limit price is the leg's quote mid rounded to the tick: down for buys, up for sells.
  - The analysis values the legs at their limit premiums.
  - The dry run (`tl_combo_preview`) takes `type`, `premium` and `tif` per leg (optional request fields) and applies them with the same command functions.
- **Time in force.** The combo carries one time in force for all its legs (D6):
  - `ioc`, the default for limit combos: each leg fills at the next open or not at all, which bounds legging to one bar;
  - `bars:N`;
  - `gtc`.

  Market legs ignore it.
- **Combo states.** The client derives a combo's state from its legs' orders and fills:
  - `working`: some legs are still working;
  - `complete`: every leg filled;
  - `incomplete`: some legs filled, the others ended unfilled through cancel, expiry or refusal;
  - `void`: nothing filled.

  In the `working` state the notice offers "Cancel working legs". When the combo becomes incomplete, the existing notice offers "Close the filled legs" (confirmed) and "Keep".
- **Ordering.** Placement order stays D3 of add-academy-and-strategies: option buys, the perpetual, option sells. At an open, option orders execute in placement order, so protective buys fill first when their limits are met.

Rejected alternatives:
- A **net-price combo limit** in the engine. It needs an all-or-nothing execution rule across legs and books, a new command and checkpoint fields. It also hides legging risk, which the game teaches.
- Per-leg **independent** time in force. It allows a combo whose protective leg expires while its short keeps working, which the single combo time in force rules out.

### D3. Multi-expiry legs, roll and adjust: client sequences, no engine command

**Multi-expiry.**
- Free legs may use up to two expiries.
- New templates:
  - **calendar**: −near, +far, same strike, call or put;
  - **diagonal**: −near at K2, +far at K1, with K1 below K2 for calls and above it for puts.

  Both also come in a reverse (short) version.
- The analysis uses the multi-expiry what-if (D4). The builder labels it "at the first expiry, at today's implied volatility level".

**One-click roll.** On an option position or a combo, "Roll" proposes:
- closing every leg of the near expiry;
- opening the same structure on a later listed expiry (the next one by default; any later listed one may be chosen), at the same strikes when listed, else the nearest listed strikes, with "re-centre at the money" as an option.

The roll is one combo submission:
- All legs share the original combo id and carry `"roll": "<n>"`.
- Market legs by default; limit legs with `ioc` as an option.
- Previewed with `tl_combo_preview` and the what-if of the holdings after the roll.

**Adjust.** On a live combo:
- "Close leg" sends the opposite order for one leg.
- "Add hedge leg" offers a perpetual leg (with the hedge-to-neutral quantity of D9 as default) or an option leg.
- The new legs carry the same combo id and `"adjust": "<n>"`.
- Before sending, the builder shows the what-if of the holdings **after** the adjustment. When a close leaves a short uncovered (`maxLoss` becomes `null`, or the margin rule pairs it naked), it requires a confirmation naming the uncovered leg.

**One ordering rule for every sequence.** Rolls, adjustments and new combos use the same placement order: every option **buy** (opening a long or closing a short), then the perpetual, then every option **sell** (closing a long or opening a short). Protection is therefore always added before it is removed, and margin freed by a buy-to-close is available to the new sells. A refusal at placement cancels the accepted legs of that submission before the player can advance (unchanged).

**Tags.** The tags (`combo`, `strategy`, `roll`, `adjust`) are ignored by replay and by the server. A test checks that a tagged log replays to the untagged digest, as today.

Rejected alternatives:
- An engine **roll** or **adjust** command. It is the same trade-off as the engine combo order (add-academy-and-strategies D3): a command, a checkpoint field, all-or-nothing rules and a policy bump, for protection against one rare case.
- Calendars with **more** than two expiries. They are not needed for the lessons, and they make the analytics' horizon ambiguous.

### D4. Engine: multi-expiry analytics, the heatmap and the holdings, all report-only

There are four additive extensions of `tl_option_whatif` / `tl_run_option_whatif`. All are optional request fields with new reply blocks, so `kAbiVersion` stays 1 and every existing fixture is unchanged.

**`horizon` analytics.** When option legs have different expiries, the reply gains `horizon`: the position at the **first** expiry.
- Each leg expiring then is at intrinsic value. Each later leg is at its model value with `make_inputs` at that time:
  - the run what-if uses the current step's IV base (no look-ahead), the term factor for its remaining time, and skew and smile at the price;
  - the run-less what-if uses the leg's own IV plus `ivPoints`.
- Fields:
  - `curve`: the grid;
  - `breakevens`: roots of the P&L curve. Each is found by bisection over the **price tick grid** (integer ticks, so the search is exact and identical on every target) between grid points where the sign changes, and reported as the tick on the profitable side nearest the crossing;
  - `maxProfit` and `maxLoss`: the extremes over the grid, the strikes, the breakevens and the price 0;
  - `unboundedAbove`: true when the asymptotic slope (net long calls minus short calls beyond every strike, the later legs counted at intrinsic) is unfavorable. Then `maxLoss` is `null`, as in D4 of add-academy-and-strategies.
- `expiry` stays `null` for such legs, as today.

**`heatmap`.** Request `{"lo","hi","points"≤61,"bars":[…]≤25}` for a run, or `"minutes":[…]` without one. The reply `heatmap.pnl[t][p]` is the P&L of the position against its entries at each price and time:
- legs past their expiry at intrinsic value;
- the run what-if's IV rule (today's base, no look-ahead).

The cap of 61 × 25 points bounds the cost to about 1,500 valuations per leg.

**`holdings: true`** (run only). The legs are the run's own positions: the perpetual position and every option position, with their average entries. A held perpetual's working reduce-only stop goes in as the leg's `stop`, as the builder does today. The reply gains `risk`:

| Field | Meaning |
|---|---|
| `deltaUnits` | Σ units × delta (perpetual delta 1) |
| `deltaNotional` | `deltaUnits` × last close |
| `gamma1pct` | ½ × \|Σ units × gamma\| × (0.01 × last close)², the P&L from gamma of a 1% move |
| `vegaPoint` | Net vega per volatility point |
| `thetaBar` | Net theta per bar |
| `fundingNext` | The perpetual's next funding payment estimate: −qty × last close × the published next rate (positive = received) |
| `*Bp` | Each of the above as basis points of equity |
| `limits` | The mission's risk limits (D9) with `exceeded` flags |

Clients show these numbers and do not compute them.

**Limit legs in the dry run.** `tl_combo_preview` legs take optional `type`, `premium` and `tif` (D2).

**Numeric representation** (new quantities):

| Quantity | Type, scale | Rounding |
|---|---|---|
| Horizon curve, heatmap cells, `maxProfit`/`maxLoss` at the horizon | `Money`, 1e-8 | Half-even from quantized per-unit values × units (`units_value`), as D4 of add-academy-and-strategies |
| Horizon breakevens | `Price`, on the tick grid | Exact: the tick on the profitable side nearest the crossing |
| `deltaUnits` | Decimal, 1e-8 | Half-even, Σ units × quantized delta |
| `deltaNotional`, `gamma1pct`, `vegaPoint`, `thetaBar`, `fundingNext` | `Money`, 1e-8 | Half-even; `gamma1pct` computed in fixed point from the quantized gamma |
| `*Bp` | int32 basis points | Half-even of value × 10,000 / equity; 0 when equity ≤ 0 |

**Fixtures.** Two new `whatif` fixtures: `whatif-calendar` (horizon analytics) and `whatif-heatmap` (a run-less condor grid).

**Tests.**
- The horizon breakevens against a brute-force scan of every tick.
- `unboundedAbove` for long and short calendars and diagonals.
- The heatmap at time 0 equals `today` at the same prices.
- No look-ahead: OPT06 extended to `horizon`, `heatmap` and `risk`.
- Purity: state digest and event log unchanged.
- `holdings` equals the explicit legs.

Rejected alternatives:
- **Client-side heatmap** from the single-point what-if: up to 1,500 worker round trips, and every number still has to come from the engine.
- A **separate `tl_portfolio` call**: it would duplicate the what-if's leg handling. `holdings` reuses it.

### D5. Calendar margin, declared per mission

`OptionSpec.calendar_margin` (bool, default false; valid only with `spread_margin`). When true, spread-margin pairing (D5 of add-academy-and-strategies) also lets a **long whose expiry is at or after the short's** cover the short:
- the requirement per paired unit is the vertical formula: calls max(0, K_long − K_short), puts max(0, K_short − K_long);
- candidates are tried in this order: the same expiry first (as today), then later expiries ascending, then series id;
- the condor rule applies within the short's expiry.

**Why it is sound.** At a zero rate a European option under Black-76 is worth at least its intrinsic value against the forward. So at the short's expiry the later long is worth at least max(0, F − K_long) for a call, and the pair cannot lose more than the formula. Before that, equity marks the long at its bid, the same approximation the vertical rule already accepts.

**Why a flag.** Pairing across expiries would change margin, and so execution, for any recorded m13–m17 run that briefly held a near short and a far long (m17's rolls). Only m21 declares it, so no digest changes: the pattern of D8.1. The briefing names the rule (`missionRules.calendarMargin`). Packs cannot declare it yet.

**Property tests.**
- A calendar's short needs 0 margin.
- A diagonal's need is ≤ the strike gap × units.
- A long that expires **before** the short never covers it.
- Pairing is order-independent.
- With the flag off, `option_book` is identical on m13–m17.

### D6. Time in force (execution policy v18)

`Order.tif` (`gtc`, `bars`, `ioc`, `fok`) and `Order.expire_step` (u32; 0 = none). The command takes optional `tif` and `tifBars`. An absent field means `gtc`, so every existing log executes as before.

| TIF | Allowed on | Rule |
|---|---|---|
| `gtc` | every order | Today's behavior: works until filled, cancelled, the run ends or (options) its series settles |
| `bars` (N ≥ 1, ≤ the bars left) | limit, stop-market, take-profit, trailing stop; option limit | Eligible on bars placed_step+1 … placed_step+N. Any remainder at the close of bar placed_step+N is `expired` with reason `time_in_force` (38), after the close phase's fills. The UI's "day" is N = 86,400 / `stepSeconds` bars |
| `ioc` | limit (perpetual and option) | Eligible only in the next bar's **open** phase: the perpetual open phase for a perpetual limit, the option open phase for an option limit. It fills what the open's price and capacity allow (capacity served as in policy v16), and any remainder is cancelled `time_in_force` right after that phase |
| `fok` | limit (perpetual and option) | Same eligibility as `ioc`, but it fills its **whole** quantity in that phase or nothing (cancelled `time_in_force`). The check runs at its turn in the phase's priority order, after the orders served before it have taken their capacity |

- **Refusals.** `ioc`/`fok` on a market, stop, take-profit or trailing order, `bars` with N = 0 or beyond the run's last bar, and any `tif` on a bracket child are refused with `invalid_time_in_force` (39). Market orders are already immediate: their unfilled remainder keeps today's `insufficient_capacity` cancel.
- **Brackets.** A bracket entry may carry `bars`. Its children attach at fill as `gtc`, so protection never expires silently.
- **Why "immediate" means the next open.** In a bar engine an order placed at the close of bar t is first eligible at the open of t+1. That open is the only "immediate" price that uses no future information. A limit that would only fill intrabar is not immediate.
- **Why an engine rule.** A client cannot emulate `ioc`, `fok` or `bars`. Its cancel can only be sent at the next decision step, after the whole next bar (open and intrabar) has executed, so an "ioc" limit could still fill intrabar. Client emulation would also make replay depend on client timing.
- **IDL.** `Reason` gains 38 `time_in_force`, 39 `invalid_time_in_force` and 40 `invalid_oco`. `OrderView` gains `tif`, `tifBars` and `expireStep`. `PlaceOrderCommand` and `PlaceOptionOrderCommand` gain optional `tif` and `tifBars`.
- **C ABI.** `tl_place_order_tif(run, cid, type, side, qty, price, ro, stop, tp, trail, trail_rate, tif, tif_bars)` and `tl_place_option_order_tif(run, cid, series, side, type, qty, premium, tif, tif_bars)` are new functions; the old ones are unchanged and mean `gtc`. Python `Run.place_order(..., tif=None, tif_bars=None)` and `Run.place_option_order(..., tif=…)`. Run-file actions: `"tif"` and `"tifBars"` on `order` and `opt` actions.

### D7. Player-built OCO groups (execution policy v18)

A new command, `LinkOco` (CommandType 11; `tl_link_oco(run, cid, order_a, order_b)`; Python `Run.link_oco`; run-file action `{"k":"oco","a","b"}`), links two **working** orders of the run.
- **Which orders.** Perpetual or option orders, in any combination. Neither may be a bracket child or already linked.
- **Group id.** `Order.link_group` = the lower order id. It is a separate field from the bracket `oco_group`, so a bracket parent can be linked without merging its children's group.
- **Refusals.** The same order twice, an unknown or inactive order, a bracket child, or an order already linked are refused with `invalid_oco` (40). A refused link changes nothing.

**Semantics.**
- **The first fill wins.** When either order fills (any quantity), the other's remainder is cancelled with `oco_sibling_filled` (23).

  The exception is two reduce-only exits of the same position: as bracket legs do, the sibling is **reduced** by the filled quantity, so a partial exit keeps protection for the rest.
- **Same bar.** The bar's phase order decides: perpetual open, option open, intrabar adverse then favorable exits, entries, then close. Within a phase the v16 priority (price, then sequence) decides. The order that executes first fills, and its sibling is cancelled **before** its own turn, so both can never fill on one bar.
- **Cancel, expiry or move.** A player cancel or a time-in-force expiry of one order dissolves the link, and the other keeps working. `MoveOrder` keeps the link.
- **Display.** `OrderView.linkGroup`. A link emits no event: it is visible in the state, the decision record of the step is `act`, and the event log of a run without links is unchanged. The cancel of a sibling emits the usual order update.

**Why an engine rule.** A client that cancels the sibling after a fill cannot stop both orders filling on the same bar (a breakout buy-stop and a sell-stop on a wide bar), which is exactly the case OCO exists for.

### D8. Policy v18 and checkpoint format 6: one bump, documented

- **One bump for both rules.** Time in force and OCO groups land together as **execution policy v18**.
- **Order serialization.** `Order` appends `tif` (u8), `tif_bars` (u32), `expire_step` (u32) and `link_group` (u32) to the checkpoint serialization (**format 6**) and to the state digest. Field order is append-only.
- **Effect on goldens.**
  - Every frozen **state** digest changes, but only through the policy number and the appended defaults: the same pattern as v13–v15.
  - **Event** digests are unchanged for every existing golden. A property test replays each golden's script under v18 and compares its event digest with the frozen v17 value.
  - A new golden, `m01-tif-oco`, exercises `bars`, `ioc`, `fok`, a linked entry pair on one wide bar, a linked pair of reduce-only exits with a partial fill, and every refusal.
- **Older runs.**
  - Clients pin the engine build per run (web engine pinning; the desktop records each run's engine and ABI), so a v17 run resumes on its own build.
  - The API replays v17 logs through the policy cache (`tools/build_policy_cache.py`). With `TRADELAB_POLICY_WINDOW` 3 the window becomes v16–v18.
  - Run files carry their policy, and `tl_run_file_verify` refuses a policy it cannot replay, as today.
- **Docs.** The engine README's policy history gains a "Policy v18" section. The `kExecutionPolicyVersion` comment names it.

Rejected alternatives:
- **Per-mission flags** for time in force and OCO. Flags work for margin rules that are mission data, but these are new **commands and order fields**: the policy number is what tells a replayer it can read them.
- **Two bumps** (v18 time in force, v19 OCO). That would mean two digest regenerations and two cache entries for one wave.

### D9. Portfolio view and per-mission risk limits

**The view.** A terminal panel ("Portfolio", also a rail summary) shows:
- per position: the perpetual and each option series, with quantity, value, P&L and delta, gamma, theta and vega;
- the totals and `risk` of D4 (`holdings: true`): delta in units and notional, gamma per 1% move, vega per point, theta per bar, the next funding payment.

It is refreshed on every state change, with stale replies dropped as the builder does.

**Risk limits.** A new `MissionRules.risk_limits`, all optional, in basis points of equity:

| Limit | Applies to |
|---|---|
| `deltaBp` | \|`deltaNotional`\| |
| `gammaBp` | `gamma1pct` |
| `vegaBp` | \|`vegaPoint`\| |
| `thetaBp` | −`thetaBar` (theta burn) |

- **State and briefing.** `tl_state` lists them in `missionRules.riskLimits`, and the briefing shows them.
- **Warnings.** The panel marks a total above its limit, and the rail shows a warning line naming the limit. A warning never blocks an order.
- **Debrief.** The debrief adds the report-only finding `risk_limit_exceeded` (scoring v14): the first step and the number of closes above each limit, measured at each close after the close phase from the engine's own Greeks.
- **Missions without limits.** m01–m17 declare none, and the panel shows only the mission's existing limits (leverage cap, liquidation warning, the 2% risk rule).

**Hedge to neutral.** "Hedge delta" proposes a perpetual order of −`deltaUnits` rounded **toward zero** to the lot. It is a normal ticket with the plan dialog and a confirmation, never sent automatically. m18 and m19 teach it.

Rejected alternative: client-side limit checks from displayed numbers. Web and desktop could round differently, and the debrief finding must be the engine's anyway.

### D10. The existing trailing stop, easier to find and taught

There is no engine change: the order type and its two modes (price distance, % of the best close) exist since policy v16.
- **Ticket.** The protective section becomes three visible tabs: **Stop · Trailing · Target**. "Trailing" is no longer behind the stop field.

  In the new-order ticket, a "Protect with" choice offers **fixed stop** (bracket, as today) or **trailing stop**. The engine does not attach a trailing stop to an entry, so choosing trailing records a *planned* trailing stop in the plan (`plan.trail`: mode and value; client data, logged with the plan's tags and ignored by replay).
- **Planned trailing notice.** On the first decision after the entry fills, a notice offers "Place the planned trailing stop" with the computed first trigger. One click opens the filled ticket. Nothing is sent without the click.
- **Chart.** The `TRAIL` label shows the rule and the trigger ("TRAIL 2% · 101,96"), and a small marker shows the best close the trigger follows. The trigger comes from the engine's order price; the best close is the highest (long) or lowest (short) close since placement, from bars ≤ the current step.
- **Teaching.**
  - A new step in `orders-execution`: a `chart` widget with closes and the trigger ratchet, and a `trail` check (trigger = best close − distance, or best close − ceil(close × rate) rounded outward to the tick).
  - A trailing card in m22's tutorial (new art: `trail`).
  - A worked example for the existing `trailing-stop` glossary term.
  - m07's existing card text links the glossary term (client text only).

  The `trail` check is verified by Vitest against `stops.ts` and by `bindings/python/tests/test_academy_numbers.py` through a scripted engine run.

Rejected alternative: a trailing stop attached to an entry in the engine. That is a new order behavior, explicitly out of scope.

### D11. Generator: existing capabilities only (v5)

Every new market is built from existing capabilities:
- segments with `iv_mult` and `funding_bias`;
- random-direction drift;
- G1 regime chains;
- G2 drawn outcomes;
- G3 announced releases;
- event jump, funding, spread and capacity channels;
- the crowding squeeze;
- per-seed lead jitter.

| Need | Built from |
|---|---|
| Realized above implied (m18) | A G1 chain of `Chop` states with high `vol` and `iv_mult` 0.7–0.8 against `Quiet` states with `iv_mult` 1.2: drawn per seed whether rehedging pays |
| Carry and its reversal (m19) | Segments with positive `funding_bias` (crowded longs), the squeeze channel, and a drawn `unwind` state with negative `funding_bias` |
| IV bid and crush (m20) | A G3 release announced 30–40 bars ahead; the segment before it with `iv_mult` 1.5–1.7; the event's `iv_mult` 0.55–0.65 over its duration; a G2-drawn surprise |
| Calendar range (m21) | A Level-anchored range with `iv_mult` 1.0, then a drawn breakout whose timing falls before or after the near expiry |
| Trend with pullbacks and targets (m22) | Trend-anchored segments with momentum and pullbacks, a drawn direction |
| Psychology traps (m23–m25) | m23: an early failed breakout with an event jump against it, then chop, then a drawn real move. m24: a quiet range, then a parabolic run with volume and extreme funding, then a squeeze reversal in most seeds. m25: a trend with sharp counter-bounces and a drawn reversal in about 30% of seeds |
| Round markets | Four specs (trend, range, breakout, event), each with drawn directions |

If tuning proves a capability missing (the likeliest case is an IV ramp anchored to an announcement rather than to a segment boundary), it is added as **G5**:
- declared per mission;
- `kGeneratorVersion` 6, not a digest input: specs that declare none generate exactly as in v5;
- recorded as an "As built" decision here.

The bands are never widened silently.

### D12. Missions m18–m25

**Common to all.**
- Synthetic, hourly bars, warm-up 60, starting balance 10,000 (campaign: carried capital).
- Perpetual at 3× (m19 and m22 at 5×).
- Funding every 8 bars.
- One golden each with default options, `tl_mission_reveal` labels, and two rubric questions each in `engine/rubrics/lessons.json` with `reviewed: false`.
- Titles are neutral about the outcome. The tutorial cards teach the discipline, never the trap, so a psychology mission's briefing does not say which bias it tempts.
- Segment values are starting points, tuned until the bands pass; any change to a band is recorded in "As built".

| | m18-gamma-scalp | m19-funding-carry | m20-event-vol | m21-calendar | m22-scale-manage | m23-second-chance | m24-fast-market | m25-the-dip |
|---|---|---|---|---|---|---|---|---|
| Title PT / EN | Missão 18 · Hedge / Mission 18 · Hedge | Missão 19 · Carrego / Mission 19 · Carry | Missão 20 · Anúncio / Mission 20 · Announcement | Missão 21 · Calendário / Mission 21 · Calendar | Missão 22 · Gestão / Mission 22 · Management | Missão 23 · Recomeço / Mission 23 · Restart | Missão 24 · Disparada / Mission 24 · Rally | Missão 25 · Queda / Mission 25 · Dip |
| Options | buy only | buy and write | buy and write | buy and write | none | none | none | none |
| Margin flags | — | `spread_margin` | `spread_margin` | `spread_margin`, `calendar_margin` | — | — | — | — |
| Risk limits | `deltaBp` 1,500 | `deltaBp` 1,000 | `vegaBp` 150 | `gammaBp` 50 | — | — | — | — |
| Chain (expiries after listing; strikes) | +40, +80; ±4 × 2.5 | +60, +120; ±4 × 2.5 | +10 after the release, +60; ±5 × 2.5 | +20, +45, +70; ±5 × 2.5 | — | — | — | — |
| Stage | Advanced options and carry (elective) | same | same | same | Trade management | Psychology | Psychology | Psychology |

**m18 Hedge: gamma scalping.**
- **Story.** The market sometimes moves more than its options price in and sometimes less. The player cannot know which in advance.
- **Lesson.**
  - A long ATM straddle is long gamma.
  - Delta-hedging it with the perpetual locks in about ½Γ(ΔS)² per rehedge.
  - Theta is the rent paid for that gamma.
  - The hedged straddle earns when realized volatility beats implied and loses when it does not.
  - Hedging more often captures more of the moves but pays more fees.
- **Objectives.**
  - Keep |delta| within the mission's limit.
  - Record the rehedge rule (every N bars or a delta band) before entering.
  - Premium ≤ 3% of equity.
- **Reveal.** Realized against implied volatility per regime visit; the comparison: unhedged straddle, hedged every 4 bars, hedged on a delta band.
- **Cards** (4): `scalp` (new art: a convex curve and rehedge points), `delta`, `decay`, `costs`.
- **Rubric.**
  - Numeric: a straddle with delta +0.20 per unit on 50 units: perpetual units to sell to be delta-neutral (**10**).
  - Choice: what pays the theta of a delta-hedged long straddle (realized moves larger than the implied volatility priced in).

**m19 Carry: funding on a hedged position.**
- **Story.** Longs are crowded and pay funding. Shorting the perpetual collects it, and a synthetic long (long call + short put, same strike and expiry) removes the price risk. Some seeds stay crowded; in others the crowd unwinds, funding flips negative or a squeeze hits the short perpetual.
- **Lesson.**
  - Carry = funding received − costs (option spreads and fees, perpetual fees).
  - Put-call parity at a zero rate: C − P = F − K, so a long call + short put at K behaves like a long forward.
  - The hedge removes price risk, **not** margin risk. The perpetual is isolated, so a squeeze can liquidate its leg while the options gain: the book is hedged, the margin is not.
  - Carry turns against the holder when funding turns negative, when costs exceed the funding left to earn, or when leverage on the perpetual leg is too high.
  - In real markets "basis" is the gap between a perpetual or future and spot. Here the funding rate is the carry, and the options supply the forward.
- **Objectives.**
  - Write the carry break-even (funding needed to pay the costs) before entering.
  - Keep the perpetual's liquidation at least 15% away.
  - Keep |delta| within the limit.
- **Reveal.** The funding path and whether the crowd unwound; the comparison: unhedged short, hedged at 5× and hedged at 2× (liquidation of the perpetual leg named).
- **Cards** (4): `carry` (new art: funding received against costs), `funding`, `margin` (isolated, not netted against the options), `parity`.
- **Rubric.**
  - Numeric: funding received in one day by a short of 20 units at 100 at +0.03% per interval, 3 intervals a day (20 × 100 × 0.0003 × 3 = **1.80**).
  - Choice: when carry turns against a short-perpetual carry (funding turns negative: shorts pay).

**m20 Announcement: volatility around an event.**
- **Story.** A release is announced 30–40 bars ahead. Options get more expensive as it nears; after it, implied volatility falls back whatever the outcome. m09 asks whether to hold one straddle into a release. m20 is about **when** implied volatility is bought and sold around it.
- **Lesson.**
  - IV rises before a known event and collapses after it (IV crush).
  - An ATM straddle's price ≈ the implied move.
  - Buying early and selling before the release earns the IV bid without the crush.
  - Holding through needs a move larger than the implied move.
  - Vega × IV change sizes the crush.
- **Objectives.**
  - Write the straddle's breakevens before buying.
  - Decide before the release bar whether to hold through, and record why.
  - Keep |vega| within the limit.
- **Reveal.** IV before and after the release, the drawn surprise and the move against the implied move; the comparison: buy early and sell 1 bar before, buy early and hold through, buy 1 bar before and hold through, and a short straddle held through (where writing is allowed).
- **Cards** (4): `crush` (new art: an IV line rising, then falling at the release), `volatility`, `payoff`, `decay`.
- **Rubric.**
  - Numeric: the breakevens of an ATM 100 straddle bought for 6 (**94 and 106**; answered as the upper one, 106).
  - Choice: what happens to implied volatility after the announcement (it falls).

**m21 Calendar: calendar and diagonal spreads.**
- **Story.** A range with steady implied volatility. Sometimes it breaks out before the near expiry, sometimes after.
- **Lesson.**
  - A long calendar (short near, long far, same strike) earns the faster decay of the near option and profits most when the price ends near the strike at the near expiry.
  - Its maximum loss at the near expiry is the net debit.
  - It is long vega.
  - A diagonal shifts the long's strike to lean the structure.
  - Here the far long covers the near short in margin (calendar margin, D5).
- **Objectives.**
  - Write the net debit (the maximum loss at the near expiry) before entering.
  - Decide before the near expiry's last 5 bars whether to close, roll the short, or hold.
  - Keep gamma within the limit.
- **Reveal.** The range, the breakout bar and its timing against the near expiry; the comparison: calendar at the money, a diagonal one strike up, and a short straddle on the near expiry.
- **Cards** (4): `calendar` (new art: two expiries, the near one decaying faster), `decay`, `margin` (calendar margin), `roll`.
- **Rubric.**
  - Numeric: sell a near 100 call for 2.00 and buy a far 100 call for 3.50: the net debit, which is the maximum loss at the near expiry (**1.50**).
  - Choice: where the calendar profits most at the near expiry (near the strike).

**m22 Management: scaling and exits.**
- **Story.** A trend with pullbacks. Some seeds trend far, some stall at the first target, and some fail.
- **Lesson.**
  - Scale in only with total risk to one invalidation within budget.
  - A partial take-profit at +1R pays for patience on the rest.
  - Moving the stop to breakeven removes the remaining risk but can stop a sound trend on a normal pullback.
  - A trailing stop lets the rest run.
  - Time in force and OCO keep working orders tidy.
- **Objectives.**
  - Total planned risk ≤ 1% across all entries.
  - Use at least one partial exit or a trailing stop.
  - Never move a stop away from the entry.
- **Reveal.** The trend's reach and pullbacks; the comparison:
  - all-in and all-out at a 2R target;
  - half at +1R, stop to breakeven, trail the rest at 2×ATR;
  - hold to the end with the initial stop.
- **Cards** (4): `scale` (new art: entries and partial exits on one trend), `trail` (new art: the trigger following the best close), `risk`, `plan`.
- **Rubric.**
  - Numeric: long 30 at 100 with a stop at 96; sell 15 at 104 and move the stop to 100: the worst result left for the trade before costs (15 × 4 = **60**).
  - Choice: what moving the stop to breakeven gives up (it can stop out a sound trend on a normal pullback).

**m23 Restart, m24 Rally, m25 Dip: psychology.**
- **Story.**
  - m23: an early clean-looking breakout fails fast and stops many players out; chop follows, and a real move comes later.
  - m24: a quiet range, then a steep rally with heavy volume and extreme funding, which reverses in most seeds and continues in some.
  - m25: a trend with sharp counter-moves, which reverses for good in some seeds.

  The briefings describe the market neutrally.
- **Lesson.**
  - m23: after a loss, wait for the setup to form again and size normally.
  - m24: an entry far from structure means a far stop or a tiny size; plan before the move, not during it.
  - m25: adding to a loser is only consistent when it was planned at entry with total risk to the same invalidation.
- **Objectives.**
  - m23: within 3 bars of a losing close, no new entry without a new plan or at 1.5× the size (as built: the rule reads any losing close, see "As built: wave 2 cross-check and playtest").
  - m24: every entry has a plan recorded before it, with the stop ≤ 2×ATR.
  - m25: no add to a losing position beyond the planned total risk.
- **Reveal.** The trap's bars and what followed; the bias findings (D13) with their evidence; the comparison: a cooled-down player against an immediate re-entry (m23), a planned breakout entry against a chase (m24), a planned single entry against averaging down (m25).
- **Cards** (3 each, from `cooldown`, `plan`, `risk`, `journal`; new art `cooldown`): they teach the rule, never the trap.
- **Rubric.**
  - m23. Numeric: two 1% losses, then the third trade sized at 2% also loses: the total drawdown before compounding (**4%**). Choice: the sound step after a stop-out.
  - m24. Numeric: entry 112 with the invalidation at 104 and a risk budget of 100: the largest quantity (100 / 8 = **12.5**). Choice: why a chased entry is worse for the same risk (a farther stop forces a smaller size, or worse reward to risk).
  - m25. Numeric: long 10 at 100, add 10 at 90, stop at 85: the loss at the stop (10 × 15 + 10 × 5 = **200**). Choice: when adding to a loser is consistent with a plan.

Every number above was checked by hand. The rubric tests and the lesson checks recompute them.

**Learnability bands** (60 seeds; the D8.3 rules hold: buy-and-hold positive in < 75%; every strategy that can lose loses in ≥ 15%, except payoff properties).

| Mission | What a disciplined player can learn |
|---|---|
| m18 | Where realized/implied over the hold ≥ 1.2, the band-hedged straddle beats the unhedged one's return-to-risk and makes money in ≥ 70%. Where it is ≤ 0.85, the hedged straddle loses in ≥ 70%. The hedged result's standard deviation is ≤ 0.6× the unhedged one's. Hedging every bar pays ≥ 2× the fees of hedging on a band |
| m19 | Without a flip, the hedged carry nets positive after costs in ≥ 75%. In flip seeds it nets negative in ≥ 60%. At 5× the perpetual leg is liquidated in ≥ 25% of squeeze seeds; at 2× in ≤ 5%. The unhedged short loses in ≥ 35% of all seeds |
| m20 | IV after the release ≤ 0.7× IV the bar before, in 100% (generated property). A straddle bought early and sold 1 bar before beats holding through in ≥ 60%. Holding through wins in ≥ 85% of seeds where \|move\| > the straddle's price at entry and loses in ≥ 80% of the others |
| m21 | Loss at the near expiry ≤ net debit + costs in 100% (payoff property with D5). The calendar wins in ≥ 70% where the price at the near expiry is within one strike step of the strike, and loses in ≥ 75% where it is beyond three steps. It is never margin-called at a size whose debit is ≤ 5% of equity |
| m22 | The managed plan has a lower standard deviation of R than all-in/all-out and a win rate ≥ 10 points higher. All-in/all-out has the higher mean R in trend-far seeds (≥ 60%). Each plan loses in ≥ 15% |
| m23 | Waiting ≥ 3 bars after the first stop-out and re-entering on the reference setup loses less than an immediate re-entry at double size in ≥ 70% |
| m24 | An entry on the first range break with a stop below the range beats a chase entry (after ≥ 4 same-direction bars, > 2×ATR above the 20-bar mean) in R in ≥ 65%. The chase still wins in ≥ 15% (FOMO is not always wrong) |
| m25 | A single planned entry with its stop beats averaging down to the same stop in ≥ 65%. Averaging down wins in ≥ 20% (the trap works sometimes), and its worst seed is ≥ 2× worse |

The pooled realism bands (indicator 42–58%, news follow < 72%, fat tails, advisor bands) must still hold over the 25 missions and the round markets.

### D13. Scoring v14: report-only facts

None of these facts is a score input, and none enters a digest. Scores of existing runs are unchanged.

**Bias findings** (all missions; evidence from the action log and from data visible at the step).

| Code | Rule | Evidence |
|---|---|---|
| `revenge_trade` | An exposure-increasing entry within 3 bars after a perpetual trade closed at a loss, either sized ≥ 1.5× that trade's planned risk (or quantity when unplanned) or with no plan recorded | Losing trade n, its exit step and loss; entry step; size ratio (bp) |
| `fomo_entry` | An entry in the direction of ≥ 4 consecutive same-direction closes, with the close > 2 ATR(14) beyond SMA(20) at the entry step, and either no stop or a stop > 2 ATR away | Entry step; extension in hundredths of ATR; consecutive closes |
| `averaging_down` | An add in the same direction at a worse price than the average entry while unrealized P&L < 0, that raises the total planned risk above the first entry's plan, or is made with no stop | Add steps; average entry before and after; unrealized loss at the add; planned risk before and after |

**Position episodes.** `debrief.episodes`: from flat to flat on the perpetual.
- Entries (scale-ins) and partial exits.
- Stop moves: `to_breakeven`, `toward`, `away`.
- Whether a trailing stop was used.
- MFE and MAE in R.
- The combined R.

m22's objectives read them, and the debrief shows them for every mission.

**`risk_limit_exceeded`** (D9).

**Strategy comparison rules** (extending add-academy-and-strategies E9):
- `delta_hedge`: after entry, at each close every N bars, or whenever |delta| exceeds a band, a perpetual order brings the net delta to zero, rounded toward zero to the lot;
- `enter_before_release: N` and `exit_before_release: N` anchors;
- a short perpetual leg and multi-expiry legs;
- `managed_exit`: half at +1R, stop to breakeven, trail the rest at k×ATR, with existing orders only.

Rows keep the existing shape.

**Coach moments** (D17) are versioned with scoring v14.

**Numeric representation.**
- Ratios are int32 basis points, half-even.
- ATR extension is an int32 in hundredths of ATR, half-even.
- MFE, MAE and R use the existing trade R representation.
- Amounts are `Money` 1e-8.

**Tests.**
- Each rule fires on a scripted run and stays silent on a near miss.
- No look-ahead: altering bars after the entry step leaves the finding unchanged.
- Existing missions' scores are byte-identical (`tests/test_scoring.cpp` over every golden).

Rejected alternative: making bias findings score inputs. That would change the scores of stored runs on re-scoring and make psychology missions punitive. The objectives and the debrief name the bias instead.

### D14. Curriculum, stages and campaign placement

**Stages** (17 in all):

| Stage | Requires | Missions | Lessons | On the campaign path |
|---|---|---|---|---|
| `management` (Trade management) | `risk` | m22 | `management-scaling` | yes, after m07 |
| `advanced` (Advanced options and carry) | `strategies` | m18, m19, m20, m21 | `vol-gamma-scalping`, `carry-funding`, `vol-events`, `options-calendars` | **no** (elective) |
| `psychology` (Psychology) | `events` | m23, m24, m25 | `mind-biases` | yes, before m12 |

Existing stages keep their `requires`, so no passed or open stage becomes locked. The new stages are branches, and the map draws them beside the path.

**Campaign order** (21 missions): m06, m01, m03, m02, m07, **m22**, m04, m09, m05, m10, m13, m14, m15, m16, m17, m08, m11, **m23, m24, m25**, m12.
- Every new campaign mission meets no lock: m22 comes after Futures II, and the psychology missions after Events.
- The existing "no lock the engine cannot enforce" test keeps passing.

**Elective stages.** A stage may declare `campaign: false`:
- `campaignMissions()` skips its missions;
- a finished, non-assisted free-play or daily-challenge run of its mission adds it to `assessed`, so the stage can pass.

Why elective:
- m18–m21 are specialist structures that the capstone does not assess;
- 25 campaign missions would make the campaign too long (the fatigue risk noted in add-academy-and-strategies).

**Campaign order snapshot.** A campaign run stores `order` (its mission list) at start. Progress v4 migration gives an active run without one the 17-mission order of add-academy-and-strategies, so an index never jumps to another mission.

Rejected alternatives:
- Inserting m22 into the Risk stage. It would unpass Risk for every player who passed it with m07.
- All eight missions in the campaign. Too long.
- No new stages. The missions would have no assessment home and no lesson link.

### D15. Daily drills

**Content.** `apps/web/src/lib/content/drills/<skill>.json`, one file per skill. Skills: `payoff`, `breakeven`, `greeks-read`, `greeks-sign`, `sizing`, `liquidation`, `funding`, `r-multiple`, `hedge`, `calendar`, `trail`. Each **template** has:
- `id`, `skill`, `stage` and `lessons` (the lesson ids it reviews);
- `params`: ranges on the tick or lot grid, e.g. `{"entry":{"lo":"80","hi":"120","step":"0.5"}}`;
- `answer`: a lesson `check` kind with parameter references (D2 of add-academy-and-strategies; the new kinds of D10 and D16 included);
- `tolerance`;
- per locale: `prompt` with `{param}` placeholders, `explain` and optional `choices`.

Answers are computed by the **same check functions** the lesson tests verify:
- arithmetic kinds with the web mirrors (`sizing.ts`, `stops.ts`, `payoff.ts`), which tests compare with engine vectors;
- option values and Greeks with `tl_option_whatif` at run time.

A template whose engine call is unsupported on the pinned build is skipped.

**Session.**
- 6 items, a **soft** two-minute timer (shown, never failing: unanswered items count as skipped), and a "no timer" accessibility setting.
- A wrong answer shows `explain` with the worked numbers.
- The end screen shows right on the first try, and a link to each item's lesson.

**Choice of items.**
1. Up to 3 items from skills tied to lessons whose review is due (`lessonReviews`).
2. Then skills due in `drillReviews` (`{skill: {box, due}}`, the same 1/3/7/21-day boxes: all right moves the skill up, any wrong sends it to box 1).
3. Then the skills with the lowest first-try rate in `drillHistory` (the last 30 sessions).

Drill answers never complete a lesson review; the lesson review stays its own.

**Determinism and parity.**
- Parameters come from **mulberry32** seeded by FNV-1a 32 of `"drill-v1:" + localDate + ":" + sessionIndex`: `Math.imul` on the web, `uint32_t` on the desktop.
- A parity fixture `drills.json` holds the first 100 outputs for 3 seeds and the generated items with their answers.
- Decimal parameters are built from integer multiples of the step, never from floating-point products.

**Correctness tests.**
- Vitest generates 200 sessions per template and checks every answer with the check verifier.
- `bindings/python/tests/test_drill_numbers.py` checks the option kinds through the Python binding.
- A unit test requires that every number in an `explain` text comes from the item's parameters or answer.

Rejected alternatives:
- LLM-written or free-form items. They cannot be checked.
- A hard timer. It pressures the player, against the product's gamification rules.

### D16. Daily challenge

- **Date.** The device's **local** date `YYYY-MM-DD`.
- **Mission.** `DAILY_POOL_V1[dayIndex mod n]`.
  - `dayIndex` is the whole days from 2026-01-01, by a civil-date algorithm, not the clock in milliseconds.
  - `DAILY_POOL_V1` is a fixed, versioned list: every built-in mission except m12, the capstone, whose debrief carries the campaign report.
- **Seed.** FNV-1a 32 of `"tradelab-daily-v1:" + date` (0 becomes 1).
- **Same for everyone.** Two players on the same local date get the same mission and seed. A parity fixture `daily.json` covers 400 dates.
- **Run rules.**
  - Mission rules, no advisor, no intel perks and the default balance, so attempts compare.
  - The run record carries `daily: {date, mission, seed}`.
  - It counts like free play: it may assess an elective stage (D14), never a campaign leg, and it earns no advisor points.
  - Retries are allowed. The **first** finished attempt is the day's record; the best attempt is shown beside it.
- **History.** `dailyChallenges` in progress v4: `{date: {mission, seed, first: {score, pnl, runId}, best: {…}, attempts}}`, the last 90 days, ranked by the mission score (process), never by P&L.

  There is no streak counter and no leaderboard. A changed device clock picks another day, which is harmless without a leaderboard.

Rejected alternatives:
- A UTC date: players in Brazil would see the challenge change at 21:00 local time.
- A server-picked challenge: it needs login and sync, which is out of scope.

### D17. Coach review: an engine call over the finished run

`tl_coach_review(run)` (Python `Run.coach_review`, worker `coach`) is refused while the run is active, like the reveal. It replays the run's own log on a fresh state of the same scenario, seed and start options, as the strategy comparison does. At each decision step `s` it evaluates the rules with **only the state as of `s`**: bars ≤ s, indicators at s, the releases announced by s (without perks), positions, orders and plans.

It returns up to 12 `moments` ordered by step. Each has:
- `step`, `code` and `tone` (`warn` or `good`);
- `evidence`: decimals and integers as above;
- the `lesson` and `glossary` ids.

**Codes.**
- **warn:**
  - `entry_without_stop`;
  - `stop_moved_away`;
  - `added_against_structure` (an add to a long while the close is below SMA(20) **and** below the lowest low of the previous 10 bars, mirrored for shorts: "here you added risk while the trend broke");
  - `risk_above_budget`;
  - `held_into_release_unhedged` (an open position over an announced release bar with no stop, or an option position with vega above the limit);
  - `revenge_trade`, `fomo_entry`, `averaging_down` (D13);
  - `risk_limit_exceeded`.
- **good:**
  - `cut_at_invalidation`;
  - `stop_to_breakeven_after_1r`;
  - `partial_at_target`;
  - `stayed_flat_before_release`.
- **Selection.** At most 9 warn and 3 good moments. Warn moments are ranked by risk impact, then step.

**Coach mode** (client). From the debrief, "Review with the coach" steps through the moments.
- **Chart.** It shows the chart up to the moment's bar only. The future of that moment is not drawn, so the player judges with what they saw.
- **Text.** The moment's text (per code, PT-BR and en, numbers from `evidence`).
- **Controls.** "Next" and "Retry from here" (D18) for warn moments.
- **Close.** The last screen links the reveal.

**Tests.**
- No look-ahead: altering bars after `s` leaves every moment at or before `s` unchanged.
- Each code on a scripted run.
- A new golden fixture kind `coach` (a scripted run and its expected moments), replayed by every binding and by the desktop.

Rejected alternatives:
- Client-side rules. They would need porting to the desktop with parity fixtures, and they would recompute indicators the engine already has.
- Moments inside `tl_debrief`. The review replays the log and is heavier than the debrief, so it stays on demand.
- An LLM coach. It cannot be checked or reproduced.

### D18. Mistake replay

**Where it is offered.** Every debrief finding with a `step` and a warn tone, and every warn coach moment, offers "Retry from here".
- It starts an assisted rewind (the existing `rewindPrefix`) to the decision of step `s`: the actions logged before `s` are kept, so the player is back at the close of bar `s` before the mistaken action.
- The new run record's link is `{"from","kind":"mistake","step","code"}`.
- The terminal shows a banner naming the moment, never what happened next.

**Rules.** The rules of assisted practice apply:
- no advisor;
- never a mission completion, points or a campaign leg;
- excluded from journal statistics;
- not offered inside a campaign mission in progress or on imported runs.

**Comparison.** At the retry's debrief, the client compares the retry's coach moments and findings with the original's code:
- **repeated**: the same code at a step in [s, s + 5];
- **avoided**: otherwise.

It also shows the two results side by side, labeled one sample.

No engine change: findings already carry steps, and coach moments come from D17.

### D19. "Guess the next move" rounds

- **Round markets.** Four engine scenarios: `r01-trend`, `r02-range`, `r03-breakout`, `r04-event`.
  - Built from existing capabilities.
  - Listed by `tl_missions` with `"practice": "round"`; menus, the campaign, the curriculum and the daily pool skip them.
  - One golden each and no rubric.
  - They must pass the realism bands, so the next move is never predictable from one indicator.
- **A round.**
  1. The client draws the market, seed and decision step from mulberry32 (seed from the date and the round index; the step in [warm-up + 20, total − 13]).
  2. It starts an engine run and advances with no actions to the decision step. The chart shows bars ≤ t and the usual indicators.
  3. The player commits:
     - a decision: long, short or no trade;
     - P(close after 12 bars > close now), from 5% to 95% in steps of 5;
     - for a trade, an invalidation price and a target price.
  4. The client advances 12 bars and reveals them: the path, whether the stop or the target came first (information only), and the ATR.
- **Score** (0–100, kept in hundredths with integer arithmetic; module `guessRound.ts`, ported with parity fixture `guess.json`):
  - **Calibration (50).** 50 × (1 − 2 × Brier), with Brier = ((p − o) / 100)². In integers: p in percent, o ∈ {0, 100}, so 50 − (p − o)² / 100, which is exact in hundredths because p − o is a multiple of 5. The result is floored at 0 (p = 50 gives 50 − 25 = 25; p = 80 and the close higher gives 50 − 4 = 46; p = 80 and the close not higher gives 50 − 64, floored to 0).
  - **Plan (40), trades only:**
    - the invalidation is on the losing side (10);
    - its distance is between 0.5 and 3 ATR(14) at t (15);
    - reward-to-risk ≥ 1.5 (15).
  - **Consistency (10).** Long with p ≥ 55, short with p ≤ 45, or no trade with 40 ≤ p ≤ 60. A no-trade decision scores the plan part as 40 when 40 ≤ p ≤ 60 (no edge, no trade), else 0.
  - **No P&L.** The score never uses P&L.
- **Results.** A round shows "one sample". `guessHistory` in progress v4 keeps the last 200 rounds (market, seed, step, decision, p, score). After 20 rounds the screen shows a calibration table (stated against observed frequency per 10% bucket).
- **Decimals.** Prices, ATR and distances are compared in fixed point at 1e-8 (the engine's decimal strings, `BigInt` on the web, `int64` on the desktop), never in floating point.

Rejected alternatives:
- Rounds on mission markets. They would spoil the missions' stories.
- Client-made bars. They would not be engine data and would not match across clients.

### D20. Data model: progress v4 and run links

Web `meta.progress` **version 4** (one migration, landed with the first wave-2 or wave-3 task that needs it, carrying every key below):

| Key | Shape | Default |
|---|---|---|
| `campaign.order`, `campaignHistory[].order` | mission ids | the 17-mission order for runs that predate v4 |
| `drillReviews` | `{skill: {box, due}}` | `{}` |
| `drillHistory` | last 30 sessions `{date, items, firstTry}` | `[]` |
| `dailyChallenges` | D16 | `{}` |
| `guessHistory` | last 200 rounds (D19) | `[]` |

- **Migration.** `migrateProgress` reads v1–v3 and fills the defaults.
- **Run records.** They gain the optional `daily` link and the assisted kind `mistake`.
- **Desktop.** The SQLite progress row stores the same JSON (v4). A v3 row reads with the defaults. The SQLite schema and IndexedDB `DB_VERSION` are unchanged.
- **Logs.** Action-log entries may carry `tif`, `tifBars` (order actions) and the new `oco` action, which every replayer applies, plus `roll` and `adjust` tags, which replay ignores.

### D21. Desktop

The desktop follows the web with the same content files, engine calls and parity fixtures:
- **Engine adapter.** `tl_place_order_tif`, `tl_place_option_order_tif`, `tl_link_oco`, the what-if's `horizon`, `heatmap` and `holdings`, and `tl_coach_review` on the `EngineHost` worker. `storage::Action` gains `tif`, `tifBars` and the `oco` action.
- **Terminal.**
  - The ticket's time-in-force selector.
  - The orders panel's "Link OCO" (select two orders) and link marks.
  - The Stop · Trailing · Target tabs and the planned-trailing notice.
  - The chart's trailing label and best-close marker.
- **Builder panel.** Limit legs, two expiries, calendar and diagonal templates, roll and adjust.
- **New panels.** The heatmap (a cell grid painted by the scene painter, a diverging palette that also works with the colorblind setting, the value of the focused cell on the help line, and a table view for screen readers) and the portfolio panel with its warnings.
- **New screens.** Drills, Daily challenge, Guess rounds, the coach mode overlay on the debrief, and the mistake banner in the terminal.
- **Missions.** m18–m25 in the menu, campaign, briefing (risk limits, calendar margin), tutorials (new art as display lists) and debrief (biases, episodes, comparison).
- **Ports with parity fixtures.** `combos` (states), `roll`, `drills`, `daily`, `guess`, `trail` and `portfolio` formatting.
- **Captures.** Windowed reference captures for every new screen on macOS and on Linux (lavapipe), regenerated only for intended changes.

### D22. Testing

| Layer | Tests |
|---|---|
| Engine | Policy v18: each TIF row of D6 and every refusal; OCO first-fill, the same-bar race, reduce-only reduction, dissolve on cancel or expiry, kept by MoveOrder; event digests of every existing golden equal under v18; checkpoint 6 round trip and format 5 refused; `m01-tif-oco` golden. What-if: horizon breakevens against a brute-force tick scan, the heatmap at time 0 equal to `today`, `holdings` equal to explicit legs, no look-ahead, purity. Calendar margin properties. m18–m25 and round goldens; `test_mission_learnability_d.cpp` (D12 bands); realism over 25 missions and the round markets. Scoring v14 rules; coach review rules, no look-ahead and `coach` fixtures; existing scores byte-identical |
| Bindings | Python `place_order(tif=)`, `link_oco`, `coach_review`, `option_whatif` with `horizon`/`heatmap`/`holdings`; new fixtures in every golden reader; `test_academy_numbers.py` and `test_drill_numbers.py`; run-file schema with the new fields; a tagged log (`roll`, `adjust`) replays to the untagged digest |
| API | v18 logs replay with the current engine; v17 logs through the policy cache (`tests/test_policies.py`); OpenAPI snapshot unchanged |
| Web unit | TIF and OCO view models; combo states (working, complete, incomplete, void); roll and adjust legs and ordering; heatmap and portfolio view models with a fake worker; trailing planned notice; progress v4 migration; drills generation and scheduling with a fake clock; daily selection (parity fixture); guess scoring (parity fixture); coach and mistake view models; content schema for new lessons, drills and glossary terms; no mission id in lessons or drills |
| Web e2e | An IOC limit that does not fill; an OCO pair on a breakout; a limit calendar built, then rolled, then adjusted with a hedge leg; the heatmap and portfolio panels with a risk-limit warning; the planned trailing stop placed after the fill; each of m18–m25 to the debrief; a drill session; the daily challenge twice (first and best); five guess rounds; the coach review; a mistake retried with the assisted label; axe on every new screen |
| Desktop | Unit tests for every port (web parity fixtures); headless flows by keyboard for each new panel and screen; windowed captures on both platforms; the playtest extended to 25 missions and the practice formats; goldens include the new fixtures; complexity ≤ 15 |

### As built: wave 1 engine, bindings, API and web

Where the build departs from or adds to D2–D10 (tasks W1-E1–E9, W1-W1–W9):

- **Time in force (D6).** Also refused with `invalid_time_in_force`: `tifBars` without the `bars` kind (a `gtc`, `ioc` or `fok` with N ≠ 0). An out-of-range `tif` code at the C ABI returns that reason without a command (like an unknown order type). `ioc`/`fok` set `expireStep` to placed + 1 for display. On a bar without trading (continuity) an immediate order is cancelled unfilled at that bar. Option `bars` is bounded by the run's last bar in the engine; the web also stops it before the series' settlement.
- **OCO links (D7).** Every unfilled end of a linked order dissolves the link (a player cancel, a time-in-force expiry, a refusal at fill, a position closed, a settlement), through one hook in `emit_order_update` (`src/order_rules.cpp`). A link emits no event; the step's decision record is `act` through `RunState::last_link_step`, which is checkpointed (format 6) and not hashed. The accepted `LinkOco` result's `orderId` is the link group.
- **What-if (D4).** `horizon` needs no request field: it is present only when option legs span expiries (absent, not `null`, otherwise), so every earlier what-if fixture stays byte-identical. The breakeven search evaluates 0, the grid, the strikes and the perpetual stops snapped to the tick, then bisects integer ticks; run-less requests take an optional `tick` (default 0.01). Heatmap prices must be above 0. `holdings: true` may come with request legs (a proposed change): the risk block then covers the holdings plus those legs ("the holdings after the change" of D3). The held perpetual's `stop` is its working reduce-only stop nearest the close and beyond it. The risk block also carries `equity` and `fundingBp`; `fundingNext` is booked as `apply_funding` would at the latest mark close; `limits` entries are `{kind, limitBp, valueBp, exceeded}` with valueBp = |deltaBp|, gammaBp, |vegaBp| or −thetaBp.
- **Combo limit legs (D2).** Only option legs take `type`/`premium`/`tif` in `tl_combo_preview`; a perpetual limit leg is invalid input. The dry run values a limit leg's premium and fee at its limit and estimates its fill there.
- **Calendar margin and risk limits (D5, D9).** The state writes `missionRules.calendarMargin` and `missionRules.riskLimits` only when declared, so every mission without them serializes as before.
- **API (W1-E9).** `ACTION_KINDS` comes from the current binding, so a v18 action in a log recorded under a cached older policy is that run's `invalid_log`. OpenAPI snapshot unchanged.
- **Roll and adjust (D3).** A roll that leaves a short with no maximum loss (rolled past the long that covered it) asks for the same confirmation as an adjustment that closes a covering leg. Adding a hedge leg never asks (its own unbounded side is shown in the after-analysis). Combo records keep every submission (`submissions`: opening, rolls, adjustments, each at its step); the state of the latest one drives the notices, and the journal and debrief group all of them as one combo.
- **Portfolio (D9).** Rows show each position's engine value, P&L and per-unit Greeks; the totals are the risk block (no client Greek arithmetic). The panel opens from a rail summary (delta notional, next funding, warnings). "Hedge delta" asks for a confirmation, then goes through the plan dialog only when it adds exposure.
- **Planned trailing stop (D10).** `plan.trail` lives on the journal plan entry (also of a skipped plan), never in the action log or a run file; the notice follows the last perpetual plan. The `trail` check kind is computed by the exact fixed-point mirror `trailTrigger` (`stops.ts`), which a Vitest WASM run and `test_academy_numbers.py` compare with the engine's own trailing order bar by bar.
- **Glossary.** The substring search also lists `heatmap` for "ATM" (after `moneyness`); ranking is unchanged (out of scope).
- **Open (W1-W8).** No built-in mission declares risk limits before m18–m21 (wave 2), so the end-to-end risk-limit warning is covered by unit tests (a fake risk block) and the engine test with a declared limit; its e2e lands with m18.

### As built: wave 1 desktop

Where the desktop build (tasks W1-D1–D5) departs from or adds to the web's:

- **Engine adapter.** The desktop links the engine directly, so it has no pre-v18 build to feature-detect: every v18 control is always shown. `storage::Action` logs `tif`/`tifBars` (only a non-default time in force, as the web's `tifFields`) and `{"k":"oco","a","b"}`, and replays an action with a `tif` through `tl_place_order_tif` / `tl_place_option_order_tif`, one without through the pre-v18 calls, like the web worker. Combo tags carry `roll`/`adjust`.
- **Parity.** New fixtures `orderRules.json` and `heatmap.json` (with `portfolio.ts`) and new cases in `strategies.json` and `combos.json` (`scripts/web_fixtures_wave1.mjs`); the fixture games now carry `premiumTick`. `trailTrigger` is ported on 128-bit fixed point and matches the web's BigInt mirror.
- **Link mark.** The bundled fonts have no chain glyph, so the desktop draws the OCO link as `OCO↔#3` (panel and chart); `link_mark` itself stays the web's `⛓#3` for the parity fixture.
- **Portfolio refresh.** The holdings what-if (with the heatmap grid of the holdings) is asked on every state, as the web does; the builder's heatmap only while it is opened.
- **Academy builder.** The run-less lesson builder offers only the seven one-expiry templates (`kOneExpiryTemplates`): the web's `LessonBuilder.svelte` lists all eleven, and a calendar or diagonal there builds no legs (no second expiry). Flagged for the web.
- **Colorblind palette.** The drawn charts, payoffs and heatmaps now follow the settings' colorblind palette (gains blue, losses orange), as the web's `--up`/`--down` do.
- **Captures.** macOS only (MoltenVK); the Linux lavapipe captures were not run here (docker is installed, but a Linux desktop build does not fit the disk budget of this task) and are left to CI.

### As built: wave 1 cross-check and playtest (W1-X)

- **Same numbers (W1-X1).** m16 and m17 at seed 3, calibration questions and plans skipped, played with the same inputs on the web (Playwright, PT-BR 1440) and the desktop (`tradelab_desktop_playtest_tests -tc="*X1*"`, PT-BR 1280x800):
  - m16: an iron condor of 10 contracts with every leg a limit at the default (passive mid: buys 0,51 and 0,48, sells 1,27 and 1,17) and IOC. Dry run: credit 14,50 cr = (1,27 + 1,17 − 0,51 − 0,48) × 10, fee 1,21, spread margin 25,00 = 2,5 × 10, breakevens 98,55 · 103,95 (100 − 1,45, 102,5 + 1,45), maximum loss 10,50 = 25,00 − 14,50. The open at bar 61 reached no limit: all four legs cancelled `time_in_force` (combo not executed). Then an OCO pair of 10-contract stops at the close ± 0,5% (101,08 / 100,08): the sell filled at bar 62 at 100,04 (fee 0,50 = 10 × 100,04 × 0,05%) and the buy was cancelled `oco_sibling_filled`. Portfolio: delta −10,0000, −1.004,10 cr, −1.005 bp, next funding +0,10, "Hedge delta" a buy of 10,0. Debrief −185,59 cr. Digest `57469bb1…`, events `885cc32e…` on both.
  - m17: a call calendar of 5 contracts at strike 105, near bar 80, far bar 155 (the last listed). Debit 9,95 = (3,24 − 1,25) × 5, fee 0,31, margin 82,06 (the short's own margin: m17 declares no calendar margin), at the first expiry breakevens 102,68 · 107,55, maximum profit 5,31, maximum loss 9,95; heatmap cells 0, 10 and 50: −9,67 (88,75, 0 bars), −0,74 (104,41, 0 bars), −2,87 (101,28, 24 bars). Filled at bar 61 (3,29 / 1,30). Rolled to bar 105: buy back 1,24, sell 1,92, net +3,40, maximum loss after 6,55 (16,45 paid for the long − 6,50 received for the short + 6,20 to buy it back − 9,60 for the new short); filled at 1,25 / 1,94. Hedge: a sell of 0,1 perpetual; then the portfolio: delta −0,0468 units, −4,90 cr, gamma of a 1% move 0,06 (½ × 5 × (0,0692 − 0,0467) × 104,7² × 1%²), vega +0,07, theta +0,04. Debrief +9,23 cr. Digest `02da668c…`, events `5b009338…` on both.
- **Fixed, each with a regression test (W1-X2):**
  - a roll could target an expiry the combo already held: on m17 the default roll of a calendar (near 80, far 105) bought back the short and wrote the far series against the long, closing the calendar. `rollTargets` now leaves out held expiries on both clients ("Every later expiry already holds a leg of this structure" when none is left);
  - the web sized "Add hedge" from the holdings' delta of the last previewed change (a step old after a roll: a sell of 0,2 where the portfolio proposed 0,1); it asks for this step's holdings. The desktop waits for this state's what-if before sizing, and clears the dry run and the what-ifs on a new state;
  - the builder called a two-expiry structure's margin "a covered sale pays only the pair's maximum loss" on missions without the calendar margin (`marginLabelKey`);
  - limit legs: the dry run showed only the quote (now "limit 0,51 (quote 0,53)"), and the legging note and the sent notice promised execution at the next open (`builder.leggingLimit`, `comboSentKey`);
  - the desktop logged a skipped plan with confidence 50 (the web 0) and planned risks with `%.2f` (2,625 → 2,62, the web's `toFixed` 2,63): the engine hashes plans, so the same inputs ended on different state digests. Fills named the side in English in a PT-BR feed;
  - the chart's best-close text sat under the ticket's ENTRY? label; the trailing line's label now names the best close and the marker has no text. The desktop measured right-aligned scene texts with another font than the one drawing them, so long labels ran under the price axis;
  - the web's Academy builder listed the four two-expiry templates, which build no legs on a single-expiry lesson market.
  - desktop layout: a live combo's legs were cut by the panel at 1280x800 (the row now wraps them above its buttons), and the protect tab "▸ Trailing •" was cut; the m07 playtest still clicked the percent switch without opening the trailing tab.
- **Left open.** A planned trailing stop does not count as the ticket's planned risk (the ticket says "No stop" until it is placed); counting the first trigger would change the plan's logged risk and is a product decision. The gamma of a 1% move is a magnitude (the engine's D4 risk block), so the total does not tell long from short gamma; the rows do. W1-D5's Linux captures and W1-W8's end-to-end risk-limit warning wait as before.

### As built: wave 2 engine and web (W2-E, W2-W)

Where the build departs from or adds to D11–D14 and D20:

- **Generator (D11).** No G5: every market uses v5 capabilities. Departures from the D11 table, found while tuning the bands:
  - m18's chain has no anchors (a Level anchor made the hedged result bimodal: the band-hedged standard deviation exceeded the unhedged one); chop `iv_mult` 0.75, quiet 1.25, visits of 70–140 bars so most seeds sit in one regime.
  - m19's unwind is a G1 state (crowded first, then an unwind of negative funding, upward drift and squeezes) rather than a drawn event.
  - m20's IV ramp is two segments (`iv_mult` 1.4, then 1.8, both with `vol_decay` 0.93) and the event's `iv_mult` 0.6 over a post-release segment of 0.8.
  - m24's next leg and m25's reversal are one-visit G1 chains (70/30): a random-direction segment cannot be tilted against a rally, because segment drift also moves the fair value.
  - m23's denial jumps on 3 bars only, so following the headline stays under the 72% realism band.
- **Bands (D12), as tested in `test_mission_learnability_d.cpp`**, with the reference players' choices recorded (no band was widened):
  - m18: the band hedger rehedges when |delta| exceeds 40% of the contracts; ratio = realized volatility over the hold against the call's IV at entry. Measured: ratio ≥ 1.2 in 24 seeds, band-hedged wins 20 (83%); ratio ≤ 0.85 in 30, loses in all 30; standard deviation 160 against 301; every-bar fees 2.0x the band's.
  - m19: hedged carry of 30 units on the last expiry held to the end; "flip seeds" are those whose reveal holds the unwind state. No unwind: 27/27 positive; unwind: 21/33 negative, liquidated at 5x in 11/33 (33%), at 2x in 0; the unhedged short loses in 45%.
  - m20: "early" buys at the announcement on the **last** expiry (more vega, less theta; on the first expiry theta ate the IV bid) and sells the bar before; "holding through" sells 3 bars after the release; the "move" is |close 3 bars after − close the bar before| against the late straddle's ask. IV crushed in 60/60; early-sell beats early-hold in 51; big moves 20/23 win, others 30/37 lose; early-sell loses in 20%.
  - m21: "loss at the near expiry" is the equity at the near expiry step, bound by the debit plus the settlement fee and the far long's bid below its mid. Near the strike 21/30 win (70%, at the band); beyond three steps 14/14 lose; no margin call at 20 contracts.
  - m22: the managed plan trails with a stop moved to max(breakeven, best close − 2 ATR); "trend-far seeds" are those where the 2R target is reached before the stop (24 seeds; all-out better in 21). Managed win rate 50% against 38%; standard deviation of R 1.10 against 1.47.
  - m23: the reference setup is a close above the 20-bar high; the revenge player re-enters long at once at twice the size; the cooled player waits 3 bars, then the next close beyond the 20-bar range, normal size; both close 30 bars later. Cooled better in 43/60 (72%).
  - m24: planned = the first close 1 ATR above the 20-bar high with the stop at the 20-bar low; chase = the first close after 4 rising closes more than 2 ATR above SMA(20), stop 2 ATR; both held 50 bars. Planned better in 38/58 (65.5%, at the band); the chase wins in 43%.
  - m25: the add is twice the quantity, once the close is 0.75 ATR below the entry ("worst seed ≥ 2x worse" needs more than an equal add: an equal add to the same stop risks at most 1.5x). Single ≥ averaging in 46 (77%); averaging better in 14 (23%); worst seed −279 against −116.
  - Buy-and-hold is positive in 40–65% of each mission's seeds; the realism bands hold over the 25 missions.
- **Goldens.** One each at seed 2026, except m25 at 2027 (the first seed whose dip reaches 1 ATR below the entry, so the golden shows `averaging_down`).
- **Scoring v14 (D13).**
  - Perpetual entries decided while option contracts are held are hedges (m18, m19) and are left out of the three bias rules; without it every rehedge of a straddle read as averaging down.
  - `revenge_trade` compares planned risks when both trades have a plan and quantities otherwise, and fires for any losing close (a stop, a manual close or a target), as D13 states.
  - `averaging_down`'s limit is the first entry's planned risk, else the risk before the add; the stop is the add's bracket stop, else the nearest reduce-only stop working at its decision.
  - `risk_limit_exceeded` rebuilds the holdings and wallet at each close from the events (no new state, digest or checkpoint field) and values them with the D4 risk block; a test compares it with the live risk block at every close.
  - Episodes match the perpetual trade rows one to one; MFE/MAE are measured from the first entry for the largest size held, only with a planned risk.
  - Comparison names with explicit JSON names: `hedged_every_4`, `hedged_carry_5x`, `hedged_carry_2x`, `all_out_2r`. m20's rows use their own timing anchors (the announcement, one bar before the release) whatever the basis; m22's rows are played by their own trade rule and always on the reference basis.
- **Web.**
  - The calendar spread's tutorial art is `calendarSpread` (`calendar` is the economic calendar of m09, m11, m12). Psychology cards use `cooldown`, `plan` and `risk` (there is no `journal` art).
  - The debrief reads the m22–m25 objectives from the episodes and bias findings (`objectiveChecks.ts`); m18–m21's objectives are not judged there.
  - Two art labels fixed on the way (both on origin/main): the straddle's "strike" sat on "breakeven" in PT-BR, and the trailing art's TRAIL sat on a candle.
  - The API keeps both campaign orders; a leg fits either, and a campaign is complete when its legs cover one order they all fit.

### As built: wave 2 desktop (W2-D)

- **Content and progress.** `content.json` carries each stage's `branch` and `campaign` flags and `campaignOrderV3`; `strings.csv` gains the evidence wording's variants (`findingVariant.*`) and limit names (`findingLimit.*`). A `CampaignRun` read without `order` plays `campaignOrderV3`; a new run writes its snapshot. The SQLite progress row is version 4: the practice keys are read with the web's validators (damaged entries dropped, the newest 30 drill sessions, 90 days and 200 guesses kept) and written back as JSON for wave 3.
- **Parity over real debriefs.** The web helpers of the v14 debrief (`objectiveChecks.ts`, `describeFindings`, the episodes panel's cells) run over the m18–m25 goldens' engine debriefs, replayed by the desktop (`apps/desktop/tests/fixtures/engine/debriefs-w2.json`, checked against the live engine on every test run), and over variants of their findings that reach every wording branch (`debriefV14.json`).
- **Debrief layout.** A table cell cannot wrap, so the web's "see bar N" buttons become one list of moments (findings, objective readings, episodes) under the findings; picking one marks its bar on the run chart (a shaded column and ▼) and widens the view when the bar is before it. The comparison table keeps a short head of each structure name (before its first " (", ": " or ", "), numbered when two heads match (m17, m18, m20), with the full rule listed under the table; the long m18–m22 names were cut and hid the [best] tag.
- **Tutorial art.** The seven new drawings and the eight earlier ones the m18–m25 cards reuse are display lists; only `book` (m18's fourth card, whose "capacity" label runs past the web's frame) is still not drawn. The straddle's "−premium" label sat on the payoff's right arm on both clients and moved to x=90 (desktop regression test; the web's label-crossing e2e covers m13–m17's first cards only).
- **References (W2-D4, open).** `tutorial-scalp` and `curriculum-branches` were captured on macOS (MoltenVK); one set serves both platforms, so the task stays open until the Linux (lavapipe) job compares them.
- **Curriculum map.** Branch rows are marked ↳ and named "Branch of <stage>"; the path keeps its numbering (1–14); the elective row's status reads "LOCKED · ELECTIVE" and its content ends with the elective note.

### As built: wave 2 cross-check and playtest (W2-X)

- **Same numbers (W2-X1).** m18–m25 at seed 3, calibration questions skipped, tutorials read through (desktop) or skipped (web), played with the same inputs on the web (`apps/web/e2e/cross-check-w2.spec.ts`, PT-BR 1440) and the desktop (`tradelab_desktop_playtest_tests -tc="*X1*"`, PT-BR 1280x800). Both tests assert the final state digest, so a drift on either client fails CI. Every number the desktop's debrief shows appears in the web's (the web also prints the chart legend of the last bar).
  - m18: a straddle of 10 contracts at strike 100 (call 3,40, put 2,20, fees 0,30 each), 8 bars, then the portfolio's hedge: delta 1,3926 units (0,570 − 0,430 per contract × 10; gamma of a 1% move 0,76 = ½ × 20 × 0,0750 × 100,78² × 1%²), a sell of 1,3 (rounded down to the 0,1 lot). Debrief −43,87 cr = options −44,90 (10 × (1,11 − 3,40) + 10 × (0 − 2,20)) + perpetual +1,52 + funding +0,19 − fees 0,67. Digest `3e7b8802…`.
  - m19: a call bought and a put written at 102,50 on the last expiry and 10 perpetual sold at 102,93. The put settled at 13,86 (−138,60 cr); the short collected +10,09 cr of funding (+0,69 cr on bar 64 ≈ 10 × 98,6 × 0,07%). Debrief +6,75 cr, the hedged carry's row. Digest `2a4dff63…`.
  - m20: a straddle at 100 on the first expiry (2,40 + 1,27 = 3,67, breakevens 96,33 · 103,67, an implied move of 3,7%) held to settlement at 94,38: +18,89 cr. The comparison's rows enter at 67 and 101. Digest `c2e382a6…`.
  - m21: a call calendar at 100 (far 2,13 bought, near 1,55 written: debit 5,80 + fees 0,61 = 6,41, the maximum loss at the near expiry), both expired worthless: −6,41 cr. Digest `ed64a29c…`.
  - m22: 10 bought with a 2×ATR stop and a plan, 5 more 5 bars later with their own plan; both stops filled on bar 70 (99,02 after a gap through 99,56, and 99,01): −30,54 cr, averaging down named (risk to the stop 21,00 → 28,60 cr against the first plan's 17,01). Digest `1dbad258…`.
  - m23: 10 bought, closed by hand the next bar at a loss of 8,31 cr (10 × (100,41 − 101,14) − 1,01 of fees), then 20 bought at once with no plan: revenge trade named at bar 62 (2,00× the size), the objective missed. Digest `6ae787a6…`.
  - m24: 38 bars waited, then 20 bought with no plan or stop: FOMO entry named (9 rising closes, 6,79 ATR above the 20-bar average). Digest `1e27c1ae…`.
  - m25: 10 bought, 20 and 20 more 12 bars apart: the price rose, so the adds averaged up (average 103,25 = (1.011,40 + 2.067,20 + 2.083,80) / 50) and no averaging down was named; funding 7,96 cr. Digest `41a46c9c…`.
- **Fixed, each with a regression test (W2-X2):**
  - scoring v14 (unreleased, so no version bump): `wrote_options` read m19's hedged carry and m21's calendar, the structures those missions teach, as naked writing and withheld the good-process credit. A long of the same kind on a later expiry now covers a short (each long unit once), and short perpetual units cover a short put (a covered put, mirroring the covered call). The rule only removes findings; the scores of every earlier golden stay identical to v13 (`scores_v13.json`). Engine tests in `test_scoring.cpp`, a Python test on the m19 and m21 goldens;
  - m22's objective "a partial exit or a trailing stop" read two stops filling on the closing bar as a partial exit; a partial exit is now an exit on an earlier bar, or any exit of a position still open at the end (`objectiveChecks.ts`, `objective_checks.cpp`, parity fixture case);
  - the episodes panel showed the adverse excursion, which the engine gives as a size, as "+2,03R"; it now reads as a loss (`episodeText.ts`, `report_texts.cpp`);
  - the comparison's intro said "built at bar 60" above m20's rows entering at 67 and 101, and "you traded no options … contracts per leg" on m22, a perpetual mission. Rows that enter on other bars than the basis get their own intro (`comparisonBasisKey`, parity fixture);
  - m23's objective and the revenge text said "stop" while the rule reads any losing close (a manual close at a loss read as a missed "after a stop" objective); both now name a losing close and the 1.5× size.
- **Playtest (W2-X2).** Both clients as a new player (the curriculum map with its branches, tutorials, briefings) and over the cross-check runs; the desktop's playtest plays each mission with a sensible and a reckless approach in both locales; web captures at 1024 and 1440 in PT-BR and en (tutorials of m18, m20, m23 and m25, the curriculum, the m20–m24 briefings and debriefs) were read. A returning player's pre-v4 campaign keeps its 17-mission order on both clients (unit tests). No title, briefing, card or lesson names the bias a psychology mission tempts; m23's objective says what is judged, not what the market will do.
- **Left open.** The remaining risk after a partial exit was checked on the engine's episode tests and the Academy's "scale" check, not in a playtest: on seed 3 the m22 stops filled before any partial exit. m22's debrief can credit good process ("every trade had a plan and limited risk") beside an averaging-down finding; both are engine facts and the finding says why. The builder's margin label keeps the mission's spread rule on a structure with no short (as on m13–m17).

### As built: wave 3 engine and web (W3-E, W3-W)

Where the build departs from or adds to D15–D20:

- **Coach review (D17).** The C ABI run handle keeps no command list and a resumed run comes from a checkpoint that carries none, so `tl_coach_review` cannot replay the player's commands. It walks the finished run's recorded state decision by decision instead and reads, at step s, only what had happened by the close of s: events up to s, orders placed by s with their working state and trigger at s (from their own order updates, never their final fields), plans, closed trades, bars and indicators up to s, releases announced by s, and the holdings rebuilt from the events (the shadow account of `risk_limit_exceeded`). The no-look-ahead test replaces the bars after a step (three cuts on m11, m23 and m25) and compares every moment up to it. The 9/3 caps are applied after: a later, more severe warning can push out an earlier one, but no moment's existence or evidence depends on a later bar.
  - Rules are judged at the decision, so the bias codes read the order placed at s with its fill estimated at the close (market) or its limit price, where the debrief's findings read the fill. A revenge or FOMO order that never filled is still a coach moment.
  - `entry_without_stop` is judged at the close of the bar the position opened or grew (a stop placed then counts, as the web's planned trailing stop is placed after the fill), once per position; option hedges are left out like the bias rules.
  - `risk_above_budget` uses the score's oversized rule (2% of the initial capital); `held_into_release_unhedged` is judged at the decision before the release bar (a market close sent then counts as protection; vega only where the mission declares a vega limit); `stayed_flat_before_release` needs a fill earlier in the run (no credit for never trading); `cut_at_invalidation` is a losing perpetual trade with `planRespected`; `partial_at_target` a partial exit at 1R or more on the position.
  - Risk impact is a fixed order of codes (limits, averaging down, revenge, release, no stop, budget, FOMO, structure, stop moved), then step. Within a step an exit during the bar comes before the decisions at its close.
  - The shared v14 helpers moved to `src/bias_rules.hpp` (every v14 finding byte-identical). The lesson and glossary ids are pinned in an engine test and checked against the web content.
  - The coach fixtures are kind `coach` (m22 and m23 runs, listed in `index.json`), replayed by the native, WASM and Python readers; the desktop's golden reader still requires kind `run`, `reveal` or `whatif` and must learn `coach` in W3-D1.
- **Round markets (D19).** After the 25 missions in `tl_missions`, with `practice: "round"` (absent for missions, so every earlier field is unchanged). Two tuning steps: r02's false breaks needed jumps of drawn direction (a range with symmetric zero-mean jumps had a kurtosis of 0.6, under the band's 1.0), and r04's event an impact of 0.03 with the reaction aligned in 60% (the gap band and the advisor's 45% honesty band). r04's advisor hit rate is 45.2%, at the band.
- **Drills (D15).** Greeks are stated as givens in the drill texts (as in the lessons' `hedge` and `scalp` checks), so no drill calls the engine at run time and the "skip a template the pinned build cannot compute" rule never applies; `test_drill_numbers.py` checks the option kinds (payoff, breakeven) and the trailing triggers of the parity fixture's items against the engine. Parameters are drawn in the file's key order; a sum or difference of two parameters draws nothing. An explanation's only literal digits are 0 and 1 (a unit test). An unseen skill counts as box 1, so its first all-right session schedules box 2; a skipped item leaves its skill's schedule as it is. `drillHistory` sessions gain `skills` (first-try results per skill), the input of the weakest-skill choice (unseen skills count 50%).
- **Daily challenge (D16).** The pool is the mission list without m12 and the round markets (24, a unit test against the WASM catalog). The run earns no advisor points (it starts without the advisor) and shows a terminal banner and a debrief tag. `DAILY_POOL_V1` with 2026-11-09 = day 312 gives m01.
- **Guess rounds (D19).** A round runs in its own engine worker, so the session's run is never touched. The market, seed and step come from mulberry32 seeded by FNV-1a of `guess-v1:<date>:<rounds played>`. Plan distance and reward need the invalidation on the losing side; the target must be on the winning side. `guessHistory` rounds gain `higher` (the calibration table's outcome); the score is kept in hundredths.
- **Mistake replay (D18).** A finding is offered "Retry from here" when its tone is warn and its step is a decision step (≥ the warm-up): today `oversized`, `leverage_vs_stop` and the v14 bias and limit findings. Repeated also counts a finding naming the same mistake (`no_stop` → `entry_without_stop`, `oversized` → `risk_above_budget`). The original's result comes from its run summary.
- **Parity fixtures.** `drills.json` (FNV vectors, the first 100 outputs of seeds 1, 42 and 3735928559, four sessions with their items and texts, 20 items of every template, `parseAnswer`), `daily.json` (400 dates from 2025-12-20), `guess.json` (picks, 30 scored commitments, first hits, a calibration table) and `coach.json` (every moment text in both locales, repeated/avoided, names), written by `apps/desktop/scripts/web_fixtures_wave3.mjs`. JS rounding (`Math.round`) is half up: the C++ ports must use floor(x + 0.5), not `std::round`.
- **Not done here (W3-D, W3-X).** The desktop screens and ports; the windowed captures; the cross-check of W3-X1.

### As built: wave 3 desktop (W3-D)

- **Engine adapter (W3-D1).** `MissionInfo` carries `practice`; the AppViewModel keeps the 25 missions for the menu, the campaign, the curriculum count and the daily pool, and the round markets apart. Two report-only analyses on the `EngineHost` worker: `Coach` (`tl_coach_review` of the run in play) and `Round`, a fresh run of a round market advanced to a step and answered with its `tl_state`, so a guess round never touches the run in play (the web's second worker). The golden reader replays kind `coach` as a run and compares its review field by field; a coach review is refused while the run is active.
- **Ports (W3-D2, W3-D3).** `viewmodel/practice/`: `prng`, `drills`, `daily`, `guess_round`, `coach_review`, checked by every case of `drills.json`, `daily.json`, `guess.json` and `coach.json`. The drill templates reach the desktop through `gen_i18n`: their structure in `content.json` (`drillSkills`, `drills`, each template's parameters as an ordered list, because the desktop's JSON reader keeps no key order) and their texts as `drill.<id>.prompt/explain/case.<k>/choice.<k>` rows. Answers use the lesson checks' arithmetic in the web's order and floor(x + 0.5) for `Math.round`. The practice keys of progress v4 are typed in the shell's progress (`model/practice_json`), a session's `skills` and a round's `higher` included; the drill clock setting is `drillTimer` in the display settings.
- **Screens.** Four views (`drills`, `daily`, `guess`, `review`), the menu's practice row, the debrief's "Review with the coach" button, daily tag, a "Retry from here" box (a combo of the warning findings at a decision step; the moments list keeps its findings, readings and episodes) and the mistake report, and the terminal's mistake and daily banners. The coach review is its own screen over the debrief (Esc closes it, the arrows move between moments); its chart is the run cut at the moment's bar (`state_at`: no later bar, order, position or liquidation line). The guess chart marks the decision bar on the reveal.
- **Daily link without a schema change.** The SQLite run row has no `daily` column (D20 keeps the schema). A daily start is known to the session; a run resumed after a relaunch is recognised as a daily attempt when its mission and seed are the challenge of the local day it was created. A daily run earns no advisor points on the desktop either.
- **Mistake retry.** "Retry from here" is the host's assisted rewind (`rewindPrefix`) with the link `{"from","kind":"mistake","step","code"}`; the retry's debrief asks the engine for its coach moments, then reports repeated or avoided and both results (the original's from its run summary).
- **Checked by hand (desktop playtest, 2026-11-09).** The first drill of the day is `greeks-delta-units` with 3 × 0,8 = 2,40 (the web's); the daily challenge is m01 with seed 3.582.066.521 (the web's fixture); the first guess round is r01-trend at bar 113 (close 90,58, ATR 0,95): a long at 70% with the invalidation at 89,15 and the target at 93,44, the close 12 bars later 90,11, scores 1 + 40 + 10 = 51, the web's. Of the ten playtest rounds, four were recomputed by hand: 95% long not higher (0 + 10 + 10: the invalidation 0,28 away is under 0,5 ATR and the 0,28 reward under 1,5R), an 80% short (calibration 0, consistency 0, only the side's 10), a 45% short (29,75 + 25 + 10: the reward 3,19 falls short of 1,5 × 2,13 = 3,195) and an 80% no trade (0). The coach's first moment of m24 chased on seed 3 is the FOMO entry at bar 98 (9 rising closes, 6,79 ATR above the 20-bar average), its chart ending at bar 98.
- **Fixed while playing.** The reveal formatted levels typed with a decimal comma as a dash (regression check in the guess flow test); the lesson links lost the emoji the button font cannot draw.
- **References.** `drills-wrong`, `guess-commit` and `review-moment` (the device clock fixed to 2026-11-09) and the menu with its practice row, captured on macOS; W3-D4 stays open until the Linux job compares them.

## Risks / Trade-offs

- **Policy v18 changes every frozen state digest.** This is mitigated by an event-digest equality test, the documented history and the policy cache. Replays of v17 runs depend on the cache being built in CI (`TRADELAB_REQUIRE_POLICY_CACHE=1`).
- **Legging risk grows with limit legs.** A `gtc` limit combo can sit half-filled for many bars. The `ioc` default, the `working` state and its notice keep this visible. Playtests decide whether `gtc` should stay offered for combos.
- **Horizon analytics are model-dependent.** Far legs are valued at today's IV level, labeled as such. The calendar's real outcome depends on the IV path, which m21 teaches.
- **Psychology rules can misfire.** A planned re-entry after a loss is not revenge. Every rule requires a missing plan, a larger size or a missing stop, and the findings are report-only and shown with evidence, so a player can see why.
- **Campaign length** grows to 21. Advanced options are elective for that reason; if playtests still show fatigue, psychology can become elective without changing the stage model.
- **Content volume.** Six lessons, about 24 glossary terms, 26 tutorial cards and 11 drill skills. The checks mechanism catches wrong numbers, not wrong explanations, and lesson texts still need the human review that rubrics await.
- **Disk and CI time.** More goldens, captures and missions. Only one desktop build directory at a time; captures and test results are cleaned at the end of each task.

## Migration

- Engine policy 17 → 18, checkpoint 5 → 6. Clients pin the engine per run. A checkpoint of format 5 is refused, and the client falls back to a full replay on the run's pinned build.
- Web progress 3 → 4 (D20). Desktop progress row v4.
- Active campaign runs keep their 17-mission order (snapshot). New campaign runs use the 21-mission order.
- Players who passed Risk, Events or Strategies find the new branch stages open, and nothing they passed becomes locked.
- Older action logs have no `tif` or `oco` entries and replay unchanged under their own policy.

## Open Questions

- Should `gtc` stay available for limit combos after playtests, or should combos allow only `ioc` and `bars`?
- Should psychology become elective too if the 21-mission campaign tests long?
- Should the daily pool include the elective missions only once the player has opened their stage? (This change: the pool is the same for everyone, so no.)
- Should drills include glossary flashcards later? (Left open, as in add-academy-and-strategies.)
