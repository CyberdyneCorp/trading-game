"""POST /api/v1/webhooks/cyberdyne-auth: Standard Webhooks signature, replay protection, `user.deleted`
erasure and the erasure audit log (spec player-identity "Logout and deletion", design D7)."""

from __future__ import annotations

import hashlib
import json
import time
from typing import Any

from conftest import WEBHOOK_SECRET
from sqlalchemy.orm import Session
from test_api import run_body, sync

from tradelab_api.adapters.db import SqlErasureLog
from tradelab_api.adapters.webhook_signature import (
    InvalidWebhook,
    WebhookVerifier,
    sign,
)

PATH = "/api/v1/webhooks/cyberdyne-auth"
OTHER_SECRET = "whsec_" + "b3RoZXItc2VjcmV0LW90aGVyLXNlY3JldC0zMmJ5dGU="


def delivery(event: dict[str, Any], *, webhook_id: str = "msg_1", ts: int | None = None,
             secret: str = WEBHOOK_SECRET, signature: str | None = None) -> tuple[bytes, dict[str, str]]:
    body = json.dumps(event).encode()
    ts = int(time.time()) if ts is None else ts
    sig = signature if signature is not None else sign(secret, webhook_id, ts, body)
    return body, {"webhook-id": webhook_id, "webhook-timestamp": str(ts), "webhook-signature": sig,
                  "content-type": "application/json"}


def deleted(user_id: str) -> dict[str, Any]:
    return {"type": "user.deleted", "timestamp": "2026-11-05T12:00:00Z", "data": {"user_id": user_id}}


def post(client, event, **kw):
    body, headers = delivery(event, **kw)
    return client.post(PATH, content=body, headers=headers)


def audit(client) -> list[Any]:
    with Session(client.app.state.engine) as s:
        return SqlErasureLog(s).entries()


def test_user_deleted_erases_the_players_data_and_is_logged(client, auth):
    sync(client, auth, [run_body()], key="a", sub="alice")
    sync(client, auth, [run_body(run_id="run-0002-bobb")], key="a", sub="bob")
    pref = {"preferences": {"ui.lang": {"value": "pt-BR", "updatedAt": 1}}}
    assert client.patch("/api/v1/preferences", headers=auth("alice", key="p"), json=pref).status_code == 200
    assert post(client, deleted("alice"), webhook_id="msg_del_1").status_code == 204
    assert client.get("/api/v1/runs", headers=auth("alice")).json() == []
    assert client.get("/api/v1/progress", headers=auth("alice")).json() == {"missions": [], "achievements": []}
    assert client.get("/api/v1/preferences", headers=auth("alice")).json() == {"preferences": {}}
    assert len(client.get("/api/v1/runs", headers=auth("bob")).json()) == 1
    entries = audit(client)
    assert [(e.source, e.reference, e.deleted_runs) for e in entries] == [("webhook", "msg_del_1", 1)]
    assert entries[0].subject_sha256 == hashlib.sha256(b"alice").hexdigest()  # never the sub itself


def test_redelivery_is_acknowledged_once(client, auth):
    sync(client, auth, [run_body()], key="a", sub="alice")
    assert post(client, deleted("alice"), webhook_id="msg_x").status_code == 204
    sync(client, auth, [run_body()], key="b", sub="alice")  # plays again after the erasure
    assert post(client, deleted("alice"), webhook_id="msg_x").status_code == 204  # retry: no-op
    assert len(client.get("/api/v1/runs", headers=auth("alice")).json()) == 1
    assert len(audit(client)) == 1


def test_bad_signatures_and_stale_timestamps_are_refused(client, auth):
    sync(client, auth, [run_body()], key="a", sub="alice")
    now = int(time.time())
    assert post(client, deleted("alice"), secret=OTHER_SECRET).status_code == 401
    assert post(client, deleted("alice"), signature="").status_code == 401
    assert post(client, deleted("alice"), signature="v2,abc").status_code == 401
    assert post(client, deleted("alice"), ts=now - 301).status_code == 401  # replay of an old capture
    assert post(client, deleted("alice"), ts=now + 301).status_code == 401
    body, headers = delivery(deleted("alice"))
    tampered = body.replace(b"alice", b"bob00")
    assert client.post(PATH, content=tampered, headers=headers).status_code == 401
    assert client.post(PATH, content=body, headers={**headers, "webhook-id": "msg_other"}).status_code == 401
    assert client.post(PATH, content=body).status_code == 401
    assert len(client.get("/api/v1/runs", headers=auth("alice")).json()) == 1
    assert audit(client) == []


def test_rotation_accepts_any_listed_signature(client):
    body, headers = delivery(deleted("carol"), webhook_id="msg_rot")
    old = sign(OTHER_SECRET, "msg_rot", int(headers["webhook-timestamp"]), body)
    headers["webhook-signature"] = f"{old} {headers['webhook-signature']}"
    assert client.post(PATH, content=body, headers=headers).status_code == 204


def test_unknown_events_are_acknowledged_without_erasing(client, auth):
    sync(client, auth, [run_body()], key="a", sub="alice")
    event = {"type": "role.granted", "data": {"user_id": "alice", "client_id": "cyb_tradelab", "role_key": "editor"}}
    assert post(client, event, webhook_id="msg_role").status_code == 204
    assert post(client, {"type": "webhook.test", "data": {}}, webhook_id="msg_test").status_code == 204
    assert len(client.get("/api/v1/runs", headers=auth("alice")).json()) == 1
    assert audit(client) == []


def test_signed_but_malformed_body_is_400(client):
    body = b"not json"
    ts = int(time.time())
    headers = {"webhook-id": "msg_bad", "webhook-timestamp": str(ts), "webhook-signature": sign(WEBHOOK_SECRET,
                                                                                                "msg_bad", ts, body)}
    assert client.post(PATH, content=body, headers=headers).status_code == 400


def test_unconfigured_secret_answers_503(client_with):
    client = client_with(webhook_secret="")
    assert post(client, deleted("alice")).status_code == 503


def test_in_game_erasure_is_logged_too(client, auth):
    sync(client, auth, [run_body()], key="a", sub="alice")
    assert client.delete("/api/v1/me/data", headers=auth("alice", key="del")).json() == {"deletedRuns": 1}
    assert client.delete("/api/v1/me/data", headers=auth("alice", key="del")).json() == {"deletedRuns": 1}
    entries = audit(client)
    assert [(e.source, e.reference, e.deleted_runs) for e in entries] == [("player", None, 1)]  # retry not re-run


def test_verifier_tolerance_uses_its_clock():
    body, headers = delivery(deleted("x"), ts=1_000)
    assert WebhookVerifier(WEBHOOK_SECRET, 300, clock=lambda: 1_200.0).verify(body, headers).type == "user.deleted"
    try:
        WebhookVerifier(WEBHOOK_SECRET, 300, clock=lambda: 1_400.0).verify(body, headers)
    except InvalidWebhook:
        pass
    else:
        raise AssertionError("stale delivery accepted")

