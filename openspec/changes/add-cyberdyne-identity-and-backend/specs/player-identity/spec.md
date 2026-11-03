# Spec Delta

## Purpose

Defines how players and staff authenticate to TradeLab through CyberdyneAuth on every client, how roles and entitlements are read, and how sessions, guest play, logout and deletion behave.

## ADDED Requirements

### Requirement: CyberdyneAuth is the only identity provider
TradeLab SHALL authenticate users exclusively through CyberdyneAuth using OpenID Connect authorization code flow with PKCE (S256). TradeLab SHALL NOT store passwords, MFA secrets or social-login tokens. The stable user key SHALL be the token `sub`.

#### Scenario: Sign in
- **WHEN** a user selects "Continue with Cyberdyne"
- **THEN** the client SHALL start an authorization-code + PKCE flow against the issuer's discovered `authorization_endpoint`
- **AND** on success the game profile SHALL be bound to the returned `sub`

### Requirement: Discovery-driven token validation
The game API SHALL validate access tokens using values derived from the issuer's `/.well-known/openid-configuration` (issuer, JWKS URI, signing algorithms excluding `none`; CyberdyneAuth uses RS256), caching JWKS with a TTL and refreshing on unknown `kid`. It SHALL require `type` = `access`, a valid signature, `exp` (with a small clock-skew leeway), `sub`, matching `iss`, and `aud` equal to the configured TradeLab API audience (`tradelab-api`). It SHALL NOT hard-code issuer strings or keys. Native clients SHALL present the token as `Authorization: Bearer`.

#### Scenario: Token for another service
- **WHEN** a request carries a valid CyberdyneAuth token whose `aud` is a different product
- **THEN** the API SHALL respond 401

#### Scenario: Not an access token
- **WHEN** a request carries a correctly signed token whose `type` is not `access`, whose `iss` is not the discovered issuer, or that has expired
- **THEN** the API SHALL respond 401

#### Scenario: Key rotation
- **WHEN** a token is signed with a `kid` not in the cached JWKS
- **THEN** the API SHALL refetch the JWKS once before rejecting

### Requirement: Web sessions through a BFF
The web client SHALL NOT hold access or refresh tokens in JavaScript-accessible storage. The game API SHALL act as a confidential OIDC client (BFF), keep tokens server-side, and issue its own host-only `__Host-` session cookie that is HttpOnly, Secure and SameSite=Lax, with CSRF protection on mutating requests. The API SHALL NOT accept CyberdyneAuth's own session cookies as authentication.

#### Scenario: Parent-domain auth cookies ignored
- **WHEN** a request to the TradeLab API carries only CyberdyneAuth's `access_token` cookie (set for the shared parent domain) and no TradeLab session or bearer token
- **THEN** the API SHALL respond 401

#### Scenario: No tokens in the browser
- **WHEN** the web client is signed in
- **THEN** no access or refresh token SHALL be present in `localStorage`, `sessionStorage`, IndexedDB or non-HttpOnly cookies

### Requirement: Native clients as public PKCE clients
Desktop and iOS clients SHALL use the system browser (desktop: loopback redirect on `127.0.0.1` with any port; iOS: `ASWebAuthenticationSession` with a registered redirect), SHALL embed no client secret, SHALL request `offline_access`, and SHALL store refresh tokens only in the OS secure store (Keychain on macOS/iOS, Credential Manager on Windows, Secret Service on Linux).

#### Scenario: Embedded webview not allowed
- **WHEN** a native client starts sign-in
- **THEN** it SHALL open the system browser or system auth session, never an embedded webview

### Requirement: Roles and permissions
Every authenticated user SHALL be a player. Staff capabilities SHALL come from CyberdyneAuth client roles of the TradeLab client (`editor`, `admin`) and their permissions (`scenarios.edit`, `scenarios.publish`, `scenarios.retire`, `imports.review`). The API SHALL read the `roles`/`permissions` claims filtered by the TradeLab client prefix. If the `roles` claim is absent, the API SHALL treat roles as unknown and resolve them via CyberdyneAuth (introspection or roles endpoint with a service token) before allowing any staff action, failing closed.

#### Scenario: Missing roles claim
- **WHEN** an access token has no `roles` claim and the user calls a publish endpoint
- **THEN** the API SHALL resolve roles from CyberdyneAuth and deny with 403 if resolution fails

### Requirement: Entitlements gate content, not results
Paid or restricted content SHALL be gated by the `entitlements` claim (`<product_key>[:plan]`). Entitlements SHALL NOT change engine parameters, scores, leverage caps or any simulation outcome.

#### Scenario: Entitlement and scoring
- **WHEN** a user with a paid plan and one without play the same pack with the same commands
- **THEN** both SHALL receive identical results and scores

### Requirement: Guest mode
Clients SHALL allow local guest play for onboarding without sign-in. Linking guest progress to an account SHALL require explicit confirmation and SHALL refuse to merge progress already linked to a different `sub`.

#### Scenario: Guest data on a different account
- **WHEN** guest progress already linked to account A is offered for linking to account B
- **THEN** linking SHALL be refused with an explanation

### Requirement: Session expiry offline
Expired sessions SHALL NOT block offline practice with installed packs. Remote operations (sync, download, catalog) SHALL require re-authentication.

#### Scenario: Expired offline (AUTH01)
- **WHEN** the refresh token expires while the device is offline
- **THEN** the player SHALL continue local missions and SHALL be asked to sign in before the next sync

### Requirement: Logout and deletion
Logout SHALL revoke the refresh token at CyberdyneAuth, end the BFF session, stop sync, and offer the user an explicit choice to keep or delete local data. When CyberdyneAuth emits a verified `user_deleted` webhook, or the user requests deletion in-game, the API SHALL erase that user's game data within the retention policy and log the erasure.

#### Scenario: Deletion requested in-game
- **WHEN** a player calls `DELETE /api/v1/me/data` with an idempotency key
- **THEN** all of that player's runs, action logs and idempotency records SHALL be erased, the number of deleted runs returned, and a retry with the same key SHALL return the same response

#### Scenario: User deleted upstream
- **WHEN** a signed `user_deleted` webhook for `sub` S is received
- **THEN** all runs, progress and imports for S SHALL be erased and the webhook acknowledged idempotently
