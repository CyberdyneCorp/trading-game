"""Bearer-token dependencies. Only `Authorization: Bearer` is read: cookies (CyberdyneAuth's parent-domain
ones included) are ignored by construction (design D4a)."""

from collections.abc import Callable
from typing import Annotated, Any

from fastapi import Depends, Header, HTTPException

from ..adapters.oidc import InvalidToken
from ..ports import TokenVerifier


def claims_dependency(verifier: TokenVerifier) -> Callable[..., dict[str, Any] | None]:
    def claims(authorization: Annotated[str | None, Header()] = None) -> dict[str, Any] | None:
        """Verified token claims; None without an Authorization header; 401 for anything else."""
        if authorization is None:
            return None
        if not authorization.lower().startswith("bearer "):
            raise HTTPException(401, "missing bearer token")
        try:
            return verifier.verify(authorization[7:])
        except InvalidToken as exc:
            raise HTTPException(401, "invalid token") from exc

    return claims


def player_dependency(claims: Callable[..., dict[str, Any] | None]) -> Callable[..., str]:
    def player(verified: Annotated[dict[str, Any] | None, Depends(claims)]) -> str:
        if verified is None:
            raise HTTPException(401, "missing bearer token")
        return str(verified["sub"])

    return player
