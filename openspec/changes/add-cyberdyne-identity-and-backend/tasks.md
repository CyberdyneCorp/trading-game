# Tasks: add-cyberdyne-identity-and-backend

## 1. CyberdyneAuth setup (staging first)
- [ ] 1.1 Request OAuth clients per design D1 with audience `tradelab-api`
- [ ] 1.2 Create role/permission catalog (D2) via `roles:manage`
- [ ] 1.3 Webhook subscription for `user.deleted` (`role.*`, entitlement events later) — the receiver is 3.6; set `TRADELAB_WEBHOOK_SECRET` from the subscription
- [ ] 1.4 Spike: desktop loopback + iOS auth session end-to-end against staging

## 2. API skeleton
- [x] 2.1 FastAPI hexagonal layout, settings, health, Alembic migrations — `domain/`, `ports.py`, `application/`, `adapters/`, `http/`, `TRADELAB_*` settings (`config.py`), `GET /api/v1/health`; Alembic scripts in `src/tradelab_api/migrations/` (`0001` baseline = the former `create_all` schema, `0002` webhook receipts + erasure log), applied at startup by `adapters/migrations.py` (`TRADELAB_MIGRATE_ON_START`, default on) and by tests; nothing calls `create_all`; `tests/test_migrations.py` checks the autogenerate diff is empty and the downgrade path on SQLite and PostgreSQL, CI runs `alembic check` on a fresh PostgreSQL
- [x] 2.2 OIDC adapter: discovery cache and JWKS cache sharing a TTL, with kid refresh, claim validation (aud, type, iss, exp, sub) — `adapters/oidc.py`; tests cover wrong aud, `type=refresh`, foreign iss, expired, key rotation
- [ ] 2.3 BFF login/callback/logout/refresh, `__Host-` session store, CSRF; regression test: CyberdyneAuth parent-domain cookie alone → 401 — not started; the API reads only `Authorization: Bearer` today (cookies are ignored by construction, but no regression test exists)
- [ ] 2.4 Roles/permissions resolver with fail-closed fallback — done: `domain/access.py` maps the verified token's `roles` entries of the TradeLab client(s) (`TRADELAB_ROLE_CLIENTS`, default `cyb_tradelab`) to the D2 permissions (`editor`: `scenarios.edit`, `imports.review`; `admin`: all five); other clients' roles, unknown roles, malformed claims and a bare `permissions` claim grant nothing; an absent `roles` claim goes to a role-lookup port that denies when unset or failing; `app.state.auth.require(permission)` → 403 (`tests/test_permissions.py`; no staff endpoint exists yet). Pending: the CyberdyneAuth role lookup adapter for tokens without `roles` (introspection or `GET /clients/me/users/{id}/roles` with a service token: needs the client of 1.1)
- [x] 2.5 Idempotency, rate limits, size limits — `Idempotency-Key` required on `POST /sync/batches` and `DELETE /me/data` (400 missing, 409 reused with another body, per-player keys), body above `TRADELAB_MAX_BODY_BYTES` (declared or streamed) → 413, malformed `Content-Length` → 400, > 50 runs per batch → 413, ≤ 20,000 actions per run; per-player token bucket keyed by the verified `sub` (`TRADELAB_RATE_LIMIT_PER_MINUTE` 120, `TRADELAB_RATE_LIMIT_BURST` 60; in process memory) → 429 with `Retry-After` (`tests/test_rate_limit.py`)

## 3. Domain
- [ ] 3.1 Catalog + pack metadata + range/ETag delivery with entitlement gate — not started; only `GET /api/v1/missions` (the engine's built-in missions) exists
- [x] 3.2 Runs and action logs through sync (no separate run/command/completion endpoints), `GET /runs`, `GET /runs/{id}`; server re-simulation in-process through `bindings/python` (ctypes over the engine C ABI) on every sync
- [ ] 3.3 Progress (union of achievements), preferences (LWW) — done: `GET /progress` per mission (runs, completed, best server score, its net P&L) derived from server-replayed runs; pending: lesson/campaign achievements, preferences
- [x] 3.4 Sync batches (D6): gap-free prefixes, identical re-sends are no-ops, rewritten seq → per-run `conflict`, digest comparison
- [x] 3.5 CSV import validation endpoint (private) — `POST /api/v1/scenario-imports/validate` `{csv, meta}` → the engine's `tl_csv_validate` report through `bindings/python`, unchanged (same codes as the clients' wizard); bearer required, CSV above `TRADELAB_MAX_CSV_BYTES` (4 MiB, the engine's limit) → 413, nothing stored (`tests/test_csv_import.py`). Engine calls are serialized (the C ABI returns JSON in process-wide buffers)
- [x] 3.6 Export and delete my data; webhook-driven erasure — `GET /me/export`, `DELETE /me/data` (runs, actions, idempotency keys); `POST /api/v1/webhooks/cyberdyne-auth` verifies the Standard Webhooks signature (CyberdyneAuth `docs/webhooks.md`; `TRADELAB_WEBHOOK_SECRET`, rotation with two signatures accepted), refuses timestamps outside 5 minutes, deduplicates on `webhook-id` (`webhook_receipts`), erases on `user.deleted` and acknowledges other events; every erasure is written to `erasure_log` (SHA-256 of the `sub`, source, webhook id, runs, time) (`tests/test_webhooks.py`). The subscription itself is 1.3
- [ ] 3.7 Replay older execution-policy versions side by side (support window, process pool) — pending; runs under any policy other than the engine's current one are rejected per run with `unsupported_policy`

## 4. Tests
- [x] 4.1 Unit + integration (Postgres), contract tests from OpenAPI — HTTP integration suite (`services/api/tests/`) run on SQLite and on PostgreSQL (CI service container via `TRADELAB_TEST_DATABASE_URL`); unit tests of `GameService`, `ErasureService` and `ImportService` with fake ports (`test_game_service.py`); OpenAPI contract: the checked-in `services/api/openapi.json` must equal the code's document (test + CI diff) and real responses of every endpoint validate against its schemas (`test_openapi_contract.py`)
- [ ] 4.2 AUTH01, SYNC01, foreign-run 404, wrong-aud 401, forged-score ignored — done: SYNC01 (server side), foreign run 404 / `not_found`, wrong-aud 401, forged score/balance/digest/report fields in a sync batch ignored (`test_client_sent_score_and_balance_are_ignored`); pending: AUTH01 (client side, needs sign-in)
- [ ] 4.3 Security review (OWASP ASVS L2 checklist for session/BFF)

## 5. Deploy
- [ ] 5.1 Coolify apps (`tradelab[-dev]`, `tradelab-api[-dev]`) + Postgres + MinIO buckets; dev and production environments — not started; `services/api/Dockerfile` and CI (`.github/workflows/api.yml`: SQLite + PostgreSQL tests, image build) exist
- [ ] 5.2 Document runbook in `services/api/README.md` — run/test/config/endpoints documented; deployment runbook pending

## 6. Clients
- [ ] 6.1 Web: upload IndexedDB action logs to `POST /api/v1/sync/batches` when signed in — not started (the web client does not call the API)
- [ ] 6.2 Web: sign-in/logout through the BFF, keep-or-delete local data on logout — blocked on 2.3
- [ ] 6.3 Guest progress linking with explicit confirmation, refusing data linked to another `sub`
- [ ] 6.4 Resumable, verified, atomic pack download and install — blocked on 3.1
- [ ] 6.5 Engine pinning for in-flight runs across client updates
- [ ] 6.6 AUTH01 / OFF01 / OFF02 end-to-end on web (native clients: `add-native-clients`)
