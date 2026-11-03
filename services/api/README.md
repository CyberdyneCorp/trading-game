# TradeLab API

FastAPI service: runs, idempotent sync, **server-side replay and scoring** with the same engine the clients run (via `bindings/python`), progress, data export and erasure. Hexagonal layout:

- `domain/` and `ports.py`: pure types and protocols.
- `application/game.py`: use cases.
- `adapters/`: SQLAlchemy (PostgreSQL), engine replayer, CyberdyneAuth OIDC verifier.
- `http/`: FastAPI wiring.

Contract: `openspec/changes/add-cyberdyne-identity-and-backend/`.

```bash
cmake --preset native-release -S ../../engine && cmake --build ../../engine/build/native-release --target tradelab_capi
uv run --python 3.12 pytest -q                                   # SQLite
TRADELAB_TEST_DATABASE_URL=postgresql+psycopg://... uv run pytest -q   # PostgreSQL
uv run uvicorn tradelab_api.main:app --reload                     # needs TRADELAB_DATABASE_URL
```

Configuration (env, prefix `TRADELAB_`): `DATABASE_URL`, `OIDC_DISCOVERY_URL` (CyberdyneAuth discovery), `AUDIENCE` (`tradelab-api`), `MAX_BODY_BYTES`. Docker image: `docker build -f services/api/Dockerfile .` from the repo root.

## Endpoints (v1)

| Method | Path | Auth | Notes |
|---|---|---|---|
| GET | `/api/v1/health` | – | engine execution policy |
| GET | `/api/v1/missions` | – | engine catalog |
| POST | `/api/v1/sync/batches` | Bearer + `Idempotency-Key` | up to 50 runs; gap-free action prefixes; server replay; digest check |
| GET | `/api/v1/runs`, `/api/v1/runs/{id}` | Bearer | other players' runs return 404 |
| GET | `/api/v1/progress` | Bearer | per mission: runs, completed, best score |
| GET | `/api/v1/me/export` | Bearer | runs, action logs, reports |
| DELETE | `/api/v1/me/data` | Bearer + `Idempotency-Key` | erases all game data of the caller |

## Not yet implemented

Web BFF login (cookies/CSRF), the CyberdyneAuth `user_deleted` webhook, Alembic migrations (tables are created on startup for now), rate limits, replaying older execution-policy versions, pack delivery, CSV import validation.
