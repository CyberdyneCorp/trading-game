"""OpenAPI contract: the checked-in snapshot (`services/api/openapi.json`) matches the code, and real
responses of every endpoint validate against the schema the snapshot publishes.

After an intentional contract change: `uv run python -m tradelab_api.openapi_snapshot > openapi.json`.
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any

import pytest
from conftest import WEBHOOK_SECRET
from jsonschema import Draft202012Validator
from test_api import LOG, run_body, sync
from test_csv_import import META, make_csv

from tradelab_api.adapters.webhook_signature import sign
from tradelab_api.openapi_snapshot import canonical, document

SNAPSHOT = Path(__file__).resolve().parent.parent / "openapi.json"


@pytest.fixture(scope="module")
def spec() -> dict[str, Any]:
    return json.loads(SNAPSHOT.read_text())


def test_snapshot_matches_the_code():
    assert SNAPSHOT.read_text() == canonical(document()), (
        "OpenAPI changed: review it and run `uv run python -m tradelab_api.openapi_snapshot > openapi.json`")


def test_the_app_serves_the_same_document(client, spec):
    assert client.get("/api/openapi.json").json() == spec


def check(spec: dict[str, Any], method: str, path: str, res: Any) -> None:
    op = spec["paths"][path][method]
    declared = op["responses"].get(str(res.status_code))
    assert declared is not None, f"{method.upper()} {path} answered undeclared {res.status_code}"
    content = declared.get("content", {}).get("application/json")
    if content is None:
        assert not res.content
        return
    schema = {**content["schema"], "components": spec["components"]}
    Draft202012Validator(schema).validate(res.json())


def test_responses_validate_against_the_published_schemas(client, auth, spec):
    get = lambda path, url=None, **kw: check(spec, "get", path, client.get(url or path, **kw))  # noqa: E731
    get("/api/v1/health")
    get("/api/v1/missions")
    res = sync(client, auth, [run_body(), run_body(run_id="run-0002-abcd", actions=LOG[:1],
                                                   options={"adviceAllowance": 2})], key="c1")
    check(spec, "post", "/api/v1/sync/batches", res)
    get("/api/v1/runs", headers=auth())
    get("/api/v1/runs/{run_id}", "/api/v1/runs/run-0001-abcd", headers=auth())
    get("/api/v1/runs/{run_id}", "/api/v1/runs/run-0002-abcd", headers=auth())
    get("/api/v1/progress", headers=auth())
    get("/api/v1/me/export", headers=auth())
    for body in ({"csv": make_csv(), "meta": META}, {"csv": "x", "meta": {}}, {"csv": "timestamp\n1\n", "meta": META}):
        check(spec, "post", "/api/v1/scenario-imports/validate",
              client.post("/api/v1/scenario-imports/validate", json=body, headers=auth()))
    check(spec, "post", "/api/v1/sync/batches",
          client.post("/api/v1/sync/batches", json={"deviceId": ""}, headers=auth(key="c2")))  # 422
    payload = json.dumps({"type": "user.deleted", "data": {"user_id": "player-1"}}).encode()
    ts = int(time.time())
    headers = {"webhook-id": "msg_c", "webhook-timestamp": str(ts), "webhook-signature": sign(WEBHOOK_SECRET, "msg_c",
                                                                                              ts, payload)}
    check(spec, "post", "/api/v1/webhooks/cyberdyne-auth",
          client.post("/api/v1/webhooks/cyberdyne-auth", content=payload, headers=headers))
    check(spec, "delete", "/api/v1/me/data", client.delete("/api/v1/me/data", headers=auth(key="d1")))
