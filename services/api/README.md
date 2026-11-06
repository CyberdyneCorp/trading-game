# TradeLab API

FastAPI service: runs, idempotent sync, **server-side replay and scoring** with the same engine the clients run (via `bindings/python`, ctypes over the engine C ABI), older execution policies replayed side by side, progress and achievements, preferences, the pack catalog with resumable downloads, data export and erasure. Hexagonal layout under `src/tradelab_api/`:

- `domain/` and `ports.py`: pure types, rules (`progress.py`, `preferences.py`, `catalog.py`; `access.py` maps staff roles to permissions) and protocols.
- `application/`: use cases: `game.py` (`GameService`: sync, runs, progress, preferences, export), `catalog.py` (`CatalogService`), `erasure.py` (`ErasureService`: in-game and webhook erasure, audit log), `imports.py` (CSV validation).
- `adapters/`: SQLAlchemy (PostgreSQL; SQLite in tests), Alembic runner (`migrations.py`), engine replayer and CSV validator, the older-policy process pool (`policy_replayer.py`, `policy_worker.py`), filesystem object store, CyberdyneAuth OIDC verifier, Standard Webhooks signature check.
- `migrations/`: Alembic scripts, the only source of the schema.
- `http/`: FastAPI wiring, request/response schemas, catalog routes and Range handling, authentication + rate limit + permission dependencies (`auth.py`, `rate_limit.py`).

Contract: `openspec/changes/add-cyberdyne-identity-and-backend/`.

## Run and test

The engine shared library (`libtradelab`) must be built first; the binding finds it via `TRADELAB_LIB`, next to the package, or in `engine/build/native-{release,debug}`.

```bash
cmake --preset native-release -S ../../engine && cmake --build ../../engine/build/native-release --target tradelab_capi
python3 ../../tools/build_policy_cache.py                               # older policies -> .policy-cache/ (optional)
uv run --python 3.12 pytest -q                                          # SQLite in memory
TRADELAB_TEST_DATABASE_URL=postgresql+psycopg://... uv run pytest -q    # PostgreSQL (drops all tables first: use a throwaway DB)
TRADELAB_DATABASE_URL=postgresql+psycopg://... TRADELAB_PACKS_DIR=../../apps/web/static/catalog \
  TRADELAB_POLICY_CACHE=.policy-cache uv run uvicorn tradelab_api.main:app --reload
```

Tests: `test_game_service.py` unit-tests the use cases with fake ports (no database, HTTP or engine); the other files go through HTTP with a local fake identity provider, on SQLite and (CI) PostgreSQL.

Without `TRADELAB_DATABASE_URL` the server uses an in-memory SQLite database that is lost on restart. OpenAPI: `/api/openapi.json`, docs: `/api/docs`.

### Migrations (Alembic)

The schema comes only from `src/tradelab_api/migrations/versions/` (nothing calls `create_all`). By default the app runs `alembic upgrade head` on its own connection at startup; set `TRADELAB_MIGRATE_ON_START=false` to run it as a release step instead. CLI (reads `TRADELAB_DATABASE_URL`):

```bash
uv run alembic upgrade head
uv run alembic revision --autogenerate -m "what changed"   # after changing a model in adapters/db.py; review the script
uv run alembic check                                        # fails while models and migrations disagree
```

`tests/test_migrations.py` applies every migration and compares the result with the models (autogenerate diff must be empty) on SQLite and PostgreSQL, and CI runs `alembic check` on a fresh PostgreSQL. `0001` is the schema the app created before migrations existed: a database created by an older build is adopted with `uv run alembic stamp 0001` (after adding the `runs.options` JSON column by hand if it predates it), then `upgrade head` (`0002` adds the webhook receipts and the erasure log, `0003` the preferences table and `runs.campaign`).

### OpenAPI contract

`openapi.json` is the checked-in contract. `tests/test_openapi_contract.py` fails when the code's document differs from it and validates real responses of every endpoint against its schemas; CI also diffs it. After an intentional change: `uv run python -m tradelab_api.openapi_snapshot > openapi.json` and review the diff.

The tests of older policies (`tests/test_policies.py`) use `.policy-cache/` (or `TRADELAB_POLICY_CACHE`) and skip without it; `TRADELAB_REQUIRE_POLICY_CACHE=1` (CI) makes them fail instead.

Docker image (build context = repo root): `docker build -f services/api/Dockerfile .`; listens on port 8000. The image carries the current policy only (the policy cache is not in it yet: deployment, task 5.1). CI: `.github/workflows/api.yml` (`alembic check`, OpenAPI snapshot, policy cache from the full history, tests on SQLite and PostgreSQL, image build).

## Configuration

| Variable | Default | Purpose |
|---|---|---|
| `TRADELAB_DATABASE_URL` | `sqlite+pysqlite:///:memory:` | SQLAlchemy URL (`postgresql+psycopg://...` in production) |
| `TRADELAB_MIGRATE_ON_START` | `true` | Run `alembic upgrade head` at startup |
| `TRADELAB_OIDC_DISCOVERY_URL` | `https://auth.backend.coolify.cyberdynecorp.ai/.well-known/openid-configuration` | CyberdyneAuth discovery; issuer, JWKS and algorithms come from it |
| `TRADELAB_AUDIENCE` | `tradelab-api` | Required `aud` of access tokens |
| `TRADELAB_MAX_BODY_BYTES` | `4194304` | Larger bodies get 413 (declared `Content-Length` or bytes actually streamed); a malformed `Content-Length` gets 400 |
| `TRADELAB_RATE_LIMIT_PER_MINUTE` | `120` | Per-player token bucket refill (requests a minute, keyed by the verified `sub`); `0` turns the limit off |
| `TRADELAB_RATE_LIMIT_BURST` | `60` | Bucket size: requests a player can make back to back |
| `TRADELAB_ROLE_CLIENTS` | `cyb_tradelab` | Comma-separated CyberdyneAuth client ids whose `roles` entries (`<client_id>:<role>`) are TradeLab roles |
| `TRADELAB_WEBHOOK_SECRET` | unset | Signing secret (`whsec_...`) of the CyberdyneAuth webhook subscription; unset = the webhook answers 503 |
| `TRADELAB_WEBHOOK_TOLERANCE_SECONDS` | `300` | Largest distance between `webhook-timestamp` and the server clock |
| `TRADELAB_MAX_CSV_BYTES` | `4194304` | Largest CSV accepted by the import validation (413 above; the body limit applies too) |
| `TRADELAB_LIB` | (search path) | Path to `libtradelab` (set in the Docker image) |
| `TRADELAB_PACKS_DIR` | unset | Pack object store (filesystem adapter): a directory with `catalog.json` and the packs it names; unset = empty catalog |
| `TRADELAB_POLICY_CACHE` | unset | Older-policy cache built by `tools/build_policy_cache.py`; unset = current policy only |
| `TRADELAB_POLICY_WINDOW` | `3` | Policy versions replayed, the current one included |
| `TRADELAB_REPLAY_WORKERS` | `2` | Worker processes per older policy |
| `TRADELAB_TEST_DATABASE_URL` | unset | Tests only: run the suite against this database |

## Endpoints (v1)

Auth = `Authorization: Bearer <CyberdyneAuth access token>` (`type=access`, `aud=tradelab-api`). Missing/invalid token → 401. Cookies are never read. Every authenticated request takes a token from the caller's bucket (keyed by the verified `sub`, so a bad token never drains someone else's); an empty bucket → 429 with `Retry-After` (whole seconds). Buckets live in process memory: each API process limits on its own (the deployment runs one).

| Method | Path | Auth | Notes |
|---|---|---|---|
| GET | `/api/v1/health` | – | `{status, executionPolicy, supportedPolicies}` |
| GET | `/api/v1/missions` | – | engine's built-in mission catalog |
| POST | `/api/v1/sync/batches` | Bearer + `Idempotency-Key` | `{deviceId, runs:[{runId, missionId, seed, executionPolicy, actions:[{seq, action}], clientDigest?, clientActionCount?, options?, campaign?}]}`; `options` is the run record's start options `{adviceAllowance 0–10, newsLead 0–2, calendar, startingBalance?, leverageCap?}` (absent = defaults; out of range → 422) and `campaign` its campaign link `{id, index 0–63}`, both fixed by the first sync (a different value later → `conflict`, as is another mission, seed or policy for the same run id); ≤ 50 runs (413), ≤ 20,000 actions per run; gap-free action prefixes; server replay with the run's execution policy and scoring; digest check. Returns one `{runId, acceptedThrough, status, serverDigest, digestMatch, scoreTotal, error}` per run; per-run `error`: `conflict`, `not_found`, `invalid_log`, `unsupported_policy`, `replay_unavailable` (an older-policy worker failed; nothing stored, re-send later) |
| GET | `/api/v1/runs`, `/api/v1/runs/{id}` | Bearer | newest first, each with its start `options`, `campaign` and `withIntel` (fast wire or calendar used: the score is labeled, not compared with plain runs); detail adds the debrief `report`; other players' runs return 404 |
| GET | `/api/v1/progress` | Bearer | `{missions:[{missionId, runs, completed, bestScore, bestNetPnl, lessonPoints, lessonTotal}], achievements:[{id, kind, missionId, campaignId, runId, earnedAt}]}` (see Progress) |
| GET | `/api/v1/preferences` | Bearer | `{preferences: {key: {value, updatedAt}}}` |
| PATCH | `/api/v1/preferences` | Bearer + `Idempotency-Key` | `{preferences: {key: {value, updatedAt}}}`, last writer wins per key; returns the merged map |
| GET | `/api/v1/scenarios` | optional Bearer | the pack catalog manifest (see Packs); `Cache-Control: no-store` |
| GET | `/api/v1/scenarios/{id}/manifest` | free: – / entitled: Bearer | one catalog entry; 404 unknown, 401/403 as for the pack |
| GET, HEAD | `/api/v1/packs/{digest}` | free: – / entitled: Bearer | the pack bytes (see Packs) |
| GET | `/api/v1/me/export` | Bearer | runs with action logs and reports, preferences |
| DELETE | `/api/v1/me/data` | Bearer + `Idempotency-Key` | erases the caller's runs, actions, preferences and idempotency records and logs the erasure; returns `{deletedRuns}` |
| POST | `/api/v1/scenario-imports/validate` | Bearer | `{csv, meta}` (the CSV text and the import wizard's metadata); returns the engine's `tl_csv_validate` report unchanged (`ok`, `rows`, `errors[{line, code}]`, `gaps`, ..., or `metaError`), the same codes the clients show. CSV above `TRADELAB_MAX_CSV_BYTES` → 413 `too_large`. Nothing is stored or logged |
| POST | `/api/v1/webhooks/cyberdyne-auth` | Standard Webhooks signature | CyberdyneAuth events (below); 204 |

`Idempotency-Key`: missing → 400; same key + same body → stored response; same key + different body → 409.

### CyberdyneAuth webhook

Verified per CyberdyneAuth `docs/webhooks.md` (Standard Webhooks): `webhook-signature` must hold a `v1,` entry equal to base64(HMAC-SHA256(secret, `"{webhook-id}.{webhook-timestamp}.{raw body}"`)) (any entry matches during a secret rotation), and `webhook-timestamp` must be within `TRADELAB_WEBHOOK_TOLERANCE_SECONDS` of the server clock. Bad or missing signature, stale timestamp → 401; signed but not an event → 400; secret not configured → 503. Replays and retries are deduplicated on `webhook-id` (`webhook_receipts` table: a known id answers 204 and does nothing). `user.deleted` erases the `data.user_id` player (same as the token `sub`): runs, action logs, idempotency records (progress is derived from runs; CSV imports are never stored). Other event types (`role.*`, `webhook.test`, future ones) are acknowledged and ignored.

### Erasure audit log

Every erasure (in-game `DELETE /me/data` or webhook) writes one `erasure_log` row: the SHA-256 of the `sub` (never the `sub` itself), the source (`player` or `webhook`), the webhook id, the number of deleted runs and the time, and logs the same line on the `tradelab.erasure` logger. A retried `DELETE` with the same idempotency key or a redelivered webhook does not add a row.

### Staff roles and permissions

Every authenticated user is a player. Staff permissions come only from the token's `roles` claim entries of the TradeLab client(s) (`TRADELAB_ROLE_CLIENTS`, entries `<client_id>:<role_key>`), mapped in `domain/access.py`: `editor` → `scenarios.edit`, `imports.review`; `admin` → those plus `scenarios.publish`, `scenarios.retire`, `runs.inspect`. Roles of other clients, unknown roles, malformed claims and a `permissions` claim alone grant nothing. An absent `roles` claim means "unknown": the resolver asks a role lookup (a port; none is configured until the API has a CyberdyneAuth service client, task 1.1) and denies when it is missing or fails. Staff routes use `app.state.auth.require("<permission>")` (403 without it); none exist yet (the scenario catalog, 3.1, will be the first).

### Older execution policies

A run replays only with the execution policy it was recorded with. The current policy replays in-process; each older one inside the support window (`TRADELAB_POLICY_WINDOW` versions, the current one included) replays in its own process pool (spawned workers, `TRADELAB_REPLAY_WORKERS` each) that loads that policy's library and Python binding from the cache. `python3 tools/build_policy_cache.py [--cache DIR] [--window N]` builds the cache: for each older policy of the window it takes the newest first-parent commit that still carried it, builds `tradelab_capi` from `git archive` of that commit and stores `policy-<v>/{meta.json, tradelab_engine/ + libtradelab}`; entries already built from the same commit are kept, entries outside the window are removed. It needs the full git history (not a shallow clone). A policy outside the window or missing from the cache keeps the per-run `unsupported_policy`.

### Progress

Everything derives from server-replayed runs; achievements are the union over all of a player's runs (any device, any sync order) and are only removed by erasing the player's data:

- `lesson:<mission>` (`lesson`): a finished run whose engine-scored mission lesson (the `lesson` actions in its log) earned every point. `lessonPoints`/`lessonTotal` is the best lesson result per mission.
- `campaign:<mission>` (`campaign_mission`): the mission finished in a campaign run (sync `campaign` link whose index names that mission in the curriculum order), not bankrupt, leaving at least 100 (the web client's `assessed`, which passes curriculum stages).
- `campaign-complete` (`campaign_complete`): one campaign run with such a leg for every campaign mission.

The campaign order and minimum mirror `apps/web/src/lib/game/curriculum.ts` and `campaign.ts` (a test fails when they drift). The capital a leg starts with is the client's `startingBalance`; the server replays with it but does not chain-check it against the previous leg's ending equity. Academy lessons (client-only, not engine-scored) are not server achievements.

### Preferences

Keys match `[A-Za-z0-9][A-Za-z0-9_.:-]{0,63}`, at most 100 per player, values are any JSON up to 4 KB. Each key carries `updatedAt` (ms since the epoch, when the player changed it): a write replaces the stored value only when newer; equal times are broken by the value's canonical JSON, so merges commute. A time in the future is clamped to the server clock, so a device with a wrong clock cannot pin a value.

### Packs

The object store (port `ObjectStore`; filesystem adapter today, an S3/MinIO adapter maps `stat`/`read` to HeadObject/GetObject with `Range`) holds `catalog.json` and the packs. `catalog.json` is exactly the web client's static manifest (`apps/web/static/catalog/catalog.json`, schema 1) plus an optional `entitlement` per pack (`product_key` or `product_key:plan`; absent = free). `GET /api/v1/scenarios` publishes it without `entitlement`, with each `file` rewritten to `packs/<digest>` (relative to the catalog URL, so the web client only changes `VITE_CATALOG_URL` to `/api/v1/scenarios`). Malformed entries and files outside the store are skipped; the file is re-read when it changes.

`GET /api/v1/packs/{digest}`: `ETag` = `"<digest>"` (strong, immutable), `Content-Length`, `Accept-Ranges: bytes`; one `Range` (`a-b`, `a-`, `-n`) → 206 with `Content-Range`, past the end → 416, multi-range or malformed → the whole pack (200); `If-Range` with another validator → 200; `If-None-Match` with the ETag → 304. Free packs are `public, max-age=31536000, immutable` and need no token. Entitled packs fail closed: listed only to a token whose `entitlements` claim (a list of strings) holds the pack's entitlement (`product` is satisfied by any `product:plan`, `product:plan` only by itself); fetching one without a token → 401, with a token lacking it (or a malformed claim) → 403; served `private, no-store`.

## Not yet implemented

Web BFF login (session cookie/CSRF), the CyberdyneAuth role lookup for tokens without a `roles` claim (needs a service client), staff endpoints, the S3/MinIO object-store adapter, the policy cache in the Docker image, deployment.
