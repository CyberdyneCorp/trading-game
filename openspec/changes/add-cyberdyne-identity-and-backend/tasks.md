# Tasks: add-cyberdyne-identity-and-backend

## 1. CyberdyneAuth setup (staging first)
- [ ] 1.1 Request OAuth clients per design D1 with audience `tradelab-api`
- [ ] 1.2 Create role/permission catalog (D2) via `roles:manage`
- [ ] 1.3 Webhook subscription for `user_deleted`, `entitlement_*`
- [ ] 1.4 Spike: desktop loopback + iOS auth session end-to-end against staging

## 2. API skeleton
- [ ] 2.1 FastAPI hexagonal layout, settings, health, Alembic migrations — done: `domain/`, `ports.py`, `application/`, `adapters/`, `http/`, `TRADELAB_*` settings (`config.py`), `GET /api/v1/health`; pending: Alembic (tables are created at startup by `Base.metadata.create_all` in `adapters/db.py`)
- [x] 2.2 OIDC adapter: discovery cache and JWKS cache sharing a TTL, with kid refresh, claim validation (aud, type, iss, exp, sub) — `adapters/oidc.py`; tests cover wrong aud, `type=refresh`, foreign iss, expired, key rotation
- [ ] 2.3 BFF login/callback/logout/refresh, `__Host-` session store, CSRF; regression test: CyberdyneAuth parent-domain cookie alone → 401 — not started; the API reads only `Authorization: Bearer` today (cookies are ignored by construction, but no regression test exists)
- [ ] 2.4 Roles/permissions resolver with fail-closed fallback — not started (no staff endpoints yet)
- [ ] 2.5 Idempotency, rate limits, size limits — done: `Idempotency-Key` required on `POST /sync/batches` and `DELETE /me/data` (400 missing, 409 reused with another body, per-player keys), body above `TRADELAB_MAX_BODY_BYTES` (declared or streamed) → 413, malformed `Content-Length` → 400, > 50 runs per batch → 413, ≤ 20,000 actions per run; pending: per-user rate limits

## 3. Domain
- [ ] 3.1 Catalog + pack metadata + range/ETag delivery with entitlement gate — not started; only `GET /api/v1/missions` (the engine's built-in missions) exists
- [x] 3.2 Runs and action logs through sync (no separate run/command/completion endpoints), `GET /runs`, `GET /runs/{id}`; server re-simulation in-process through `bindings/python` (ctypes over the engine C ABI) on every sync
- [ ] 3.3 Progress (union of achievements), preferences (LWW) — done: `GET /progress` per mission (runs, completed, best server score, its net P&L) derived from server-replayed runs; pending: lesson/campaign achievements, preferences
- [x] 3.4 Sync batches (D6): gap-free prefixes, identical re-sends are no-ops, rewritten seq → per-run `conflict`, digest comparison
- [ ] 3.5 CSV import validation endpoint (private) — not started
- [ ] 3.6 Export and delete my data; webhook-driven erasure — done: `GET /me/export`, `DELETE /me/data` (runs, actions, idempotency keys); pending: `POST /webhooks/cyberdyne-auth` (`user_deleted`), erasure audit log
- [ ] 3.7 Replay older execution-policy versions side by side (support window, process pool) — pending; runs under any policy other than the engine's current one are rejected per run with `unsupported_policy`

## 4. Tests
- [ ] 4.1 Unit + integration (Postgres), contract tests from OpenAPI — done: HTTP integration suite (`services/api/tests/`) run on SQLite and on PostgreSQL (CI service container via `TRADELAB_TEST_DATABASE_URL`); pending: unit tests of `GameService` with fake ports, OpenAPI contract tests
- [ ] 4.2 AUTH01, SYNC01, foreign-run 404, wrong-aud 401, forged-score ignored — done: SYNC01 (server side), foreign run 404 / `not_found`, wrong-aud 401; pending: AUTH01 (client), a test that a client-sent score/balance is ignored
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
