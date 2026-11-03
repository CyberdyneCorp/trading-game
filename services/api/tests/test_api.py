from __future__ import annotations

from typing import Any

import httpx
import tradelab_engine as te
from conftest import DISCOVERY

from tradelab_api.adapters.oidc import OidcVerifier

POLICY = te.engine_version()["executionPolicy"]
LOG: list[dict[str, Any]] = [
    {"k": "ans", "pBp": 6000, "skipped": False},
    {"k": "plan", "side": "buy", "skipped": False, "option": False, "lose": False, "inv": "95.50", "target": None,
     "conf": 60, "risk": "100.00"},
    {"k": "order", "type": "market", "side": "buy", "qty": "10.0", "price": None, "ro": False, "stop": "95.50",
     "tp": "110.00", "trail": None},
    {"k": "adv", "n": 200},
]


def run_body(run_id: str = "run-0001-abcd", actions: list[dict[str, Any]] | None = None, start: int = 0,
             **extra: Any) -> dict[str, Any]:
    acts = LOG if actions is None else actions
    body = {"runId": run_id, "missionId": "m01-false-breakout", "seed": 5, "executionPolicy": POLICY,
            "actions": [{"seq": start + i, "action": a} for i, a in enumerate(acts)]}
    body.update(extra)
    return body


def sync(client, auth, runs, key="k1", sub="player-1"):
    return client.post("/api/v1/sync/batches", json={"deviceId": "ipad-1", "runs": runs}, headers=auth(sub, key))


def test_health_and_missions_are_public(client):
    assert client.get("/api/v1/health").json()["executionPolicy"] == POLICY
    assert any(m["id"] == "m01-false-breakout" for m in client.get("/api/v1/missions").json())


def test_auth_rejects_missing_wrong_audience_and_non_access_tokens(client, idp):
    assert client.get("/api/v1/runs").status_code == 401
    for bad in (idp.token(aud="another-product"), idp.token(type="refresh"), idp.token(iss="https://evil"),
                idp.token(exp=1)):
        assert client.get("/api/v1/runs", headers={"Authorization": f"Bearer {bad}"}).status_code == 401
    assert client.get("/api/v1/runs", headers={"Authorization": f"Bearer {idp.token()}"}).status_code == 200


def test_key_rotation_refetches_jwks_once(client, idp):
    assert client.get("/api/v1/runs", headers={"Authorization": f"Bearer {idp.token()}"}).status_code == 200
    hits = idp.jwks_hits
    idp.rotate()
    assert client.get("/api/v1/runs", headers={"Authorization": f"Bearer {idp.token()}"}).status_code == 200
    assert idp.jwks_hits == hits + 1


def test_sync_replays_on_the_server_and_scores_it(client, auth):
    expected = te.replay("m01-false-breakout", 5, LOG)
    body = run_body(clientDigest=expected.digest, clientActionCount=len(LOG))
    res = sync(client, auth, [body])
    assert res.status_code == 200
    out = res.json()[0]
    assert out["error"] is None
    assert out["acceptedThrough"] == len(LOG)
    assert out["serverDigest"] == expected.digest
    assert out["digestMatch"] is True
    assert out["status"] in {"finished", "bankrupt"}
    assert out["scoreTotal"] == expected.debrief["score"]["total"]  # server score, never the client's


def test_digest_mismatch_is_flagged_and_server_result_kept(client, auth):
    body = run_body(clientDigest="0" * 64, clientActionCount=len(LOG))
    out = sync(client, auth, [body]).json()[0]
    assert out["digestMatch"] is False
    detail = client.get("/api/v1/runs/run-0001-abcd", headers=auth()).json()
    assert detail["digestMismatch"] is True
    assert detail["serverDigest"] == te.replay("m01-false-breakout", 5, LOG).digest


def test_idempotency_key_replays_response_and_rejects_other_payloads(client, auth):
    first = sync(client, auth, [run_body()], key="same")
    again = sync(client, auth, [run_body()], key="same")
    assert again.json() == first.json()
    other = sync(client, auth, [run_body(run_id="run-0002-abcd")], key="same")
    assert other.status_code == 409
    assert client.post("/api/v1/sync/batches", json={"deviceId": "x", "runs": []}, headers=auth()).status_code == 400


def test_resent_batch_does_not_duplicate_actions_SYNC01(client, auth):
    sync(client, auth, [run_body(actions=LOG[:2])], key="a")
    out = sync(client, auth, [run_body(actions=LOG[:2])], key="b").json()[0]
    assert out["acceptedThrough"] == 2
    out = sync(client, auth, [run_body(actions=LOG)], key="c").json()[0]  # re-sends 0..1, adds 2..3
    assert out["acceptedThrough"] == 4
    export = client.get("/api/v1/me/export", headers=auth()).json()
    assert len(export["runs"][0]["actions"]) == 4


def test_rewritten_action_conflicts_and_gaps_are_not_accepted(client, auth):
    sync(client, auth, [run_body(actions=LOG[:2])], key="a")
    changed = [LOG[0], {**LOG[1], "conf": 99}]
    assert sync(client, auth, [run_body(actions=changed)], key="b").json()[0]["error"] == "conflict"
    gap = sync(client, auth, [run_body(actions=LOG[3:], start=3)], key="c").json()[0]
    assert gap["acceptedThrough"] == 2  # seq 3 without seq 2: nothing new accepted


def test_runs_are_private_to_their_owner(client, auth):
    sync(client, auth, [run_body()], key="a", sub="alice")
    assert client.get("/api/v1/runs/run-0001-abcd", headers=auth("bob")).status_code == 404
    out = sync(client, auth, [run_body()], key="b", sub="bob").json()[0]
    assert out["error"] == "not_found"
    assert client.get("/api/v1/runs", headers=auth("bob")).json() == []


def test_bad_logs_and_unsupported_policies_are_rejected_per_run(client, auth):
    bad = run_body(run_id="run-bad-0001", actions=[{"k": "explode"}])
    old = run_body(run_id="run-old-0001", executionPolicy=POLICY - 1)
    out = {o["runId"]: o for o in sync(client, auth, [bad, old, run_body()]).json()}
    assert out["run-bad-0001"]["error"] == "invalid_log"
    assert out["run-old-0001"]["error"] == "unsupported_policy"
    assert out["run-0001-abcd"]["error"] is None


def test_oversized_batch_is_rejected(client, auth):
    runs = [run_body(run_id=f"run-{i:04d}-abcd", actions=[]) for i in range(51)]
    assert sync(client, auth, runs).status_code == 413


def test_regression_body_limit_counts_streamed_bytes_not_only_content_length(idp, client_with):
    # Before: only Content-Length was checked, so a chunked body skipped the limit and a
    # non-numeric header raised a 500.
    small = client_with(max_body_bytes=64)
    headers = {"Authorization": f"Bearer {idp.token()}", "Idempotency-Key": "k-1", "Content-Type": "application/json"}
    chunks = iter([b'{"deviceId": "dev-1", ', b'"runs": [], "padding": "' + b"x" * 100 + b'"}'])
    assert small.post("/api/v1/sync/batches", content=chunks, headers=headers).status_code == 413
    assert small.post("/api/v1/sync/batches", content=b"{}",
                      headers={**headers, "Content-Length": "abc"}).status_code == 400
    assert small.post("/api/v1/sync/batches", content=b"x" * 100, headers=headers).status_code == 413
    assert small.get("/api/v1/health").status_code == 200


def test_regression_discovery_is_refetched_after_the_ttl(idp):
    # Before: the discovery document was cached forever, only the JWKS had a TTL.
    http = httpx.Client(transport=httpx.MockTransport(idp.handler))
    verifier = OidcVerifier(DISCOVERY, "tradelab-api", client=http, ttl=0.0)
    verifier.verify(idp.token())
    first = idp.discovery_hits
    verifier.verify(idp.token())
    assert idp.discovery_hits > first


def test_progress_export_and_erase(client, auth):
    sync(client, auth, [run_body()], key="a")
    progress = client.get("/api/v1/progress", headers=auth()).json()
    assert progress[0]["missionId"] == "m01-false-breakout"
    assert progress[0]["completed"] == 1
    assert progress[0]["bestScore"] is not None
    res = client.delete("/api/v1/me/data", headers=auth(key="del"))
    assert res.json() == {"deletedRuns": 1}
    assert client.delete("/api/v1/me/data", headers=auth(key="del")).json() == {"deletedRuns": 1}  # idempotent
    assert client.get("/api/v1/runs", headers=auth()).json() == []
