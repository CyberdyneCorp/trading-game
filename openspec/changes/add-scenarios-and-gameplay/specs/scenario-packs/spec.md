# Spec Delta

## Purpose

Defines the immutable, signed scenario pack that carries market data, instruments, costs, events and mission rules to every client, plus the masking and import rules for historical data.

## ADDED Requirements

### Requirement: Pack manifest
Every pack SHALL include a manifest with: scenarioId, schemaVersion, packVersion, mode (`historical`, `synthetic`, `branch`), engineCompatibility range, executionPolicyVersion, scoringVersion, timeframe, step duration, display precision, alias policy, instrument specs (perpetual contract and option series), cost model, liquidity model, funding model, IV model, margin model, mission rules, file list with SHA-256 hashes and sizes, signingKeyId, signature, provenance and license. Monetary values SHALL be serialized as decimal strings of scaled integers.

#### Scenario: Missing field
- **WHEN** a manifest lacks the funding model
- **THEN** validation SHALL fail with an error naming the missing field

### Requirement: Pack schema versions
The engine SHALL read every pack schema version from 1 up to the newest it knows and refuse others with `unsupported_schema`. Schema 2 SHALL add, each optional: the pack's own mission texts in PT-BR and en (`mission.json`: a title and a briefing per locale), mission objectives and limits in the mission rules, mark-price bars (`mark.bin`), settlement sub-bars (`subbars.bin`) and maintenance-margin tiers in the margin model. A schema-1 manifest that names a schema-2 field or file SHALL be refused. Writers SHALL emit schema 1 unless a schema-2 feature is used, so schema-1 packs keep their bytes. Clients SHALL brief a run on a pack with the pack's own texts and objectives when it has them.

#### Scenario: Schema-1 pack after the upgrade
- **WHEN** a pack built before schema 2 is installed or resumed
- **THEN** it SHALL load as before, with the generic pack texts and the common objectives

#### Scenario: Pack with its own mission
- **WHEN** a schema-2 pack carries mission texts and objectives
- **THEN** the briefing SHALL show its title, briefing and objectives in the shown locale, and the engine's mission rules SHALL list those objectives

#### Scenario: Pack with a mark-price series
- **WHEN** a run is on a schema-2 pack whose mark price differs from the last price
- **THEN** the briefing SHALL say that margin, liquidation and funding use the mark, and the clients SHALL estimate and side-check orders at the last price, as the engine places and fills them

#### Scenario: Schema-2 feature in a schema-1 manifest
- **WHEN** a manifest declaring schema 1 lists margin tiers or a `mission.json`
- **THEN** validation SHALL fail (`invalid_field` naming the field, `unsupported_feature` naming the file)

### Requirement: Integrity and compatibility
Clients SHALL verify file sizes, hashes and the manifest signature against a pinned set of publisher keys, and the engine compatibility range, before a pack can be installed or a run started. Packs SHALL be immutable: a content change SHALL produce a new packVersion and SHALL NOT alter existing runs.

#### Scenario: Tampered pack (DATA02)
- **WHEN** one byte of a data file is changed
- **THEN** installation SHALL be refused with reason `hash_mismatch` and no run SHALL start

#### Scenario: Incompatible engine
- **WHEN** a pack requires an engine version outside the installed range
- **THEN** installation SHALL be refused with reason `incompatible_engine`

#### Scenario: Edited manifest
- **WHEN** any manifest value is changed after signing
- **THEN** installation SHALL be refused with reason `bad_signature`, because the signature covers the canonical (RFC 8785) manifest, which lists every file's hash

#### Scenario: Unknown signer
- **WHEN** a pack's `signingKeyId` is neither a pinned publisher key nor this device's key
- **THEN** installation SHALL be refused with reason `untrusted_key`

### Requirement: Pack safety limits
Packs SHALL contain data only (no executable code). Validators SHALL enforce limits on total size, decompressed size, file count, number of bars, events and option series, and field lengths.

#### Scenario: Decompression bomb
- **WHEN** a pack's decompressed size exceeds the declared limit
- **THEN** decompression SHALL abort and the pack SHALL be rejected

#### Scenario: Too many events
- **WHEN** a pack declares more news rows, liquidity shocks or option series than the validator's limits
- **THEN** it SHALL be rejected with reason `too_many_events` or `too_many_series` before any data file is decompressed

#### Scenario: Oversized schema-2 data
- **WHEN** a pack declares more settlement sub-bars than the limit, more than 16 margin tiers, or a `mission.json` larger than 16 KiB
- **THEN** it SHALL be rejected (`too_many_sub_bars`, `invalid_field`, `mission_too_large`) before the data is used

#### Scenario: Hostile bytes
- **WHEN** a pack, CSV file or manifest is arbitrary or mutated data
- **THEN** the validator SHALL return a reason code without crashing or reading out of bounds (fuzzed with libFuzzer under ASan and UBSan)

### Requirement: Historical masking
Historical packs SHALL rebase prices so that the first open of the playable window (the open of the bar at the first decision point) equals 100 by multiplying all OHLC values, strikes and price-denominated parameters by the same positive factor, SHALL express time relative to the first bar, and SHALL display a random alias that is stable within a run. Masking SHALL NOT use any data from after the first decision point to choose its factor. Volume and capacity SHALL be transformed jointly so exposure and capacity are unchanged.

#### Scenario: Geometry preserved
- **WHEN** a historical window is masked
- **THEN** every bar-to-bar return and every candle's wick-to-body ratio SHALL be preserved within the declared rounding

#### Scenario: Factor chosen without the future
- **WHEN** any bar after the first decision point changes
- **THEN** the masked bars up to the first decision point SHALL be unchanged

### Requirement: Reveal metadata
Packs SHALL include reveal metadata (asset, pair, venue, period, transformation, source) shown only in the debrief and only when the license permits. Scenarios the player imported or has already played SHALL be labeled as known scenarios.

#### Scenario: Reveal after completion
- **WHEN** the player is mid-run
- **THEN** no reveal metadata SHALL be exposed in UI, accessibility tree, logs or network responses

### Requirement: Point-in-time historical sources
Historical packs SHALL be built from point-in-time data: every bar, funding rate, mark/index value and volatility-index value SHALL carry its availability time, and the engine SHALL expose a value at decision step `t` only if its availability time is at or before the close of bar `t`. Source extracts SHALL be pinned (`as_of` timestamp, export id, file checksums, source revision) and recorded in the pack provenance so a pack can be rebuilt bit-identically.

#### Scenario: Late-published funding rate
- **WHEN** a funding rate's event time is at bar `t` but its availability time is after bar `t+1` closes
- **THEN** it SHALL first be visible at decision step `t+2`

#### Scenario: Reproducible build
- **WHEN** the packer rebuilds a pack from the recorded provenance
- **THEN** the resulting pack digest SHALL equal the published one

### Requirement: Gap-free playable windows
The packer SHALL refuse to build a historical window whose playable or warm-up range overlaps a source gap, unless the gap is represented as declared continuity bars with zero capacity and the mission opts in.

#### Scenario: Known ingest outage
- **WHEN** a requested window overlaps a source outage with missing bars
- **THEN** the build SHALL fail with reason `source_gap` naming the missing range

### Requirement: Private CSV import
The system SHALL import CSV with UTC timestamp, open, high, low, close, volume and optional funding rate, plus user-entered metadata (asset, pair, venue, volume unit, interval). Validation SHALL check temporal order, duplicates, positive prices, non-negative volume, high ≥ max(open, close, low), low ≤ min(open, close, high), timestamp unit, interval coherence and gaps. Gaps SHALL be reported, never filled with invented prices or volume. By default the bars on either side of a gap SHALL be joined; the player MAY instead choose to represent each missing interval as a declared continuity bar (no trading: the previous close, zero volume, no funding, zero capacity) with a gap policy for indicators (skip or carry forward), recorded in the manifest as `data.gapPolicy` with a continuity column in `bars.bin` (engine execution policy v17). Imported packs SHALL be private to the user and labeled as known scenarios.

#### Scenario: Timestamp in wrong unit (DATA01)
- **WHEN** timestamps are in milliseconds but declared as seconds
- **THEN** import SHALL fail with a clear error and no silent correction

#### Scenario: Missing bars
- **WHEN** consecutive rows are further apart than the declared interval by a whole number of intervals
- **THEN** the report SHALL list the gap (line, from, to, number of missing bars), the import SHALL still be allowed, and no bar SHALL be invented

#### Scenario: Gaps filled with continuity bars
- **WHEN** a CSV with two missing hours is imported with the gap fill "skip"
- **THEN** the pack SHALL hold two continuity bars at the previous close with zero volume, its manifest SHALL declare `gapPolicy: skip`, no order SHALL execute in those bars and indicators SHALL leave them out

#### Scenario: Imported pack
- **WHEN** a valid CSV is imported
- **THEN** the result SHALL be a pack flagged `private`, masked, signed by a per-device key, labeled as a known scenario, with the asset, pair, venue and period only in its reveal metadata, and publishing it SHALL fail with reason `private_pack`

### Requirement: Licensing record
Every official pack SHALL record license, attribution, transformation rights and redistribution/offline rights. Publishing SHALL be blocked when rights for the intended use (commercial, offline) are not recorded.

#### Scenario: Missing offline rights (LIC01)
- **WHEN** a historical pack without offline redistribution rights is marked downloadable
- **THEN** publishing SHALL fail with reason `license_incomplete`
