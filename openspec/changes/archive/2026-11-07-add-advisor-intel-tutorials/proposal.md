# Proposal: add-advisor-intel-tutorials

## Why

Players start missions without guidance on what the mission teaches or which controls matter, and they have no structured way to see how an experienced trader would read the market in front of them. Progress is also invisible: finishing missions and trading with discipline earn nothing beyond a score. This change adds onboarding per mission, a coach players earn through good process, and legal information perks that reward completed missions.

## What Changes

- **Mission tutorials**: 3–5 short illustrated cards per mission, covering the concept, what to watch and which controls to use. They are shown automatically the first time a mission starts, can be skipped, and can be replayed from the briefing.
- **Advisor (honest coach)**: a new engine function reads only what the player can see at the current step:
  - regime (trend or range);
  - indicators: RSI, Bollinger, MACD, ADX and SMA;
  - the latest detected candle pattern and its context;
  - released news, including whether it is already priced in;
  - funding crowding.

  It returns long, short or wait, with a confidence (capped well below certainty), a suggested stop and size, and the reasons behind it. Asking costs one advisor point. Each request is a logged engine command, so replay and server re-simulation reproduce it. The debrief shows every piece of advice and how it turned out.
- **Advisor points**: the engine reports points earned in a run:
  - +1 for each closed trade that had a plan and a protective stop;
  - +1 more if that trade made money;
  - +2 for finishing the mission;
  - capped per mission.

  The client keeps a local balance that starts at 3 and is capped. Points are never purchasable.
- **Intel perks** (legal, unlocked by the number of distinct missions completed):
  - **Economic calendar** (1 mission): shows the bar of upcoming scheduled releases, without their content.
  - **Fast wire I** (2 missions): headlines visible 1 bar before their public release.
  - **Fast wire II** (4 missions): headlines visible 2 bars before their public release.

  Perks are toggled on the briefing. The debrief and the run record show which perks were used.
- Engine: a new `RequestAdvice` command and `RunOptions` at start (advisor allowance, news lead, calendar). This bumps `kExecutionPolicyVersion` to 11. With default options, replays and digests are unchanged, so existing goldens still pass.

## Capabilities

### New Capabilities
- `mission-tutorial`: per-mission tutorial cards, when they are shown, skip and replay.
- `advisor`: advice inputs (visible data only), output, cost, honesty bounds, logging and debrief.
- `progression-rewards`: advisor point earning and balance, intel perk unlocks, toggles, labeling.

### Modified Capabilities
- `gameplay-loop` (from `add-scenarios-and-gameplay`): the debrief also lists advisor use and perks. Covered by the new capabilities rather than a delta, because that change is not archived yet.

## Affected

- Clients: web (`apps/web`) now; desktop and iPad get the same engine behavior through the bindings when they ship.
- Engine: execution policy v10 → v11 (new command, start options). The generator is unchanged (v2), and default runs replay identically.
- Bindings: WASM and Python (start options, new command); the backend re-simulation accepts the options stored with a run.

## Non-goals

- Insider information or any framing that rewards illegal conduct; perks are modeled on legal products (economic calendars, low-latency news feeds).
- An advisor that uses hidden generator state (fair value, scripted segments, future bars or unreleased news beyond an active perk).
- Purchasing points, perks or advisor uses, and syncing points across devices (local only until accounts exist).
- Guided coach-mark overlays inside the terminal (cards only).
- LLM-generated advice: the advisor is deterministic engine code.
