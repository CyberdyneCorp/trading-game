from __future__ import annotations

import json
import os
import time
import uuid
from collections.abc import Callable
from typing import Any

import httpx
import jwt
import pytest
from cryptography.hazmat.primitives.asymmetric import rsa
from fastapi.testclient import TestClient
from jwt.algorithms import RSAAlgorithm

from tradelab_api.adapters.engine_replayer import ACTION_KINDS, EngineReplayer
from tradelab_api.adapters.oidc import OidcVerifier
from tradelab_api.config import Settings
from tradelab_api.http.app import create_app

ISSUER = "https://auth.test.local"
DISCOVERY = f"{ISSUER}/.well-known/openid-configuration"


class FakeIdP:
    """Local OIDC provider: discovery + JWKS served through httpx.MockTransport."""

    def __init__(self) -> None:
        self.keys: dict[str, Any] = {}
        self.jwks_hits = 0
        self.rotate()

    def rotate(self) -> str:
        kid = uuid.uuid4().hex[:8]
        self.keys[kid] = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        self.kid = kid
        return kid

    def handler(self, request: httpx.Request) -> httpx.Response:
        if request.url.path.endswith("openid-configuration"):
            return httpx.Response(200, json={"issuer": ISSUER, "jwks_uri": f"{ISSUER}/jwks.json",
                                             "id_token_signing_alg_values_supported": ["RS256"]})
        self.jwks_hits += 1
        keys = []
        for kid, key in self.keys.items():
            jwk = json.loads(RSAAlgorithm.to_jwk(key.public_key()))
            keys.append({**jwk, "kid": kid, "alg": "RS256", "use": "sig"})
        return httpx.Response(200, json={"keys": keys})

    def token(self, sub: str = "player-1", **overrides: Any) -> str:
        now = int(time.time())
        claims = {"iss": ISSUER, "sub": sub, "aud": "tradelab-api", "type": "access", "iat": now, "exp": now + 600}
        claims.update(overrides)
        kid = overrides.pop("_kid", None) or self.kid
        claims.pop("_kid", None)
        return jwt.encode(claims, self.keys[kid], algorithm="RS256", headers={"kid": kid})


@pytest.fixture
def idp() -> FakeIdP:
    return FakeIdP()


@pytest.fixture
def client(idp: FakeIdP) -> TestClient:
    http = httpx.Client(transport=httpx.MockTransport(idp.handler))
    verifier = OidcVerifier(DISCOVERY, "tradelab-api", client=http)
    # TRADELAB_TEST_DATABASE_URL runs the same suite against PostgreSQL (CI does); default: SQLite.
    url = os.environ.get("TRADELAB_TEST_DATABASE_URL", "sqlite+pysqlite:///:memory:")
    if not url.startswith("sqlite"):
        _reset(url)
    app = create_app(Settings(database_url=url), verifier, EngineReplayer(), ACTION_KINDS)
    return TestClient(app)


def _reset(url: str) -> None:
    from tradelab_api.adapters.db import Base, make_engine

    engine = make_engine(url)
    Base.metadata.drop_all(engine)
    engine.dispose()


@pytest.fixture
def auth(idp: FakeIdP) -> Callable[..., dict[str, str]]:
    def headers(sub: str = "player-1", key: str | None = None) -> dict[str, str]:
        h = {"Authorization": f"Bearer {idp.token(sub)}"}
        if key:
            h["Idempotency-Key"] = key
        return h

    return headers
