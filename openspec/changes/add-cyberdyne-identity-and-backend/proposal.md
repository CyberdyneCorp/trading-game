# Proposal: add-cyberdyne-identity-and-backend

## Why

Players need one Cyberdyne identity across web, desktop and iPad, progress that survives devices, and a catalog to download packs. The server must be the authority on progress and scores (re-simulating runs with the same engine), while desktop, iPad/iPhone and the installed web app keep working fully offline. CyberdyneAuth already provides OIDC (auth-code + PKCE), client roles, entitlements and signed webhooks, so the game must integrate rather than build any identity of its own.

## What Changes

- Register TradeLab as OIDC clients in CyberdyneAuth: one confidential web BFF client and public PKCE clients for desktop and iOS (Android later), with a dedicated access-token audience.
- Game roles (`editor`, `admin`) and permissions as CyberdyneAuth client roles; every authenticated user is a player.
- Optional entitlements (`<client_id>:<plan>`) gate paid content only, never results.
- `services/api` (FastAPI, hexagonal, PostgreSQL, object storage): catalog, pack delivery, runs synced as idempotent action-log batches with server re-simulation and scoring, progress, data export/erasure, CSV import validation.
- Offline-first sync protocol with idempotent batches and per-device attempt identity.
- Erasure and export of game data, triggered by the user or by CyberdyneAuth `user.deleted` webhooks.

## Capabilities

### New Capabilities
- `player-identity`: sign-in, sessions, tokens, roles, entitlements, guest mode, logout, account deletion.
- `game-api`: catalog, packs, run sync and re-simulation, progress, export/erasure, imports; validation and authorization rules.
- `offline-sync`: local storage, run durability, batch sync, conflict rules, engine-version pinning.

### Modified Capabilities
None.

## Non-goals

- Any password, MFA or social login inside TradeLab (CyberdyneAuth owns them).
- Payments implementation (CyberdyneAuth + Stripe/App Store own billing; we only read entitlements).
- Online comparable challenges / leaderboards.
- Editorial admin UI (API permissions only).

## Impact

- New: `services/api/`, database migrations, MinIO buckets, Coolify deployment at `tradelab[-dev].coolify.cyberdynecorp.ai`.
- CyberdyneAuth configuration (admin): OAuth clients, audience, scopes, role catalog, webhook subscription. No code changes in CyberdyneAuth.
- Clients: login/logout, token storage, sync engine. Depends on `add-deterministic-engine` (C ABI + `bindings/python` ctypes binding) and `add-scenarios-and-gameplay` (pack format, scoring).
