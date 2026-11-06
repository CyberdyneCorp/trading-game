# Design: add-cyberdyne-identity-and-backend

## Context

CyberdyneAuth (inspected 2026-11-02, OpenAPI v0.1.0, issuer `https://auth.backend.coolify.cyberdynecorp.ai`) is an OIDC provider with:
- `authorization_endpoint` `/api/v1/auth/oauth2/authorize`, `token_endpoint` `/api/v1/auth/oauth2/token`, `userinfo`, `jwks_uri` `/.well-known/jwks.json`, `revocation_endpoint`, `introspection_endpoint`, `end_session_endpoint`.
- Grants: `authorization_code` (PKCE **S256 mandatory** for all clients), `refresh_token`, `client_credentials`; device code optional (off by default).
- RS256 tokens; access-token claims `sub`, `type`, `entitlements`, `roles` (`client_id:role_key`), `permissions`, `org`/`orgs`, `aud` (only when the client has `access_token_audience`), `amr`, `auth_time`, `scope`, `client_id`.
- Public clients: loopback redirects match any port (RFC 8252), private-use schemes allowed.
- Client RBAC (role catalog per client, `roles:manage` for self-management), entitlements ledger (`product_key[:plan]`, product key = client id), Apple/Google/Stripe purchase validation, signed webhooks (`user_deleted`, `entitlement_*`).
- Guidance: derive everything from discovery; missing `roles` claim ≠ no roles.

## Decisions

### D1. OAuth clients (registered by a CyberdyneAuth admin)

| client_id (proposed) | type | grants | redirect | notes |
|---|---|---|---|---|
| `cyb_tradelab` | confidential | authorization_code, refresh_token, client_credentials | `https://tradelab.coolify.cyberdynecorp.ai/api/auth/callback`, `https://tradelab-dev.coolify.cyberdynecorp.ai/api/auth/callback` | Web BFF in `services/api`; product key for entitlements; owns role catalog (`roles:manage`, `roles:read`) |
| `cyb_tradelab_desktop` | public | authorization_code, refresh_token | `http://127.0.0.1/callback` (any port) | desktop client (Raven), system browser |
| `cyb_tradelab_ios` | public | authorization_code, refresh_token | `ai.cyberdyne.tradelab:/oauth/callback` | `ASWebAuthenticationSession` |
| `cyb_tradelab_android` | public | (later) | App Link | Phase 5 |
| `cyb_tradelab_packer` | confidential | client_credentials | — | Authoring-time service client for Market4Me exports (audience `market4me-api`, Market4Me role `analyst`); never shipped in any client |

All: `trusted: true`, scopes `openid email profile offline_access roles`, `access_token_audience: "tradelab-api"` so that `permissions` resolve across the client family (CyberdyneAuth unions permissions for clients sharing an audience).

### D2. Roles and permissions
- Catalog on `cyb_tradelab`: roles `editor`, `admin`; permissions `scenarios.edit`, `scenarios.publish`, `scenarios.retire`, `imports.review`, `runs.inspect`.
- The API maps roles to permissions itself (`domain/access.py`: `editor` → `scenarios.edit`, `imports.review`; `admin` → all five) from the verified token's `roles` entries prefixed with a TradeLab client id, so a misconfigured catalog upstream cannot widen access; a `permissions` claim alone grants nothing. Absent `roles` = unknown → role lookup (not configured until the service client exists) → deny.
- Player is implicit (any valid token). No player role to grant → zero onboarding friction.

### D3. Backend shape
- `services/api/src/tradelab_api`: FastAPI, hexagonal: `domain/` (run, incoming run, sync outcome, progress; errors), `ports.py` (RunRepository, IdempotencyStore, Replayer, TokenVerifier, Clock), `application/game.py` (GameService use cases), `adapters/` (SQLAlchemy 2 for PostgreSQL / SQLite in tests, CyberdyneAuth OIDC/JWKS verifier, engine replayer), `http/` (FastAPI wiring and schemas), `config.py` (`TRADELAB_*` settings). Also `application/erasure.py` (ErasureService), `application/imports.py` (CSV validation), `domain/access.py` (roles → permissions), `adapters/migrations.py` + `migrations/` (Alembic; the only source of the schema, applied at startup unless `TRADELAB_MIGRATE_ON_START=false`), `adapters/webhook_signature.py`. Planned adapters: S3-compatible pack/import storage, the CyberdyneAuth role lookup.
- Re-simulation uses `bindings/python` (ctypes over the engine's C ABI `engine/include/tradelab/c_api.h`, shared library target `tradelab_capi` → `libtradelab`), not a compiled Python extension: the same ABI surface the WASM build wraps. A replay is bit-identical to the client run (cross-binding golden test in `bindings/python/tests`). Each sync replays the full action log in-process (milliseconds per run today); a process pool and multiple engine/policy versions side-by-side remain future work: runs recorded under another execution policy are rejected per run with `unsupported_policy` for now.
- The action log the server stores is exactly the client's IndexedDB log (format in `bindings/python/tradelab_engine/replay.py`), keyed by (run_id, seq): re-sent identical actions are no-ops, a different action at an existing seq is a `conflict` for that run, and only the contiguous prefix extending the stored log is accepted (actions after a gap are dropped until the client re-sends the missing ones).
- Tables today: `runs` (owner `sub`, mission, seed, execution policy, device, server/client digest, mismatch flag, status, score, debrief report, start options), `run_actions (run_id, seq) pk`, `idempotency_keys (owner, key) pk`, `webhook_receipts (webhook_id) pk`, `erasure_log` (SHA-256 of the `sub`, source, webhook id, deleted runs, time). Planned: `players`, `progress_achievements`, `preferences`, `imports`, `packs`, `pack_versions`.
- Deployed on Coolify; object storage = MinIO (S3 API) buckets `tradelab-packs` (immutable, by digest) and `tradelab-imports` (private, per-user prefix, lifecycle-expired). Packs are streamed through the API (entitlement check + range support), never via public bucket URLs; presigned GET URLs with short TTL are an allowed optimization.

### D4. Web BFF flow
- `/api/auth/login` → PKCE + state + nonce stored server-side → redirect to authorize.
- `/api/auth/callback` → code exchange with client secret → store tokens in server session (Postgres or Redis) → set `tl_session` cookie (HttpOnly, Secure, SameSite=Lax) + `tl_csrf` double-submit cookie.
- Refresh handled server-side; refresh replay at CyberdyneAuth revokes the tree → BFF treats `invalid_grant` as logout.
- Logout → revoke refresh token → redirect to `end_session_endpoint` with `post_logout_redirect_uri`.

### D4a. Hosts and cookies
- Environments follow the Cyberdyne convention: web `https://tradelab-dev.coolify.cyberdynecorp.ai` (dev) and `https://tradelab.coolify.cyberdynecorp.ai` (prod); API `tradelab-api[-dev].coolify.cyberdynecorp.ai`.
- The SvelteKit node server reverse-proxies `/api/*` to the API service, so the browser only talks to the web host and the BFF cookie is first-party and host-only: `__Host-tl_session` (no `Domain`, `Path=/`, HttpOnly, Secure, SameSite=Lax) + `__Host-tl_csrf`.
- **Shared parent domain hazard:** CyberdyneAuth sets its own `access_token`/`csrf` cookies with `COOKIE_DOMAIN=.coolify.cyberdynecorp.ai`, so browsers send them to every TradeLab host. The API SHALL ignore those cookies entirely and authenticate only via `__Host-tl_session` or an `Authorization: Bearer` header; otherwise any page on a sibling `*.coolify` host could ride a CyberdyneAuth session into TradeLab (confused deputy / CSRF). Our cookie names never collide with CyberdyneAuth's.
- CORS on the API host: deny by default; only native clients (no Origin) and the web host via the proxy.
- API routes live under `/api/v1`, OpenAPI at `/api/openapi.json`, docs at `/api/docs`. Until the BFF exists the API authenticates only via `Authorization: Bearer` and reads no cookies at all.

### D5. Native token handling
- Desktop: loopback listener on ephemeral port, PKCE, tokens in OS keychain via a small cross-platform wrapper (Keychain / Credential Manager / libsecret).
- iOS: `ASWebAuthenticationSession`, Keychain with `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`.
- API accepts `Authorization: Bearer` from native clients; same validation as BFF-derived tokens.

### D6. Sync protocol
- `POST /api/v1/sync/batches` with `Authorization: Bearer` and `Idempotency-Key`; body `{deviceId, runs:[{runId, missionId, seed, executionPolicy, actions:[{seq, action}], clientDigest?, clientActionCount?}]}` (≤ 50 runs, ≤ 20,000 actions per run). Response: one outcome per run `{runId, acceptedThrough, status, serverDigest, digestMatch, scoreTotal, error}`.
- There is no separate run/command/completion endpoint: a run is created by its first sync, completion is the server's replayed status (`finished`/`bankrupt`), and the client's `clientDigest` is compared only when `clientActionCount` equals the server's committed count.
- Action primary key `(run_id, seq)` makes re-sends no-ops. Per-run failures do not fail the batch: the HTTP status stays 200 and the run's `error` is `conflict` (rewritten seq, or run id reused with another mission/seed), `not_found` (run owned by another player), `invalid_log` (malformed action, unknown mission, too many actions) or `unsupported_policy`. Batch-level failures: 401, 400 (no idempotency key), 409 (key reused with another body), 413 (too large), 422 (schema).
- Progress deltas are not returned yet; clients read `GET /api/v1/progress`.

### D7. Webhooks
- Subscribe `cyb_tradelab` to `user.deleted` (and `role.granted`/`role.revoked` once cached authorization exists; entitlement events when CyberdyneAuth emits them); verify the Standard Webhooks signature per CyberdyneAuth `docs/webhooks.md` (HMAC-SHA256 over `id.timestamp.body`, any `v1,` entry during a rotation, timestamp within 5 minutes); store the `webhook-id` receipt so retries and replays are no-ops; acknowledge unknown types with 2xx.
- Endpoint `POST /api/v1/webhooks/cyberdyne-auth`: 204 on success or duplicate, 401 bad signature/stale, 400 signed non-event, 503 without a configured secret (CyberdyneAuth retries). `user.deleted` erases runs, action logs and idempotency records of `data.user_id` and writes the erasure audit log.

### D8. Rate limits
- Per-player token bucket keyed by the verified `sub` (only after the token verifies, so forged tokens cannot drain another player), 120/min sustained with a burst of 60 by default, 429 with `Retry-After`. In process memory: the deployment runs a single API process; a shared store (Redis/Postgres) is needed before scaling out.

## Risks / Trade-offs

- **CyberdyneAuth outage** → offline play unaffected; web sign-in blocked; API keeps verifying cached JWKS for valid tokens.
- **Offline packs contain the future** → server re-simulation proves rule compliance, not that the player didn't peek; no competitive ranking on offline runs.
- **Multiple engine versions on server** → bounded by a support window (e.g. last 3 policy versions); older runs archived as-is.

## Open Questions

- Does CyberdyneAuth's hosted login page support the desktop loopback flow UX well (browser tab left open)? Spike in Phase 0.
- Data retention period for runs after account deletion request (proposed: immediate erase, 30-day backup rotation).
