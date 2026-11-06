from __future__ import annotations

import json
import os
import time
import uuid
from collections.abc import Callable
from pathlib import Path
from typing import Any

import httpx
import jwt
import pytest
from cryptography.hazmat.primitives.asymmetric import rsa
from fastapi.testclient import TestClient
from jwt.algorithms import RSAAlgorithm

from tradelab_api.adapters.engine_replayer import ACTION_KINDS, EngineCsvValidator, EngineReplayer
from tradelab_api.adapters.oidc import OidcVerifier
from tradelab_api.adapters.policy_replayer import PolicyReplayer
from tradelab_api.config import Settings
from tradelab_api.http.app import create_app

# Older execution policies (tools/build_policy_cache.py). Without the cache only the current policy is
# replayed and the older-policy tests skip, unless TRADELAB_REQUIRE_POLICY_CACHE=1 (CI) fails them.
POLICY_CACHE = Path(os.environ.get("TRADELAB_POLICY_CACHE", Path(__file__).resolve().parents[1] / ".policy-cache"))
ISSUER = "https://auth.test.local"
WEBHOOK_SECRET = "whsec_" + "dGVzdC13ZWJob29rLXNlY3JldC0zMi1ieXRlcy0hISE="  # base64 of a 32-byte test key
DISCOVERY = f"{ISSUER}/.well-known/openid-configuration"


class FakeIdP:
    """Local OIDC provider: discovery + JWKS served through httpx.MockTransport."""

    def __init__(self) -> None:
        self.keys: dict[str, Any] = {}
        self.jwks_hits = 0
        self.discovery_hits = 0
        self.rotate()

    def rotate(self) -> str:
        kid = uuid.uuid4().hex[:8]
        self.keys[kid] = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        self.kid = kid
        return kid

    def handler(self, request: httpx.Request) -> httpx.Response:
        if request.url.path.endswith("openid-configuration"):
            self.discovery_hits += 1
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


@pytest.fixture(scope="session")
def replayer() -> Any:
    r = PolicyReplayer(EngineReplayer(), POLICY_CACHE, window=3, workers=1)
    yield r
    r.close()


def database_url_for_tests() -> str:
    """TRADELAB_TEST_DATABASE_URL runs the same suite against PostgreSQL (CI does); default: SQLite.
    A PostgreSQL database is emptied first."""
    url = os.environ.get("TRADELAB_TEST_DATABASE_URL", "sqlite+pysqlite:///:memory:")
    if not url.startswith("sqlite"):
        _reset(url)
    return url


def build_client(idp: FakeIdP, replayer: Any, *, packs: Any = None, app_options: dict[str, Any] | None = None,
                 **settings: Any) -> TestClient:
    http = httpx.Client(transport=httpx.MockTransport(idp.handler))
    verifier = OidcVerifier(DISCOVERY, "tradelab-api", client=http)
    settings.setdefault("webhook_secret", WEBHOOK_SECRET)
    app = create_app(Settings(database_url=database_url_for_tests(), **settings), verifier, replayer, ACTION_KINDS,
                     csv_validator=EngineCsvValidator(), packs=packs, **(app_options or {}))
    return TestClient(app)


@pytest.fixture
def client(idp: FakeIdP, replayer: Any) -> TestClient:
    return build_client(idp, replayer)


@pytest.fixture
def client_with(idp: FakeIdP, replayer: Any) -> Callable[..., TestClient]:
    return lambda **settings: build_client(idp, replayer, **settings)


def _reset(url: str) -> None:
    from sqlalchemy import text

    from tradelab_api.adapters.db import Base, make_engine

    engine = make_engine(url)
    Base.metadata.drop_all(engine)
    with engine.begin() as conn:
        conn.execute(text("DROP TABLE IF EXISTS alembic_version"))
    engine.dispose()


@pytest.fixture
def auth(idp: FakeIdP) -> Callable[..., dict[str, str]]:
    def headers(sub: str = "player-1", key: str | None = None, **claims: Any) -> dict[str, str]:
        h = {"Authorization": f"Bearer {idp.token(sub, **claims)}"}
        if key:
            h["Idempotency-Key"] = key
        return h

    return headers
