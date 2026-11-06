# TradeLab API

FastAPI service: runs, idempotent sync, **server-side replay and scoring** with the same engine the clients run (via `bindings/python`, ctypes over the engine C ABI), older execution policies replayed side by side, progress and achievements, preferences, the pack catalog with resumable downloads, data export and erasure. Hexagonal layout under `src/tradelab_api/`:

- `domain/` and `ports.py`: pure types, rules (`progress.py`, `preferences.py`, `catalog.py`) and protocols.
- `application/`: use cases (`game.py`: `GameService`; `catalog.py`: `CatalogService`).
- `adapters/`: SQLAlchemy (PostgreSQL; SQLite in tests), engine replayer and the older-policy process pool (`policy_replayer.py`, `policy_worker.py`), filesystem object store, CyberdyneAuth OIDC verifier.
- `http/`: FastAPI wiring, bearer auth, schemas, catalog routes and Range handling.

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

Tables are created at startup (no migrations yet). Without `TRADELAB_DATABASE_URL` the server uses an in-memory SQLite database that is lost on restart. OpenAPI: `/api/openapi.json`, docs: `/api/docs`.

The tests of older policies (`tests/test_policies.py`) use `.policy-cache/` (or `TRADELAB_POLICY_CACHE`) and skip without it; `TRADELAB_REQUIRE_POLICY_CACHE=1` (CI) makes them fail instead.

Docker image (build context = repo root): `docker build -f services/api/Dockerfile .`; listens on port 8000. The image carries the current policy only (the policy cache is not in it yet: deployment, task 5.1). CI: `.github/workflows/api.yml` (policy cache from the full history, tests on SQLite and PostgreSQL, image build).

## Configuration

| Variable | Default | Purpose |
|---|---|---|
| `TRADELAB_DATABASE_URL` | `sqlite+pysqlite:///:memory:` | SQLAlchemy URL (`postgresql+psycopg://...` in production) |
| `TRADELAB_OIDC_DISCOVERY_URL` | `https://auth.backend.coolify.cyberdynecorp.ai/.well-known/openid-configuration` | CyberdyneAuth discovery; issuer, JWKS and algorithms come from it |
| `TRADELAB_AUDIENCE` | `tradelab-api` | Required `aud` of access tokens |
| `TRADELAB_MAX_BODY_BYTES` | `4194304` | Larger bodies get 413 (declared `Content-Length` or bytes actually streamed); a malformed `Content-Length` gets 400 |
| `TRADELAB_LIB` | (search path) | Path to `libtradelab` (set in the Docker image) |
| `TRADELAB_PACKS_DIR` | unset | Pack object store (filesystem adapter): a directory with `catalog.json` and the packs it names; unset = empty catalog |
| `TRADELAB_POLICY_CACHE` | unset | Older-policy cache built by `tools/build_policy_cache.py`; unset = current policy only |
| `TRADELAB_POLICY_WINDOW` | `3` | Policy versions replayed, the current one included |
| `TRADELAB_REPLAY_WORKERS` | `2` | Worker processes per older policy |
| `TRADELAB_TEST_DATABASE_URL` | unset | Tests only: run the suite against this database |

## Endpoints (v1)

Auth = `Authorization: Bearer <CyberdyneAuth access token>` (`type=access`, `aud=tradelab-api`). Missing/invalid token → 401. Cookies are never read.

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
| DELETE | `/api/v1/me/data` | Bearer + `Idempotency-Key` | erases the caller's runs, actions, preferences and idempotency records; returns `{deletedRuns}` |

`Idempotency-Key`: missing → 400; same key + same body → stored response; same key + different body → 409.

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

Web BFF login (session cookie/CSRF), roles/permissions resolver, the CyberdyneAuth `user_deleted` webhook, Alembic migrations (a database created before the `runs.options`/`runs.campaign` columns or the `preferences` table needs them added by hand until then; `create_all` adds missing tables, not columns), rate limits, the S3/MinIO object-store adapter, the policy cache in the Docker image, CSV import validation, deployment.
