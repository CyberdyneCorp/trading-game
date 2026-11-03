"""CyberdyneAuth access-token verification (spec player-identity "Discovery-driven token validation").

Everything is derived from the issuer's discovery document: issuer string, JWKS URI and algorithms.
Nothing is hard-coded. JWKS is cached with a TTL and refetched once on an unknown `kid`. Tokens
must be `type == "access"`, unexpired, from the discovered issuer and for our audience. The
discovery document shares the JWKS TTL, so an issuer that moves its `jwks_uri` is picked up.
"""

from __future__ import annotations

import time
from typing import Any

import httpx
import jwt
from jwt import PyJWK


class InvalidToken(Exception):
    pass


class OidcVerifier:
    def __init__(self, discovery_url: str, audience: str, *, client: httpx.Client | None = None, ttl: float = 600.0,
                 leeway: float = 30.0) -> None:
        self._discovery_url = discovery_url
        self._audience = audience
        self._http = client or httpx.Client(timeout=5.0)
        self._ttl = ttl
        self._leeway = leeway
        self._meta: dict[str, Any] | None = None
        self._keys: dict[str, PyJWK] = {}
        self._fetched_at = 0.0

    def _discovery(self) -> dict[str, Any]:
        if self._meta is None:
            res = self._http.get(self._discovery_url)
            res.raise_for_status()
            self._meta = res.json()
        return self._meta

    def _refresh_keys(self) -> None:
        if time.monotonic() - self._fetched_at > self._ttl:
            self._meta = None  # discovery expires with the keys
        jwks = self._http.get(self._discovery()["jwks_uri"])
        jwks.raise_for_status()
        self._keys = {k["kid"]: PyJWK(k) for k in jwks.json().get("keys", []) if "kid" in k}
        self._fetched_at = time.monotonic()

    def _key(self, kid: str | None) -> PyJWK:
        if not self._keys or time.monotonic() - self._fetched_at > self._ttl:
            self._refresh_keys()
        if kid not in self._keys:
            self._refresh_keys()  # key rotation: one refetch before rejecting
        key = self._keys.get(kid or "")
        if key is None:
            raise InvalidToken("unknown signing key")
        return key

    def verify(self, token: str) -> dict[str, Any]:
        try:
            header = jwt.get_unverified_header(token)
            meta = self._discovery()
            algorithms = [a for a in meta.get("id_token_signing_alg_values_supported", ["RS256"]) if a != "none"]
            claims = jwt.decode(
                token,
                self._key(header.get("kid")).key,
                algorithms=algorithms,
                issuer=meta["issuer"],
                audience=self._audience,
                leeway=self._leeway,
                options={"require": ["exp", "iss", "sub", "aud"]},
            )
        except (jwt.PyJWTError, httpx.HTTPError, KeyError) as exc:
            raise InvalidToken(str(exc)) from exc
        if claims.get("type") != "access":
            raise InvalidToken("not an access token")
        return claims
