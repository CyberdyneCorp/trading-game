# Tasks: add-cyberdyne-identity-and-backend

## 1. CyberdyneAuth setup (staging first)
- [ ] 1.1 Request OAuth clients per design D1 with audience `tradelab-api`
- [ ] 1.2 Create role/permission catalog (D2) via `roles:manage`
- [ ] 1.3 Webhook subscription for `user_deleted`, `entitlement_*`
- [ ] 1.4 Spike: desktop loopback + iOS auth session end-to-end against staging

## 2. API skeleton
- [ ] 2.1 FastAPI hexagonal layout, settings, health, Alembic migrations
- [ ] 2.2 OIDC adapter: discovery cache, JWKS cache with kid refresh, claim validation (aud, type, iss, exp)
- [ ] 2.3 BFF login/callback/logout/refresh, `__Host-` session store, CSRF; regression test: CyberdyneAuth parent-domain cookie alone → 401
- [ ] 2.4 Roles/permissions resolver with fail-closed fallback
- [ ] 2.5 Idempotency middleware, rate limits, size limits

## 3. Domain
- [ ] 3.1 Catalog + pack metadata + range/ETag delivery with entitlement gate
- [ ] 3.2 Runs, commands, completion, server re-simulation worker (nanobind engine)
- [ ] 3.3 Progress (union of achievements), preferences (LWW)
- [ ] 3.4 Sync batches (D6)
- [ ] 3.5 CSV import validation endpoint (private)
- [ ] 3.6 Export and delete my data; webhook-driven erasure

## 4. Tests
- [ ] 4.1 Unit + integration (testcontainers Postgres), contract tests from OpenAPI
- [ ] 4.2 AUTH01, SYNC01, foreign-run 404, wrong-aud 401, forged-score ignored
- [ ] 4.3 Security review (OWASP ASVS L2 checklist for session/BFF)

## 5. Deploy
- [ ] 5.1 Coolify apps (`tradelab[-dev]`, `tradelab-api[-dev]`) + Postgres + MinIO buckets; dev and production environments
- [ ] 5.2 Document runbook in `services/api/README.md`
