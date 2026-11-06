# Spec Delta

## Purpose

Defines the TradeLab HTTP API for scenario catalog and packs, run sync, server-side re-simulation, progress, data export/erasure and private imports, with its authorization and integrity rules.

## ADDED Requirements

### Requirement: Endpoints
The API SHALL expose under `/api/v1`: `GET /health`, `GET /missions`, `POST /sync/batches`, `GET /runs`, `GET /runs/{id}`, `GET /progress`, `GET /preferences`, `PATCH /preferences`, `GET /me/export`, `DELETE /me/data`, `GET /scenarios`, `GET /scenarios/{id}/manifest`, `GET /packs/{digest}`, `POST /scenario-imports/validate` and `POST /webhooks/cyberdyne-auth`. Runs SHALL be created, extended and completed only through `POST /sync/batches`; there SHALL be no separate run-creation, command or completion endpoint. All endpoints except health, mission listing, the catalog listing, the manifest and download of free packs, and the webhook SHALL require an authenticated player; a missing, malformed or invalid credential SHALL return 401.

#### Scenario: Unauthenticated sync
- **WHEN** `POST /api/v1/sync/batches` or `GET /api/v1/runs` is called without a valid bearer token or session
- **THEN** the API SHALL respond 401

#### Scenario: Public health and missions
- **WHEN** `GET /api/v1/health` or `GET /api/v1/missions` is called without credentials
- **THEN** the API SHALL respond 200 with the engine's execution-policy version or the mission list

### Requirement: Idempotent mutations
Every mutating player request (sync batches, data deletion) SHALL carry an `Idempotency-Key` header, scoped to the calling player; a missing key SHALL return 400. The CyberdyneAuth webhook SHALL be deduplicated on its `webhook-id` instead, and CSV validation changes no state. Repeating a request with the same key and an identical body SHALL return the stored original response without side effects; reusing a key with a different body SHALL return 409.

#### Scenario: Retried sync batch
- **WHEN** `POST /sync/batches` is retried with the same idempotency key and body
- **THEN** the batch SHALL be applied once and both responses SHALL be identical

#### Scenario: Key reused for another payload
- **WHEN** a key already used for one batch is sent with a different body
- **THEN** the API SHALL respond 409 and apply nothing

### Requirement: Ownership
A player SHALL only read or mutate their own runs, progress and imports. Reading another player's resource SHALL return 404, and syncing a run id owned by another player SHALL fail for that run with `not_found`, never revealing that the resource exists.

#### Scenario: Foreign run
- **WHEN** player A requests player B's run
- **THEN** the API SHALL respond 404

#### Scenario: Foreign run id in a sync batch
- **WHEN** player B syncs a run id that belongs to player A
- **THEN** that run's outcome SHALL carry error `not_found` and player A's run SHALL be unchanged

### Requirement: Server re-simulation
On every sync the API SHALL re-simulate the run from its complete stored action log with the recorded mission (pack digest once packs exist), seed and execution-policy version, using the same engine as clients, and SHALL validate the result before storing any new action. It SHALL store its own final digest, status (`active`, `finished` or `bankrupt`), debrief and score. When the client sends `clientDigest` with a `clientActionCount` equal to the server's committed action count, the API SHALL compare digests and flag a mismatch; otherwise no comparison is made. Client-reported balances or scores SHALL NOT be accepted.

#### Scenario: Digest mismatch
- **WHEN** a client's reported final digest for its full log differs from the server's
- **THEN** the run SHALL be stored with the server result and `digestMismatch` = true, the sync outcome SHALL report `digestMatch` = false, and progress SHALL use the server result

#### Scenario: Invalid log
- **WHEN** a synced run contains an unknown or malformed action
- **THEN** that run's outcome SHALL carry error `invalid_log`, nothing SHALL be stored for it, and other runs in the batch SHALL be processed normally

### Requirement: Engine version support
The API SHALL re-simulate a run only with the execution-policy version it was recorded with. A run recorded with a version the server cannot replay SHALL be rejected for that run with error `unsupported_policy`, storing nothing for it. The API SHALL keep every engine and execution-policy version still referenced by an active or syncing run available for re-simulation, within a published support window (the last N policy versions, the current one included), each older version in its own worker processes with the engine library and binding built from that version's commit. A run's execution policy SHALL be fixed by its first sync; a later sync of the same run id with another policy SHALL fail for that run with `conflict`. A replay worker that fails SHALL fail only that run, with `replay_unavailable`, storing nothing.

#### Scenario: Unsupported version
- **WHEN** a device syncs a run recorded with an execution policy the server does not have
- **THEN** that run's outcome SHALL carry error `unsupported_policy` and the rest of the batch SHALL be processed

#### Scenario: Old engine run syncs
- **WHEN** a device syncs a run recorded with an older supported engine version
- **THEN** the API SHALL re-simulate it with that version, and its digest SHALL equal the digest that version's engine produces for the same log

### Requirement: Pack delivery
`GET /scenarios` SHALL return the catalog manifest in the format the web client reads from its static manifest (schema 1), with each pack's `file` naming `GET /packs/{digest}` relative to the catalog URL. Packs SHALL be served by content digest, immutable, with `Content-Length`, strong `ETag` and HTTP range support for resumable downloads. Free packs SHALL need no credential. Packs requiring an entitlement SHALL be listed only to players whose verified token holds it and SHALL fail closed: 401 without a credential, 403 with a token that does not hold the entitlement or whose entitlements claim is missing or malformed.

#### Scenario: Resume download
- **WHEN** a client requests a byte range of a pack after an interruption
- **THEN** the API SHALL return 206 with the requested range

#### Scenario: Range past the end
- **WHEN** a client requests a range starting at or after the pack's size
- **THEN** the API SHALL return 416 with `Content-Range: bytes */<size>`

#### Scenario: Entitled pack without the entitlement
- **WHEN** a signed-in player without the pack's entitlement requests an entitled pack
- **THEN** the API SHALL respond 403 and the pack SHALL NOT appear in that player's catalog listing

### Requirement: Progress and achievements
`GET /progress` SHALL derive every figure from server-replayed runs only: per mission the runs, completed runs, best server score and the best engine-scored lesson result; and achievements as the union over all of the player's runs, each earned by the earliest qualifying run: a mission lesson answered for every point in a finished run, a campaign mission finished (not bankrupt, leaving at least the campaign minimum) in a campaign run whose link names that mission at its index, and one campaign run with such a leg for every campaign mission. A run's campaign link SHALL be fixed by its first sync. Achievements SHALL be removed only by erasing the player's data.

#### Scenario: Lesson achievement
- **WHEN** a player syncs a finished run whose recorded lesson answers earn every lesson point
- **THEN** `GET /progress` SHALL list the lesson achievement for that mission with that run

#### Scenario: Sync order does not matter
- **WHEN** the same runs are synced from several devices in any order
- **THEN** the achievements SHALL be the same

### Requirement: Preferences
The API SHALL store player preferences per key with last-writer-wins semantics: a write carrying a key's change time SHALL replace the stored value only when newer, ties SHALL be broken deterministically by the value, and a change time in the future SHALL be clamped to the server's clock. Preferences SHALL be limited in number and size, private to their owner, exported and erased with the player's data.

#### Scenario: Older write loses
- **WHEN** a device sends a preference changed before the stored one
- **THEN** the stored value SHALL be kept and returned

### Requirement: Limits and validation
The API SHALL enforce a request size limit (`TRADELAB_MAX_BODY_BYTES`, default 4 MiB, 413) on the declared `Content-Length` and on the bytes actually received (so chunked bodies are limited too), rejecting a malformed `Content-Length` with 400, at most 50 runs per sync batch (413), at most 20,000 actions per run (more in one request fails schema validation; a stored log that would exceed it fails that run with `invalid_log`), per-user rate limits, and schema validation (422). Monetary values SHALL travel as decimal strings, SHALL NOT be parsed into floats by the API, and SHALL be parsed by the engine as int64 fixed-point identically on client and server.

#### Scenario: Oversized batch
- **WHEN** a sync batch contains more than 50 runs
- **THEN** the API SHALL respond 413 and accept nothing from that batch

#### Scenario: Oversized body
- **WHEN** a request declares a `Content-Length` above the configured limit
- **THEN** the API SHALL respond 413 without reading it

#### Scenario: Rate-limited player
- **WHEN** an authenticated player exceeds the configured per-player request rate
- **THEN** the API SHALL respond 429 with a `Retry-After` header, keyed by the verified `sub`, without affecting other players

#### Scenario: Private CSV validation
- **WHEN** a player posts a CSV and its import metadata to `POST /scenario-imports/validate`
- **THEN** the API SHALL return the engine's import report with the same reason codes the clients show, and SHALL store nothing

### Requirement: Privacy-minimal telemetry
The API SHALL record only the telemetry events defined in the product spec, SHALL NOT store real-portfolio data, and SHALL treat private imports as private to their owner.

#### Scenario: Import visibility
- **WHEN** a player uploads a private CSV for validation
- **THEN** it SHALL NOT be listed in any catalog or visible to other users or editors
