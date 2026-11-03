# TradeLab API

FastAPI service: runs, idempotent sync, **server-side replay and scoring** with the same engine the clients run (via `bindings/python`, ctypes over the engine C ABI), progress, data export and erasure. Hexagonal layout under `src/tradelab_api/`:

- `domain/` and `ports.py`: pure types and protocols.
- `application/game.py`: use cases (`GameService`).
- `adapters/`: SQLAlchemy (PostgreSQL; SQLite in tests), engine replayer, CyberdyneAuth OIDC verifier.
- `http/`: FastAPI wiring and request/response schemas.

Contract: `openspec/changes/add-cyberdyne-identity-and-backend/`.

## Run and test

The engine shared library (`libtradelab`) must be built first; the binding finds it via `TRADELAB_LIB`, next to the package, or in `engine/build/native-{release,debug}`.

```bash
cmake --preset native-release -S ../../engine && cmake --build ../../engine/build/native-release --target tradelab_capi
uv run --python 3.12 pytest -q                                          # SQLite in memory
TRADELAB_TEST_DATABASE_URL=postgresql+psycopg://... uv run pytest -q    # PostgreSQL (drops all tables first: use a throwaway DB)
TRADELAB_DATABASE_URL=postgresql+psycopg://... uv run uvicorn tradelab_api.main:app --reload
```

Tables are created at startup (no migrations yet). Without `TRADELAB_DATABASE_URL` the server uses an in-memory SQLite database that is lost on restart. OpenAPI: `/api/openapi.json`, docs: `/api/docs`.

Docker image (build context = repo root): `docker build -f services/api/Dockerfile .`; listens on port 8000. CI: `.github/workflows/api.yml` (tests on SQLite and PostgreSQL, image build).

## Configuration

| Variable | Default | Purpose |
|---|---|---|
| `TRADELAB_DATABASE_URL` | `sqlite+pysqlite:///:memory:` | SQLAlchemy URL (`postgresql+psycopg://...` in production) |
| `TRADELAB_OIDC_DISCOVERY_URL` | `https://auth.backend.coolify.cyberdynecorp.ai/.well-known/openid-configuration` | CyberdyneAuth discovery; issuer, JWKS and algorithms come from it |
| `TRADELAB_AUDIENCE` | `tradelab-api` | Required `aud` of access tokens |
| `TRADELAB_MAX_BODY_BYTES` | `4194304` | Larger bodies get 413 (declared `Content-Length` or bytes actually streamed); a malformed `Content-Length` gets 400 |
| `TRADELAB_LIB` | (search path) | Path to `libtradelab` (set in the Docker image) |
| `TRADELAB_TEST_DATABASE_URL` | unset | Tests only: run the suite against this database |

## Endpoints (v1)

Auth = `Authorization: Bearer <CyberdyneAuth access token>` (`type=access`, `aud=tradelab-api`). Missing/invalid token → 401.

| Method | Path | Auth | Notes |
|---|---|---|---|
| GET | `/api/v1/health` | – | `{status, executionPolicy}` |
| GET | `/api/v1/missions` | – | engine's built-in mission catalog |
| POST | `/api/v1/sync/batches` | Bearer + `Idempotency-Key` | `{deviceId, runs:[{runId, missionId, seed, executionPolicy, actions:[{seq, action}], clientDigest?, clientActionCount?}]}`; ≤ 50 runs (413), ≤ 20,000 actions per run; gap-free action prefixes; server replay and scoring; digest check. Returns one `{runId, acceptedThrough, status, serverDigest, digestMatch, scoreTotal, error}` per run; per-run `error`: `conflict`, `not_found`, `invalid_log`, `unsupported_policy` |
| GET | `/api/v1/runs`, `/api/v1/runs/{id}` | Bearer | newest first; detail adds the debrief `report`; other players' runs return 404 |
| GET | `/api/v1/progress` | Bearer | per mission: runs, completed, best server score, its net P&L |
| GET | `/api/v1/me/export` | Bearer | runs with action logs and reports |
| DELETE | `/api/v1/me/data` | Bearer + `Idempotency-Key` | erases the caller's runs, actions and idempotency records; returns `{deletedRuns}` |

`Idempotency-Key`: missing → 400; same key + same body → stored response; same key + different body → 409.

## Not yet implemented

Web BFF login (session cookie/CSRF), roles/permissions resolver, the CyberdyneAuth `user_deleted` webhook, Alembic migrations, rate limits, replaying older execution-policy versions, scenario catalog and pack delivery, CSV import validation, deployment.
