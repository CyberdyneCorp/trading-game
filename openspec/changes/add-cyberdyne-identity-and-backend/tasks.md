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
- [ ] 2.5 Idempotency, rate limits, size limits — done: `Idempotency-Key` required on `POST /sync/batches`, `PATCH /preferences` and `DELETE /me/data` (400 missing, 409 reused with another body, per-player keys), body above `TRADELAB_MAX_BODY_BYTES` (declared or streamed) → 413, malformed `Content-Length` → 400, > 50 runs per batch → 413, ≤ 20,000 actions per run; pending: per-user rate limits

## 3. Domain
- [x] 3.1 Catalog + pack metadata + range/ETag delivery with entitlement gate — `GET /api/v1/scenarios` (the web client's static manifest format, `file` → `packs/<digest>`), `GET /api/v1/scenarios/{id}/manifest`, `GET|HEAD /api/v1/packs/{digest}` (strong ETag = digest, single Range → 206, 416, If-Range, If-None-Match → 304); metadata = `catalog.json` in an `ObjectStore` port with a filesystem adapter (`TRADELAB_PACKS_DIR`); optional per-pack `entitlement` checked against the verified token's `entitlements` claim, fail closed (no token 401, without it 403, entitled packs listed only to holders); web e2e `catalog-api.spec.ts` downloads a pack from a locally started API with an interrupted Range download resumed after a reload (`E2E_API_PORT`, CI web job); `VITE_CATALOG_URL` keeps the static default. The S3/MinIO adapter behind the same port comes with the MinIO buckets (5.1)
- [x] 3.2 Runs and action logs through sync (no separate run/command/completion endpoints), `GET /runs`, `GET /runs/{id}`; server re-simulation in-process through `bindings/python` (ctypes over the engine C ABI) on every sync
- [x] 3.3 Progress (union of achievements), preferences (LWW) — `GET /progress`: per mission runs, completed, best server score and its net P&L, best engine-scored lesson result; achievements from server-replayed runs only: `lesson:<mission>` (lesson actions in the log, every point), `campaign:<mission>` (finished campaign leg, ≥ 100 left; sync carries the run record's `campaign` link, fixed by the first sync), `campaign-complete`; `GET|PATCH /preferences` last writer wins per key (`updatedAt`, ties by canonical value, future times clamped), erased and exported with the player's data. Not covered: the capital chain between campaign legs is not checked; Academy lessons stay client-only
- [x] 3.4 Sync batches (D6): gap-free prefixes, identical re-sends are no-ops, rewritten seq → per-run `conflict`, digest comparison
- [ ] 3.5 CSV import validation endpoint (private) — not started
- [ ] 3.6 Export and delete my data; webhook-driven erasure — done: `GET /me/export`, `DELETE /me/data` (runs, actions, idempotency keys); pending: `POST /webhooks/cyberdyne-auth` (`user_deleted`), erasure audit log
- [x] 3.7 Replay older execution-policy versions side by side (support window, process pool) — `tools/build_policy_cache.py` builds `tradelab_capi` and keeps the binding of the newest first-parent commit of each older policy in the window (`--window 3` = current + 2) into `<cache>/policy-<v>/`; `PolicyReplayer` replays the current policy in-process and each older one in a spawned process pool loading that library (`TRADELAB_POLICY_CACHE`, `TRADELAB_POLICY_WINDOW`, `TRADELAB_REPLAY_WORKERS`); outside the window or not cached → `unsupported_policy`; a failed worker → per-run `replay_unavailable`; a run's policy is fixed by its first sync; tests replay a run recorded with the cached previous policy (CI builds the cache from the full history). The Docker image does not carry the cache yet (5.1)

## 4. Tests
- [ ] 4.1 Unit + integration (Postgres), contract tests from OpenAPI — done: HTTP integration suite (`services/api/tests/`) run on SQLite and on PostgreSQL (CI service container via `TRADELAB_TEST_DATABASE_URL`); pending: unit tests of `GameService` with fake ports, OpenAPI contract tests
- [ ] 4.2 AUTH01, SYNC01, foreign-run 404, wrong-aud 401, forged-score ignored — done: SYNC01 (server side), foreign run 404 / `not_found`, wrong-aud 401; pending: AUTH01 (client), a test that a client-sent score/balance is ignored
- [ ] 4.3 Security review (OWASP ASVS L2 checklist for session/BFF)

## 5. Deploy
- [ ] 5.1 Coolify apps (`tradelab[-dev]`, `tradelab-api[-dev]`) + Postgres + MinIO buckets; dev and production environments — not started (includes the S3/MinIO `ObjectStore` adapter, the policy cache in the API image and the web host's same-origin `/api` proxy); `services/api/Dockerfile` and CI (`.github/workflows/api.yml`: SQLite + PostgreSQL tests, image build) exist
- [ ] 5.2 Document runbook in `services/api/README.md` — run/test/config/endpoints documented; deployment runbook pending

## 6. Clients
- [ ] 6.1 Web: upload IndexedDB action logs to `POST /api/v1/sync/batches` when signed in — not started (the web client does not call the API)
- [ ] 6.2 Web: sign-in/logout through the BFF, keep-or-delete local data on logout — blocked on 2.3
- [ ] 6.3 Guest progress linking with explicit confirmation, refusing data linked to another `sub`
- [ ] 6.4 Resumable, verified, atomic pack download and install — web: free packs download from the API catalog (`VITE_CATALOG_URL=/api/v1/scenarios` behind a same-origin `/api` proxy) with the existing resumable, verified, verify-then-install download (e2e `catalog-api.spec.ts`); pending: entitled packs (need sign-in, 6.2), native clients (`add-native-clients`)
- [ ] 6.5 Engine pinning for in-flight runs across client updates
- [ ] 6.6 AUTH01 / OFF01 / OFF02 end-to-end on web (native clients: `add-native-clients`)
