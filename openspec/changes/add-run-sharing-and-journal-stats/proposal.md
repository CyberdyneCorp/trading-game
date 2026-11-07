# Proposal: add-run-sharing-and-journal-stats

## Why

A run lives only on the device that played it. A player cannot show a run to a teacher or a friend, or open on the desktop a run played in the browser, even though the deterministic action log already rebuilds any run exactly. Players also see one debrief at a time: nothing tells them whether they keep moving stops, keep sizing above 2% or whether their expectancy is improving, which is the point of keeping a journal.

## What Changes

- **Run files** (`.tlrun`): a versioned JSON document with everything a replay needs and nothing that identifies the device:
  - the mission (or the catalog pack's digest), the seed and the start options;
  - the engine versions it was recorded with;
  - the action log in the shared format (the golden fixtures' format);
  - the run's final status and state digests;
  - the plan notes, only when the player opts in.
- **Engine verification**: a new C ABI call `tl_run_file_verify` parses an untrusted run file with the engine's strict JSON reader and size limits. It validates every field and action, refuses a file from another execution policy, and replays the log from scratch. It accepts the file only when the replay reproduces the recorded status and digests, and returns the canonical file, its SHA-256 and the debrief when the run ended. The web and the desktop both use it, so they accept and refuse the same files.
- **Import and replay viewer**: an imported run is stored apart from the player's own runs. It can be watched bar by bar (step, play/pause, to the end) and then shows its debrief. It never counts for missions completed, curriculum stages, the campaign, advisor points, perks or journal statistics. Export is offered on the debrief, the journal and the statistics screen. Import is offered in the menu and the journal.
- **Journal statistics**: a new screen built from the player's own runs that reached a debrief. It shows:
  - trades, win rate and expectancy in R;
  - the average R and the distribution of R;
  - stops respected and stops moved away from the entry;
  - average risk per trade against the 1–2% rule, and trades above 2%;
  - liquidations and margin calls;
  - mistakes that repeat (debrief finding codes);
  - per mission and per curriculum stage;
  - the last runs against the ones before them.

  Each statistic names the lesson it measures and links to it. Below 30 trades the screen says it is too early to conclude.
- **Engine debrief**: each perpetual trade reports `stopMovedAway`, how many times its protective stop was moved further from the entry while the trade was open. This is a report-only field: no score, digest or version changes.

## Capabilities

### New Capabilities
- `run-sharing`: run file format, privacy, engine verification, import, replay viewer, exclusion from progress.
- `journal-stats`: per-run summary, aggregation, educational framing, the stop-moved-away debrief field.

### Modified Capabilities
- `desktop-client` (from `add-native-clients`): run files and journal statistics on the desktop. Written as an added requirement because that change is not archived.

## Affected

- Engine: new `tl_run_file_verify` (`kAbiVersion` stays 1: additions only); `stopMovedAway` on debrief trades. Execution policy (17), scoring (13) and checkpoint format (5) are unchanged, and every golden digest stays the same.
- Shared fixtures: `engine/tests/runfiles/` (valid, tampered and malformed files with their expected verdicts). The engine tests, the web unit tests and the desktop read them.
- Web: run file export and import, an `imports` IndexedDB store (database version 4), a replay viewer screen, a journal statistics screen, a per-run summary stored with each run record.
- Desktop: the same features over the linked engine (section D of the tasks).

## Non-goals

- Uploading or sharing run files through the API, or links to them: files move as files. Server-side verification can reuse the same engine call later.
- Exporting runs on private (CSV-imported) packs: the market is the player's private data, and its pack is signed by a device key no other device trusts.
- Proving who played a run. A valid file proves only that the engine reproduces it, which is why imported runs never count for anything.
- Replaying files recorded under another execution policy (see design D4; open question for the product owner).
- Statistics across devices, or over imported runs.
