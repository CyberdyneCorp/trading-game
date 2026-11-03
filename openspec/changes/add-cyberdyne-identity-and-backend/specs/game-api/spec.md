# Spec Delta

## Purpose

Defines the TradeLab HTTP API for scenario catalog and packs, runs and commands, server-side re-simulation, progress and private imports, with its authorization and integrity rules.

## ADDED Requirements

### Requirement: Endpoints
The API SHALL expose under `/api/v1`: `GET /scenarios`, `GET /scenarios/{id}/manifest`, `GET /packs/{digest}`, `POST /runs`, `POST /runs/{id}/commands`, `POST /runs/{id}/complete`, `POST /sync/batches`, `GET /progress`, `POST /scenario-imports/validate`, `DELETE /me/data`, `GET /me/export`, and `POST /webhooks/cyberdyne-auth`. All endpoints except health, catalog listing for public packs and the webhook SHALL require an authenticated player.

#### Scenario: Unauthenticated run creation
- **WHEN** `POST /runs` is called without a valid session or token
- **THEN** the API SHALL respond 401

### Requirement: Idempotent mutations
Every mutating request SHALL carry an idempotency key. Repeating a request with the same key and payload SHALL return the original response without side effects; reusing a key with a different payload SHALL return 409.

#### Scenario: Retried completion
- **WHEN** `POST /runs/{id}/complete` is retried with the same idempotency key
- **THEN** the run SHALL be completed once and both responses SHALL be identical

### Requirement: Ownership
A player SHALL only read or mutate their own runs, progress and imports. Access to another player's resource SHALL return 404.

#### Scenario: Foreign run
- **WHEN** player A requests player B's run
- **THEN** the API SHALL respond 404

### Requirement: Server re-simulation
On run completion or sync, the API SHALL re-simulate the run with the recorded pack digest, engine version, execution-policy version, seed and command log, using the same engine as clients. It SHALL compare its final digest with the client's, store its own results and score, and flag mismatches for investigation without trusting client-reported balances or scores.

#### Scenario: Digest mismatch
- **WHEN** a client's reported final digest differs from the server's
- **THEN** the run SHALL be stored with the server result, flagged `digest_mismatch`, and progress SHALL use the server result

### Requirement: Engine version support
The API SHALL keep every engine and execution-policy version still referenced by an active or syncing run available for re-simulation, and SHALL reject runs referencing unknown versions with 422.

#### Scenario: Old engine run syncs
- **WHEN** a device syncs a run recorded with an older supported engine version
- **THEN** the API SHALL re-simulate it with that version

### Requirement: Pack delivery
Packs SHALL be served by content digest, immutable, with `Content-Length`, strong `ETag` and HTTP range support for resumable downloads. Packs requiring an entitlement SHALL return 403 to users without it.

#### Scenario: Resume download
- **WHEN** a client requests a byte range of a pack after an interruption
- **THEN** the API SHALL return 206 with the requested range

### Requirement: Limits and validation
The API SHALL enforce request size limits, command-batch size limits, rate limits per user, and schema validation; monetary values SHALL be decimal strings of scaled integers and SHALL be rejected if not parseable within int64.

#### Scenario: Oversized batch
- **WHEN** a sync batch exceeds the maximum command count
- **THEN** the API SHALL respond 413 and accept nothing from that batch

### Requirement: Privacy-minimal telemetry
The API SHALL record only the telemetry events defined in the product spec, SHALL NOT store real-portfolio data, and SHALL treat private imports as private to their owner.

#### Scenario: Import visibility
- **WHEN** a player uploads a private CSV for validation
- **THEN** it SHALL NOT be listed in any catalog or visible to other users or editors
