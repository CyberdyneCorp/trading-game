# Proposal: add-academy-and-strategies

## Why

Missions teach by doing, but the concepts behind them are explained only in short tutorial cards and the debrief. Only one Academy lesson exists (Foundations), so a player who does not know what a liquidation price, a breakeven or theta is meets them for the first time inside a mission. Option positions also stop at single legs: the game cannot show spreads, covered calls, protective puts or condors, which are how most people actually use options. This change adds the missing teaching layer (lessons for every stage, a glossary, a guided first trade, an options what-if lab) and a strategy stage with a multi-leg builder and five missions.

## What Changes

- **Academy lessons for every curriculum stage.** About 15 new lessons in the existing runner format (concept, demonstration, decision, feedback, transfer), each with interactive widgets and questions with feedback:
  - Structure; Orders (market, limit and stop, spread, slippage, capacity);
  - Futures I–III (long/short P&L; margin, leverage, liquidation, mark against last; funding);
  - Risk (sizing from the stop and R multiples; expectancy, drawdown and risk of ruin);
  - Options I (call and put, premium, intrinsic and time value, payoff, breakeven);
  - Options II (delta, gamma, theta and vega; implied volatility, time decay, IV crush);
  - Options III (writing and margin, settlement, covered call, protective put, spreads);
  - Strategies; Patterns & Indicators; Events; Evaluation (planning and psychology: thesis, invalidation, journal, FOMO, revenge trading, overconfidence and calibration).

  Completion and retakes are tracked on the device. A light spaced-review prompt brings a lesson's questions back after 1, 3, 7 and 21 days.
- **One content source.** Lesson, glossary and guided-tour content moves to JSON data files with PT-BR and en text side by side (`apps/web/src/lib/content/`). The web imports them. The desktop generates its strings and content from them, as it does today for `academy.ts`, with the same drift check. The Foundations lesson moves there first, unchanged.
- **Options what-if lab.** Sliders for price, time to expiry and implied volatility show the value today, the P&L at expiry and each Greek, for a position of one or more legs. Every number comes from the engine's pricing kernel through a new report-only C ABI call, `tl_option_whatif`. It changes no run and no digest. Rate is not a slider: the game's model is Black-76 at a zero rate, and the lab says so.
- **Multi-leg strategy builder in the option ticket.**
  - Templates: bull call spread, bear put spread, covered call, protective put, straddle, strangle and iron condor, plus free legs on one expiry.
  - Before sending, it shows the combined payoff diagram, breakevens, maximum profit and loss, net premium, fees, margin and net Greeks. These come from the engine (`tl_option_whatif` and a new dry-run call, `tl_combo_preview`).
  - Legs are sent as ordinary orders at one decision step: buys first, then sells. If any leg is refused, the legs already accepted are cancelled before anything can execute.
  - A leg cancelled at execution (no bid, margin moved) leaves a combo marked incomplete. The client says what is open and offers to close it. It never acts by itself.
  - There is no engine combo order. Design D3 explains why.
- **Defined-risk margin, per mission.** A new option-spec flag, `spread_margin`, margins a short option paired with a long of the same kind and expiry at the most the pair can lose at expiry (0 for a debit spread, the width for a credit spread; an iron condor at its larger side). Only m13–m17 declare it, so every existing mission, pack and golden is unchanged.
- **Guided first trade.** The first time a player opens the terminal in a campaign, coach marks point at:
  - the chart, the ticket and the stop;
  - the risk line;
  - advance;
  - then the debrief.

  The tour is skippable, can be replayed from the help overlay and the briefing, and is keyboard and screen-reader accessible.
- **Glossary.** About 70 shared terms. A glossary screen (searchable, per locale), in-place definitions on hover and focus for terms marked in the UI, and links from lessons and tutorial cards.
- **Strategies stage and five missions (m13–m17).** A new stage after Options III, with missions designed and tested like design D8 of add-scenarios-and-gameplay:
  - m13: bull call spread against a naked call;
  - m14: covered call (income against capped upside);
  - m15: protective put (insurance cost against a crash);
  - m16: iron condor against a short strangle in a range, then a breakout (defined against undefined risk);
  - m17: managing before expiry (rolling against holding; theta and gamma near expiry).

  Each mission gets tutorial cards, lesson questions (shipped `reviewed: false`), a seeded reveal, a report-only strategy comparison in the debrief, learnability bands and its own golden. The campaign grows to 17 missions.

## Capabilities

### New Capabilities
- `academy`: lesson format and widgets, content source, completion, retakes, spaced review, correctness checks of lesson numbers.
- `options-whatif`: the what-if lab (inputs, outputs, engine source of numbers), in the Academy and in the terminal.
- `strategy-builder`: templates and free legs, the pre-trade analysis, submission and partial-failure handling, combos in the journal and debrief.
- `guided-first-trade`: coach marks on the real terminal, when they are shown, skip, replay, accessibility.
- `glossary`: shared terms, glossary screen, in-place definitions.

### Modified Capabilities
These changes are not archived yet (`openspec/specs/` is empty), so their changes are written as ADDED requirements in delta files named after the capability. This follows add-advisor-intel-tutorials. Where an added requirement replaces one of theirs, it says so.
- `options` (add-deterministic-engine): the report-only what-if valuation and combo dry run; vertical spread margin when a mission declares it.
- `curriculum` (add-scenarios-and-gameplay): a Strategies stage and the 17-mission graph, which replace "Phase-2 mission graph"; Academy lessons for every stage that never gate the campaign; recommended lessons open the Academy.
- `market-generator` (add-scenarios-and-gameplay): learnability bands for m13–m17.
- `gameplay-loop` (add-scenarios-and-gameplay): the strategy comparison in the debrief of m13–m17.
- `mission-tutorial` (add-advisor-intel-tutorials): strategy tutorials state the mission's margin rule; cards link glossary terms.
- `desktop-client` (add-native-clients): the desktop ships the same Academy, lab, builder, tour, glossary and missions.

## Affected

- **Clients.** Web (`apps/web`) and desktop (`apps/desktop`, Linux and macOS) in this change. iPad gets the engine behavior through the bindings when it ships.
- **Engine.**
  - Execution policy stays at **v17**. `spread_margin` acts only where a mission declares it, the same pattern as design D8.1, so every existing golden stays byte-identical.
  - Generator stays at **v5**: m13–m17 use existing capabilities only.
  - Scoring goes **v12 → v13**: the report-only `strategyComparison` debrief block. It is not a score input and changes no digest.
  - Checkpoint format stays at **5**.
  - C ABI stays at **1**: `tl_option_whatif` and `tl_combo_preview` are additions, which `kAbiVersion` does not count. Clients feature-detect them on an older pinned build.
  - New goldens: m13–m17 runs and `whatif` fixtures (a new fixture kind, replayed by every binding).
- **Bindings.** WASM worker (new requests `whatif` and `comboPreview`), Python (`option_whatif`, `Run.combo_preview`), desktop adapter.
- **Services.** `services/api` needs no change. It re-simulates the m13–m17 runs through the Python binding like any other mission, and an optional `combo` tag on logged option actions is ignored by replay.
- **Storage.** Web `meta.progress` gains `lessonReviews`, `coachDone` and `glossarySeen` (version 3, migrated). Desktop `prefs.json` gains the same keys. IndexedDB `DB_VERSION` and the SQLite schema are unchanged.

## Non-goals

- Login and sync (Cyberdyne Auth), Market4Me, deployment.
- The human review of lesson questions: every new rubric ships `reviewed: false`, so understanding stays not applicable for those missions until a person signs them off.
- An engine combo or all-or-nothing order, limit orders in the builder, and calendar or diagonal spreads (legs on different expiries).
- Margin offsets between the perpetual and options (a covered call's short call stays margined on its own), and portfolio margin.
- An interest-rate input: the pricing model stays Black-76 at a zero rate.
- Early exercise or assignment (options stay European and cash-settled).
- LLM-written or adaptive lesson content; spaced review that is synced across devices.
- Changing m01–m12, their goldens or their bands.
