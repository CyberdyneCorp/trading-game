"""Request authentication dependencies: bearer token → verified claims → rate limit → player or staff.

Only `Authorization: Bearer` is read: cookies (CyberdyneAuth's parent-domain ones included) are ignored
by construction (design D4a). No `from __future__ import annotations`: FastAPI resolves these
signatures at runtime.
"""

from collections.abc import Callable
from typing import Annotated, Any

from fastapi import Depends, Header, HTTPException

from ..adapters.oidc import InvalidToken
from ..domain.access import PermissionResolver
from ..ports import TokenVerifier
from .rate_limit import TokenBuckets, retry_after_header


class Authenticator:
    def __init__(self, verifier: TokenVerifier, limiter: TokenBuckets, resolver: PermissionResolver) -> None:
        self._verifier = verifier
        self._limiter = limiter
        self._resolver = resolver

    def claims(self, authorization: Annotated[str | None, Header()] = None) -> dict[str, Any]:
        """Verified access-token claims of a caller that is within its rate limit (401 / 429)."""
        if not authorization or not authorization.lower().startswith("bearer "):
            raise HTTPException(401, "missing bearer token")
        try:
            claims = self._verifier.verify(authorization[7:])
        except InvalidToken as exc:
            raise HTTPException(401, "invalid token") from exc
        wait = self._limiter.take(str(claims["sub"]))
        if wait > 0:
            raise HTTPException(429, "rate limit exceeded", headers={"Retry-After": retry_after_header(wait)})
        return claims

    def optional_claims(self, authorization: Annotated[str | None, Header()] = None) -> dict[str, Any] | None:
        """As `claims`, but None without an Authorization header (public routes that a token personalizes)."""
        if authorization is None:
            return None
        return self.claims(authorization)

    def player(self) -> Callable[..., str]:
        def dependency(claims: Annotated[dict[str, Any], Depends(self.claims)]) -> str:
            return str(claims["sub"])

        return dependency

    def require(self, permission: str) -> Callable[..., str]:
        """A staff dependency: the caller's `sub` when its roles grant `permission`, otherwise 403."""

        def dependency(claims: Annotated[dict[str, Any], Depends(self.claims)]) -> str:
            if permission not in self._resolver.permissions(claims):
                raise HTTPException(403, "forbidden")
            return str(claims["sub"])

        return dependency
