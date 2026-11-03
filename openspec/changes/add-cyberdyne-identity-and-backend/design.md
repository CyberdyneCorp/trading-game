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
| `cyb_tradelab_desktop` | public | authorization_code, refresh_token | `http://127.0.0.1/callback` (any port) | Dear ImGui client, system browser |
| `cyb_tradelab_ios` | public | authorization_code, refresh_token | `ai.cyberdyne.tradelab:/oauth/callback` | `ASWebAuthenticationSession` |
| `cyb_tradelab_android` | public | (later) | App Link | Phase 5 |
| `cyb_tradelab_packer` | confidential | client_credentials | — | Authoring-time service client for Market4Me exports (audience `market4me-api`, Market4Me role `analyst`); never shipped in any client |

All: `trusted: true`, scopes `openid email profile offline_access roles`, `access_token_audience: "tradelab-api"` so that `permissions` resolve across the client family (CyberdyneAuth unions permissions for clients sharing an audience).

### D2. Roles and permissions
- Catalog on `cyb_tradelab`: roles `editor`, `admin`; permissions `scenarios.edit`, `scenarios.publish`, `scenarios.retire`, `imports.review`, `runs.inspect`.
- Player is implicit (any valid token). No player role to grant → zero onboarding friction.

### D3. Backend shape
- `services/api`: FastAPI, hexagonal: `domain/` (runs, progress, packs), `application/` (use cases), `ports/` (RunRepository, PackStore, IdentityVerifier, EngineReplayer, Clock), `adapters/` (Postgres via SQLAlchemy 2 + Alembic, S3-compatible storage, CyberdyneAuth OIDC/JWKS, nanobind engine).
- Re-simulation runs in a worker pool (process-based) to isolate engine crashes; multiple engine wheel versions installed side-by-side as `tradelab_engine_v{major}_{policy}`.
- Postgres tables: `players(sub pk)`, `runs`, `run_commands (run_id, seq) pk`, `run_results`, `progress_achievements`, `preferences`, `idempotency_keys`, `imports`, `packs`, `pack_versions`, `webhook_receipts`.
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

### D5. Native token handling
- Desktop: loopback listener on ephemeral port, PKCE, tokens in OS keychain via a small cross-platform wrapper (Keychain / Credential Manager / libsecret).
- iOS: `ASWebAuthenticationSession`, Keychain with `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`.
- API accepts `Authorization: Bearer` from native clients; same validation as BFF-derived tokens.

### D6. Sync protocol
- `POST /sync/batches` body: `{deviceId, batches:[{runId, scenarioDigest, engineVersion, policyVersion, seed, commands:[{seq, step, type, payload, clientCommandId}], completion?:{finalDigest}}]}`; response per run: accepted seq range, server digest, score, progress deltas.
- Command primary key `(run_id, seq)` makes re-sends no-ops; a different payload for an existing `(run_id, seq)` → 409 for that run only.

### D7. Webhooks
- Subscribe `cyb_tradelab` to `user_deleted` and `entitlement_granted/revoked`; verify signature per CyberdyneAuth `webhooks.md`; store receipt id for idempotency.

## Risks / Trade-offs

- **CyberdyneAuth outage** → offline play unaffected; web sign-in blocked; API keeps verifying cached JWKS for valid tokens.
- **Offline packs contain the future** → server re-simulation proves rule compliance, not that the player didn't peek; no competitive ranking on offline runs.
- **Multiple engine versions on server** → bounded by a support window (e.g. last 3 policy versions); older runs archived as-is.

## Open Questions

- Does CyberdyneAuth's hosted login page support the desktop loopback flow UX well (browser tab left open)? Spike in Phase 0.
- Data retention period for runs after account deletion request (proposed: immediate erase, 30-day backup rotation).
