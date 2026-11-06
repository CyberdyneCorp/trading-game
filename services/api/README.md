# TradeLab API

FastAPI service: runs, idempotent sync, **server-side replay and scoring** with the same engine the clients run (via `bindings/python`, ctypes over the engine C ABI), progress, data export and erasure. Hexagonal layout under `src/tradelab_api/`:

- `domain/` and `ports.py`: pure types and protocols; `domain/access.py` maps staff roles to permissions.
- `application/`: use cases: `game.py` (`GameService`: sync, runs, progress, export), `erasure.py` (`ErasureService`: in-game and webhook erasure, audit log), `imports.py` (CSV validation).
- `adapters/`: SQLAlchemy (PostgreSQL; SQLite in tests), Alembic runner (`migrations.py`), engine replayer and CSV validator, CyberdyneAuth OIDC verifier, Standard Webhooks signature check.
- `migrations/`: Alembic scripts, the only source of the schema.
- `http/`: FastAPI wiring, request/response schemas, authentication + rate limit + permission dependencies (`auth.py`, `rate_limit.py`).

Contract: `openspec/changes/add-cyberdyne-identity-and-backend/`.

## Run and test

The engine shared library (`libtradelab`) must be built first; the binding finds it via `TRADELAB_LIB`, next to the package, or in `engine/build/native-{release,debug}`.

```bash
cmake --preset native-release -S ../../engine && cmake --build ../../engine/build/native-release --target tradelab_capi
uv run --python 3.12 pytest -q                                          # SQLite in memory
TRADELAB_TEST_DATABASE_URL=postgresql+psycopg://... uv run pytest -q    # PostgreSQL (drops all tables first: use a throwaway DB)
TRADELAB_DATABASE_URL=postgresql+psycopg://... uv run uvicorn tradelab_api.main:app --reload
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

`tests/test_migrations.py` applies every migration and compares the result with the models (autogenerate diff must be empty) on SQLite and PostgreSQL, and CI runs `alembic check` on a fresh PostgreSQL. `0001` is the schema the app created before migrations existed: a database created by an older build is adopted with `uv run alembic stamp 0001` (after adding the `runs.options` JSON column by hand if it predates it), then `upgrade head`.

### OpenAPI contract

`openapi.json` is the checked-in contract. `tests/test_openapi_contract.py` fails when the code's document differs from it and validates real responses of every endpoint against its schemas; CI also diffs it. After an intentional change: `uv run python -m tradelab_api.openapi_snapshot > openapi.json` and review the diff.

Docker image (build context = repo root): `docker build -f services/api/Dockerfile .`; listens on port 8000. CI: `.github/workflows/api.yml` (`alembic check`, OpenAPI snapshot, tests on SQLite and PostgreSQL, image build).

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
| `TRADELAB_TEST_DATABASE_URL` | unset | Tests only: run the suite against this database |

## Endpoints (v1)

Auth = `Authorization: Bearer <CyberdyneAuth access token>` (`type=access`, `aud=tradelab-api`). Missing/invalid token → 401. Every authenticated request takes a token from the caller's bucket (keyed by the verified `sub`, so a bad token never drains someone else's); an empty bucket → 429 with `Retry-After` (whole seconds). Buckets live in process memory: each API process limits on its own (the deployment runs one).

| Method | Path | Auth | Notes |
|---|---|---|---|
| GET | `/api/v1/health` | – | `{status, executionPolicy}` |
| GET | `/api/v1/missions` | – | engine's built-in mission catalog |
| POST | `/api/v1/sync/batches` | Bearer + `Idempotency-Key` | `{deviceId, runs:[{runId, missionId, seed, executionPolicy, actions:[{seq, action}], clientDigest?, clientActionCount?, options?}]}`; `options` is the run record's start options `{adviceAllowance 0–10, newsLead 0–2, calendar, startingBalance?, leverageCap?}` (absent = defaults; out of range → 422), fixed by the first sync (a different value later → `conflict`) and used to start the replay; ≤ 50 runs (413), ≤ 20,000 actions per run; gap-free action prefixes; server replay and scoring; digest check. Returns one `{runId, acceptedThrough, status, serverDigest, digestMatch, scoreTotal, error}` per run; per-run `error`: `conflict`, `not_found`, `invalid_log`, `unsupported_policy` |
| GET | `/api/v1/runs`, `/api/v1/runs/{id}` | Bearer | newest first, each with its start `options` and `withIntel` (fast wire or calendar used: the score is labeled, not compared with plain runs); detail adds the debrief `report`; other players' runs return 404 |
| GET | `/api/v1/progress` | Bearer | per mission: runs, completed, best server score, its net P&L |
| GET | `/api/v1/me/export` | Bearer | runs with action logs and reports |
| DELETE | `/api/v1/me/data` | Bearer + `Idempotency-Key` | erases the caller's runs, actions and idempotency records and logs the erasure; returns `{deletedRuns}` |
| POST | `/api/v1/scenario-imports/validate` | Bearer | `{csv, meta}` (the CSV text and the import wizard's metadata); returns the engine's `tl_csv_validate` report unchanged (`ok`, `rows`, `errors[{line, code}]`, `gaps`, ..., or `metaError`), the same codes the clients show. CSV above `TRADELAB_MAX_CSV_BYTES` → 413 `too_large`. Nothing is stored or logged |
| POST | `/api/v1/webhooks/cyberdyne-auth` | Standard Webhooks signature | CyberdyneAuth events (below); 204 |

`Idempotency-Key`: missing → 400; same key + same body → stored response; same key + different body → 409.

### CyberdyneAuth webhook

Verified per CyberdyneAuth `docs/webhooks.md` (Standard Webhooks): `webhook-signature` must hold a `v1,` entry equal to base64(HMAC-SHA256(secret, `"{webhook-id}.{webhook-timestamp}.{raw body}"`)) (any entry matches during a secret rotation), and `webhook-timestamp` must be within `TRADELAB_WEBHOOK_TOLERANCE_SECONDS` of the server clock. Bad or missing signature, stale timestamp → 401; signed but not an event → 400; secret not configured → 503. Replays and retries are deduplicated on `webhook-id` (`webhook_receipts` table: a known id answers 204 and does nothing). `user.deleted` erases the `data.user_id` player (same as the token `sub`): runs, action logs, idempotency records (progress is derived from runs; CSV imports are never stored). Other event types (`role.*`, `webhook.test`, future ones) are acknowledged and ignored.

### Erasure audit log

Every erasure (in-game `DELETE /me/data` or webhook) writes one `erasure_log` row: the SHA-256 of the `sub` (never the `sub` itself), the source (`player` or `webhook`), the webhook id, the number of deleted runs and the time, and logs the same line on the `tradelab.erasure` logger. A retried `DELETE` with the same idempotency key or a redelivered webhook does not add a row.

### Staff roles and permissions

Every authenticated user is a player. Staff permissions come only from the token's `roles` claim entries of the TradeLab client(s) (`TRADELAB_ROLE_CLIENTS`, entries `<client_id>:<role_key>`), mapped in `domain/access.py`: `editor` → `scenarios.edit`, `imports.review`; `admin` → those plus `scenarios.publish`, `scenarios.retire`, `runs.inspect`. Roles of other clients, unknown roles, malformed claims and a `permissions` claim alone grant nothing. An absent `roles` claim means "unknown": the resolver asks a role lookup (a port; none is configured until the API has a CyberdyneAuth service client, task 1.1) and denies when it is missing or fails. Staff routes use `app.state.auth.require("<permission>")` (403 without it); none exist yet (the scenario catalog, 3.1, will be the first).

## Not yet implemented

Web BFF login (session cookie/CSRF), the CyberdyneAuth role lookup for tokens without a `roles` claim (needs a service client), staff endpoints (with the scenario catalog), replaying older execution-policy versions, scenario catalog and pack delivery, deployment.
