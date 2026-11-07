# Design: add-run-sharing-and-journal-stats

## Context

**What exists.**
- Every client stores a run as a header plus an append-only action log. The web keeps it in IndexedDB (`runs` and `actions` stores), the desktop in SQLite. Resume replays the log through the engine. The stored digest only checks the replay.
- The action log has one language-neutral format, the web client's. It is documented in `bindings/python/tradelab_engine/replay.py` and used by the shared golden fixtures (`engine/tests/golden/`). The engine tests, the Python binding and the desktop replay those fixtures. Client ids are 1, 2, … for every action except `adv`.
- The engine has a strict JSON reader for untrusted input (`src/json_in.hpp`). It accepts integers only, rejects duplicate keys and bounds depth, length, string size and member count. It also writes RFC 8785 canonical JSON. Pack manifests and CSV import metadata already go through it.
- The debrief (engine) lists every trade with its R multiple, exit reason, planned risk and whether its plan was respected, plus finding codes with a tone. The web keeps a compact copy of a campaign mission's debrief on its campaign leg for the campaign report (`campaignReport.ts`). Other runs keep no debrief: a debrief exists only while the run is loaded.
- Assisted attempts are already excluded from progress, points and the campaign by a flag on the run record.

**The gaps.** A run cannot leave its device, and nothing aggregates debriefs across runs.

## Goals / Non-goals

Goals:
- One file format, written and read the same way by the web and the desktop.
- The engine decides whether a file is valid, so both clients accept and refuse the same files. Malformed or hostile input never crashes a client or reaches the engine's run functions unchecked.
- Imported runs cannot change the player's progress, by construction.
- Statistics computed from engine numbers only. Each statistic is framed by the lesson it measures, never as a trading signal.
- No change to execution, scoring or any golden digest.

Non-goals: see the proposal.

## Decisions

### D1. The run file: versioned JSON, the shared action format, no compression

A run file is UTF-8 JSON, written with the extension `.tlrun`. The reader also accepts `.json`. Version 1 has exactly these members; any other member, at any level, makes the file invalid:

| Member | Type | Meaning |
|---|---|---|
| `format` | `"tradelab-run"` | Identifies the document. |
| `version` | `1` | Format version. A reader refuses other values (`unsupported_version`). |
| `mission` | string, 1–64 of `[a-z0-9._-]` | Built-in mission id, or the pack's scenario id for a pack run. |
| `pack` | `null` or 64 lowercase hex | Digest of the catalog pack the run played; `null` for a built-in mission. |
| `seed` | integer 0..2^32−1 | Run seed. |
| `options` | object | Start options in the run-record shape: `adviceAllowance` (0..1000), `newsLead` (0..255), `calendar` (bool), optional `startingBalance` (decimal string > 0) and `leverageCap` (1..1000). |
| `engine` | object | `executionPolicy`, `abi` (integers) and `version` (string ≤ 32): the engine that recorded the run. |
| `labels` | object | `campaign` and `assisted` (bools): how the run was played. Informational: they never change verification, and an imported run never counts either way. |
| `actions` | array, ≤ 20,000 | The action log in the shared format (`replay.py`), with the optional `combo`/`strategy` tags and `trailRate`. |
| `journal` | array, ≤ 500 | Plan notes, or `[]` when the player did not include them (D3). Each has `step`, `side`, `thesis` (≤ 1,000 bytes), `invalidation`, `target`, `plannedRisk` (decimal strings or `null`), `confidence` (0..100), `skipped`, and optional `loseScenario` (≤ 1,000 bytes), `instrument` and `combo`. |
| `result` | object | `status` (`active`, `finished` or `bankrupt`), `digest` and `eventsDigest` (64 lowercase hex): the engine state after the last action. |

Numbers are integers only, and every price, quantity and amount is a decimal string of at most 12 integer and 8 fraction digits. That is the engine reader's subset, so a reader never has to handle floating point. A file is at most 1 MiB. A finished 300-bar run with a few hundred actions is about 30 KB.

**Rejected alternatives.**
- *gzip (or zstd) around the JSON.* Files are small. Compression would add a codec to the desktop, a decompression-bomb limit to the reader and an opaque file to the player. The browser's `CompressionStream` and the engine's zstd frame reader could do it later as a version 2 without changing the content.
- *A client-specific action format (the desktop's SQLite rows).* The shared format already exists and every client already replays it bit for bit in the golden tests.
- *Embedding a checksum or a signature.* Replaying to the recorded digest (D4) is a stronger check than a checksum. A signature would need a device identity, which D3 forbids, and would prove nothing about how the run was played.

### D2. The engine verifies a file: `tl_run_file_verify`

```c
const char* tl_run_file_verify(const char* file_json, const uint8_t* pack, uint32_t pack_len, const char* keys_json);
```

It is run-less and returns a static buffer. It never touches another run. It:

1. parses the text with the strict reader (1 MiB, depth 8, strings ≤ 2,048 bytes, ≤ 20,000 members per array or object);
2. checks the schema of D1 member by member, recording the path of the first bad member (for example `actions[12].qty`);
3. refuses a file whose `engine.executionPolicy` differs from the engine's (`policy_mismatch`, D4);
4. creates the run (`tl_run_create_ex` with the options, or `tl_run_create_pack` with `pack` and `keys_json` when the file names a pack);
5. applies every action with the same applier as the golden tests (client ids assigned the same way);
6. compares the final status, `digest` and `eventsDigest` with `result`.

Reply on success: `{"ok":true,"mission","seed","pack","status","step","digest","eventsDigest","actions","rejected","fileDigest","run","debrief"}`.
- `rejected` counts the commands the engine refused during the run. Refusals are part of a run, not an error.
- `run` is the canonical file (RFC 8785): what a client stores.
- `fileDigest` is the SHA-256 of that canonical text: the id of an imported run, so importing the same run twice keeps one copy.
- `debrief` is `tl_debrief` of the replayed run, or `null` while the run is active.

On failure: `{"ok":false,"reason","path","pack"}`. Reasons: `too_large`, `malformed` (not JSON under the reader's subset), `not_a_run_file`, `unsupported_version`, `invalid_field` (with `path`), `unknown_mission`, `policy_mismatch`, `pack_required` (with the digest the client must supply), `pack_mismatch`, `pack_invalid`, `invalid_options`, `digest_mismatch`.

The applier moves from `tests/golden.cpp` to `src/run_file.cpp`. The golden test now calls it, so the run files and the goldens replay through one implementation.

**Sealing (the exporter's call).** `tl_run_file_seal` takes the same arguments and runs the same checks and replay on a file whose `result` may be absent. It returns the accepted verdict, with the replay's status and digests written into `run`. A client builds the other members from its run record and action log and seals them. It never computes a result itself (the web's run record keeps only the state digest, not the events digest), so the web and the desktop write byte-identical canonical files for the same run. The client compares the sealed digest with the digest stored on its record and warns when they differ, which means a damaged local log.

**Rejected alternatives.**
- *Validate in each client.* Two validators in two languages would drift, and the desktop would need its own fuzzing. The CSV importer set the precedent: the engine checks, the clients translate codes.
- *A separate `tl_run_file_check` (parse only).* `verify` reports `pack_required` with the digest before any replay, which is all a client needs to ask for the pack.

### D3. Privacy: nothing that identifies the device or the player

A file holds no run id, no timestamps, no device key or pack signing seed, no account, no locale and no progress. Plan notes are free text the player wrote, so they are included only when the player ticks "include my plan notes" at export (off by default); otherwise `journal` is `[]`. The engine never reads the notes. Runs on private packs (CSV imports, signed by the device key) cannot be exported: the export control explains why. Catalog packs are public, and the file carries only their digest.

### D4. Integrity and older policies

A file verifies only when its replay reproduces `result` exactly. A digest that differs means a tampered or damaged file (`digest_mismatch`), and the file is refused, not flagged and kept. A file from another execution policy is refused with `policy_mismatch`, naming both versions, because a replay under different rules is expected to differ. The web's pinned older engine builds serve only the device's own unfinished runs. Loading an arbitrary historical engine for an imported file is the open question below.

**Rejected alternative.** *Import a mismatching file as "unverified" and show it anyway.* A viewer would then show positions and a debrief that the recorded run never had, and the label would be easy to miss. Refusing keeps "what you watch is what was played" true.

### D5. Imported runs live apart and never count

The web stores imported runs in a new `imports` IndexedDB store, keyed by `fileDigest` (database version 4). It holds the canonical file, the verified status, the mission, the pack digest and the import date. The desktop uses its own table. None of the progress readers (missions completed, perks, curriculum, campaign, advisor points, journal statistics) read that store, so an imported run cannot count by construction, not by a flag someone could forget to check. Removing an imported run deletes only its row.

### D6. The replay viewer drives the client's own engine

Watching a run rebuilds it in the client's engine worker from the canonical file: start with the seed and options, or the pack, then apply the actions. Nothing is logged or persisted, the run record and checkpoints are not touched, and every command path of the session is closed while viewing. "Next bar" applies the actions up to and including the next advance, one bar at a time: an `adv n` plays as n single advances. The engine already guarantees that n single steps equal one n-step batch (the desktop tests compare the digests). "Play" repeats it on a timer, "To the end" applies the rest, and the viewer then shows the debrief from the same engine. The debrief hides what belongs to the player's own run: points, rewind, another market, play again and the campaign. The viewer also plays the device's own runs from the journal and the statistics screen, through the same path. That is how the player can re-watch a finished run without importing it.

**Rejected alternative.** *A second worker for the viewer.* It would load the engine twice for no gain: verification is run-less (D2), and leaving the viewer resumes the player's run from storage exactly as the menu already does.

### D7. Journal statistics from stored per-run summaries

When a debrief loads for one of the player's own runs (not assisted, not viewed), the client stores a compact summary on the run record:
- the mission, stage, status, initial capital and net result;
- the score;
- every trade's net, R in basis points, risk as a share of initial capital, exit reason, plan respected, stop moved away and instrument;
- the debrief's finding codes with their tone.

The statistics screen aggregates these summaries. It never replays runs on open. Runs that finished before this change have no summary: the screen offers to compute them, one at a time. Each is rebuilt as a run file and verified (D2), which returns its debrief. A run recorded under another policy, or whose replay does not match, is skipped and counted as "could not be recomputed".

Which runs count: the player's own runs that reached a debrief (finished or bankrupt), in campaign, free play and practice on the player's packs. Assisted attempts are excluded, because a rewind trades with hindsight. Imported runs are excluded (D5), and so are runs left before their end, which have no debrief.

Aggregation (pure functions, `journalStats.ts`, mirrored by the desktop through the web parity fixtures):
- **Closed trades** are trades with an exit. A trade still open at the end has no R.
- **Win rate**: closed trades with net > 0 over closed trades.
- **R**: average and distribution over closed trades with an R. Buckets: ≤ −2, (−2, −1], (−1, 0], (0, 1], (1, 2], > 2. **Expectancy** is the mean R. Trades without a planned risk are counted apart.
- **Stops**: respected = `planRespected` true over true + false. Moved away = perpetual trades with `stopMovedAway > 0` over perpetual trades that report the field.
- **Risk per trade**: the average risk share of trades with a planned risk, the share within 1–2%, below 1% and above 2%, and the runs with the `oversized` finding.
- **Liquidations and margin calls**: trades closed by liquidation or margin call, and runs with them.
- **Repeated mistakes**: finding codes with tone `bad` or `warn`, by the number of runs that had them, flagged when at least 2 runs did.
- **Per mission and per stage** (the curriculum's mission-to-stage map): runs, closed trades, win rate and expectancy.
- **Trend**: the last 10 runs against the 10 before them (expectancy, stops respected, average risk), plus a row per run in time order. With fewer than 30 closed trades, the screen says it is too early to conclude, the same rule as the campaign report.

Each statistic carries the lesson it measures, and the screen links to that lesson:

| Statistic | Lesson |
|---|---|
| Win rate, R and expectancy | Expectancy and drawdown (`risk-expectancy`) |
| Risk per trade | Sizing from the stop (`risk-sizing`) |
| Stops respected or moved away | Plan, journal and biases (`mind-plan`) |
| Liquidations and margin calls | Margin, leverage and liquidation (`futures-margin`), Writing options (`options-writing`) |
| Repeated mistakes | The debrief's lesson code for each finding |

The texts frame each statistic as a measure of process ("a stop moved away turns a planned loss into a larger one"), never as a promise about the market.

**Rejected alternatives.**
- *Replay every run when the screen opens.* That is seconds of WASM per dozen runs, repeated on every visit.
- *Aggregate in the engine.* The engine only reports per-run facts. Cross-run arithmetic is client display, as with the campaign report. The desktop matches it through the parity fixtures.

### D8. `stopMovedAway` on debrief trades (report-only)

For each perpetual trade, the debrief counts the accepted `MoveOrder` commands on a reduce-only stop that moved the trigger away from the entry (lower for a long, higher for a short) while the trade was open. The previous trigger comes from the order's own event history. Moving a stop toward the entry, a take-profit or a trailing stop does not count. Option rows omit the field.

It changes no score and no state, so the state digests, the event digests and `kScoringVersion` (13) stay as they are. Clients treat a missing field (a debrief from an older build) as unknown, not as zero.

**Rejected alternative.** *Derive it in the client from the event feed.* Both clients would need the same order-history walk, and the engine already walks those events for the trade table.

### D9. Fixtures shared by all clients

`engine/tests/runfiles/` holds run files and `index.json`, which lists each file with its expected verdict (`ok`, `reason`, and for valid files `status`, `digest`, `eventsDigest` and `fileDigest`). The valid files are built from the scripted golden runs (`tools/make_run_files.py`): same inputs, same expected digests. The set has:
- an in-progress run;
- a pack-less multi-option run;
- tampered files: one action changed, the digest changed;
- malformed files: truncated, wrong format tag, unknown member, a float, a bad decimal, too many actions, deep nesting, a non-UTF-8 byte;
- a file from another policy.

The engine tests (native and WASM), the web unit tests (the browser WASM build in Node) and the desktop tests check every file. The web unit test also checks that the web's exporter writes the same canonical file from the same run.

## Risks / Trade-offs

- **Files from older policies cannot be watched after an engine update.** This is a deliberate trade for integrity (D4). The refusal names both policy versions.
- **A file can be crafted.** Anyone with the engine can produce a valid file for any play. That is harmless, because imported runs never count (D5), and the UI never calls an imported run "verified as played by" anyone.
- **Plan notes leak if the player includes them.** The option is off by default and the export dialog says what is included.
- **IndexedDB version bump.** Opening version 4 only adds a store. A tab still on version 3 blocks the upgrade until it closes, as with earlier bumps.

## Migration

- Web: database version 4 adds `imports`. Run records gain an optional `summary`, absent on older records until computed (D7).
- Desktop: a schema migration adds the imports table and the summary column.
- Engine: additive C ABI (`kAbiVersion` 1) and a report-only debrief field. No policy, scoring or checkpoint version change.

## Open Questions

- **Older-policy files**: should a client keep (or download) older engine builds, as the API's policy cache does, so it can watch files recorded under an older execution policy? Until then they are refused (D4).
- **Trend window**: are the last 10 runs against the 10 before them the right comparison, or should it be calendar weeks?
- **Plan notes default**: is "off" the right default for including plan notes in a shared file (for example, a teacher reviewing a student's runs needs them)?
