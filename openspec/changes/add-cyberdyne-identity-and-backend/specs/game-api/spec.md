# Spec Delta

## Purpose

Defines the TradeLab HTTP API for scenario catalog and packs, run sync, server-side re-simulation, progress, data export/erasure and private imports, with its authorization and integrity rules.

## ADDED Requirements

### Requirement: Endpoints
The API SHALL expose under `/api/v1`: `GET /health`, `GET /missions`, `POST /sync/batches`, `GET /runs`, `GET /runs/{id}`, `GET /progress`, `GET /me/export`, `DELETE /me/data`, `GET /scenarios`, `GET /scenarios/{id}/manifest`, `GET /packs/{digest}`, `POST /scenario-imports/validate` and `POST /webhooks/cyberdyne-auth`. Runs SHALL be created, extended and completed only through `POST /sync/batches`; there SHALL be no separate run-creation, command or completion endpoint. All endpoints except health, mission listing, catalog listing for public packs and the webhook SHALL require an authenticated player; a missing, malformed or invalid credential SHALL return 401.

#### Scenario: Unauthenticated sync
- **WHEN** `POST /api/v1/sync/batches` or `GET /api/v1/runs` is called without a valid bearer token or session
- **THEN** the API SHALL respond 401

#### Scenario: Public health and missions
- **WHEN** `GET /api/v1/health` or `GET /api/v1/missions` is called without credentials
- **THEN** the API SHALL respond 200 with the engine's execution-policy version or the mission list

### Requirement: Idempotent mutations
Every mutating request SHALL carry an `Idempotency-Key` header, scoped to the calling player; a missing key SHALL return 400. Repeating a request with the same key and an identical body SHALL return the stored original response without side effects; reusing a key with a different body SHALL return 409.

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
The API SHALL re-simulate a run only with the execution-policy version it was recorded with. A run recorded with a version the server cannot replay SHALL be rejected for that run with error `unsupported_policy`, storing nothing for it. The API SHALL keep every engine and execution-policy version still referenced by an active or syncing run available for re-simulation, within a published support window.

#### Scenario: Unsupported version
- **WHEN** a device syncs a run recorded with an execution policy the server does not have
- **THEN** that run's outcome SHALL carry error `unsupported_policy` and the rest of the batch SHALL be processed

#### Scenario: Old engine run syncs
- **WHEN** a device syncs a run recorded with an older supported engine version
- **THEN** the API SHALL re-simulate it with that version

### Requirement: Pack delivery
Packs SHALL be served by content digest, immutable, with `Content-Length`, strong `ETag` and HTTP range support for resumable downloads. Packs requiring an entitlement SHALL return 403 to users without it.

#### Scenario: Resume download
- **WHEN** a client requests a byte range of a pack after an interruption
- **THEN** the API SHALL return 206 with the requested range

### Requirement: Limits and validation
The API SHALL enforce a request size limit (`TRADELAB_MAX_BODY_BYTES`, default 4 MiB, 413) on the declared `Content-Length` and on the bytes actually received (so chunked bodies are limited too), rejecting a malformed `Content-Length` with 400, at most 50 runs per sync batch (413), at most 20,000 actions per run (more in one request fails schema validation; a stored log that would exceed it fails that run with `invalid_log`), per-user rate limits, and schema validation (422). Monetary values SHALL travel as decimal strings, SHALL NOT be parsed into floats by the API, and SHALL be parsed by the engine as int64 fixed-point identically on client and server.

#### Scenario: Oversized batch
- **WHEN** a sync batch contains more than 50 runs
- **THEN** the API SHALL respond 413 and accept nothing from that batch

#### Scenario: Oversized body
- **WHEN** a request declares a `Content-Length` above the configured limit
- **THEN** the API SHALL respond 413 without reading it

### Requirement: Privacy-minimal telemetry
The API SHALL record only the telemetry events defined in the product spec, SHALL NOT store real-portfolio data, and SHALL treat private imports as private to their owner.

#### Scenario: Import visibility
- **WHEN** a player uploads a private CSV for validation
- **THEN** it SHALL NOT be listed in any catalog or visible to other users or editors
