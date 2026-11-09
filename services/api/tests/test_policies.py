"""Older execution policies replayed side by side (spec game-api "Engine version support")."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

import pytest
import tradelab_engine as te
from conftest import POLICY_CACHE
from test_api import LOG, run_body, sync

from tradelab_api.adapters.engine_replayer import EngineReplayer
from tradelab_api.adapters.policy_replayer import PolicyReplayer, discover
from tradelab_api.domain.errors import ReplayUnavailable, UnsupportedPolicy

CURRENT = te.engine_version()["executionPolicy"]
OLD = CURRENT - 1


def _fake_entry(root: Path, policy: int, library: bool = True) -> None:
    pkg = root / f"policy-{policy}" / "tradelab_engine"
    pkg.mkdir(parents=True)
    if library:
        (pkg / "libtradelab.so").write_bytes(b"")
    (root / f"policy-{policy}" / "meta.json").write_text(json.dumps({"policy": policy, "commit": "abc",
                                                                      "library": "libtradelab.so"}))


def test_discover_keeps_complete_entries_only(tmp_path):
    _fake_entry(tmp_path, OLD)
    _fake_entry(tmp_path, OLD - 1, library=False)
    (tmp_path / "policy-3").mkdir()
    (tmp_path / "policy-3" / "meta.json").write_text("{not json")
    assert set(discover(tmp_path)) == {OLD}
    assert discover(tmp_path / "missing") == {}
    assert discover(None) == {}


def test_window_bounds_the_supported_policies(tmp_path):
    for policy in (OLD, OLD - 1, OLD - 2):
        _fake_entry(tmp_path, policy)
    replayer = PolicyReplayer(EngineReplayer(), tmp_path, window=3)
    assert replayer.supported_policies() == {CURRENT, OLD, OLD - 1}
    assert PolicyReplayer(EngineReplayer(), tmp_path, window=1).supported_policies() == {CURRENT}
    with pytest.raises(UnsupportedPolicy):
        replayer.replay("m01-false-breakout", 5, LOG, None, OLD - 2)


def test_a_broken_policy_worker_is_unavailable_not_a_bad_log(tmp_path):
    _fake_entry(tmp_path, OLD)  # an empty "library": the worker cannot start
    replayer = PolicyReplayer(EngineReplayer(), tmp_path, window=3, workers=1)
    try:
        with pytest.raises(ReplayUnavailable):
            replayer.replay("m01-false-breakout", 5, LOG, None, OLD)
    finally:
        replayer.close()


def test_a_run_outside_the_window_keeps_the_per_run_error(client, auth):
    out = sync(client, auth, [run_body(run_id="run-ancient-01", executionPolicy=1), run_body()]).json()
    assert [o["error"] for o in out] == ["unsupported_policy", None]
    assert client.get("/api/v1/runs/run-ancient-01", headers=auth()).status_code == 404


def test_regression_policy_cannot_change_for_a_stored_run(client, auth):
    # A stored run replays with the policy it was first synced with; another policy for the same
    # run id is a conflict, never a silent replay under the other engine.
    sync(client, auth, [run_body(actions=LOG[:2])], key="a")
    if OLD not in client.get("/api/v1/health").json()["supportedPolicies"]:
        pytest.skip("older policy not cached")
    out = sync(client, auth, [run_body(executionPolicy=OLD)], key="b").json()[0]
    assert out["error"] == "conflict"


# ---- with the real cache (tools/build_policy_cache.py) --------------------------------------------


def _require_cache() -> Path:
    entry = POLICY_CACHE / f"policy-{OLD}"
    if not (entry / "meta.json").is_file():
        if os.environ.get("TRADELAB_REQUIRE_POLICY_CACHE") == "1":
            pytest.fail(f"policy cache missing at {entry}: run tools/build_policy_cache.py")
        pytest.skip(f"no cached policy {OLD}: run tools/build_policy_cache.py")
    return entry


def _replay_with_cached_binding(entry: Path, actions: list[dict[str, Any]]) -> dict[str, Any]:
    """Records the reference run with the old binding in a fresh interpreter (independent of the pool)."""
    meta = json.loads((entry / "meta.json").read_text())
    code = ("import json, sys, tradelab_engine as te; r = te.replay('m01-false-breakout', 5, json.load(sys.stdin));"
            "print(json.dumps({'digest': r.digest, 'policy': te.engine_version()['executionPolicy'],"
            " 'total': r.debrief['score']['total'] if r.debrief else None}))")
    env = {**os.environ, "PYTHONPATH": str(entry), "TRADELAB_LIB": str(entry / "tradelab_engine" / meta["library"])}
    out = subprocess.run([sys.executable, "-c", code], input=json.dumps(actions), capture_output=True, text=True,
                         env=env, cwd=entry, check=True)
    return json.loads(out.stdout)


def test_a_run_recorded_under_an_older_policy_replays_with_that_engine(client, auth):
    entry = _require_cache()
    recorded = _replay_with_cached_binding(entry, LOG)
    assert recorded["policy"] == OLD
    current = te.replay("m01-false-breakout", 5, LOG)
    assert recorded["digest"] != current.digest  # the digest covers the policy version

    assert OLD in client.get("/api/v1/health").json()["supportedPolicies"]
    body = run_body(run_id="run-old-policy-1", executionPolicy=OLD, clientDigest=recorded["digest"],
                    clientActionCount=len(LOG))
    out = sync(client, auth, [body, run_body()]).json()
    assert out[0]["error"] is None
    assert out[0]["serverDigest"] == recorded["digest"]
    assert out[0]["digestMatch"] is True
    assert out[0]["scoreTotal"] == recorded["total"]
    assert out[1]["serverDigest"] == current.digest  # same batch, current policy, in-process
    detail = client.get("/api/v1/runs/run-old-policy-1", headers=auth()).json()
    assert detail["executionPolicy"] == OLD
    assert detail["digestMismatch"] is False


def test_an_older_policy_log_is_extended_and_bad_logs_are_refused(client, auth):
    _require_cache()
    sync(client, auth, [run_body(run_id="run-old-policy-2", executionPolicy=OLD, actions=LOG[:2])], key="a")
    out = sync(client, auth, [run_body(run_id="run-old-policy-2", executionPolicy=OLD)], key="b").json()[0]
    assert out["acceptedThrough"] == len(LOG)
    assert out["serverDigest"] == _replay_with_cached_binding(_require_cache(), LOG)["digest"]
    bad = run_body(run_id="run-old-policy-3", executionPolicy=OLD,
                   actions=[{"k": "order", "type": "market"}])  # fields missing: the old binding refuses it
    assert sync(client, auth, [bad], key="c").json()[0]["error"] == "invalid_log"


# ---- execution policy v18 (add-practice-and-advanced-strategies D8) ------------------------------

V18_LOG: list[dict[str, Any]] = [
    {"k": "adv", "n": 2},
    {"k": "order", "type": "stop_market", "side": "buy", "qty": "1.0", "price": "200.00", "ro": False, "stop": None,
     "tp": None, "trail": None, "tif": "bars", "tifBars": 5, "combo": "c1", "roll": "1"},
    {"k": "order", "type": "limit", "side": "buy", "qty": "1.0", "price": "1.00", "ro": False, "stop": None, "tp": None,
     "trail": None, "tif": "ioc"},
    {"k": "order", "type": "limit", "side": "buy", "qty": "1.0", "price": "2.00", "ro": False, "stop": None, "tp": None,
     "trail": None},
    {"k": "oco", "a": 1, "b": 3},
    {"k": "adv", "n": 200},
]


def test_a_v18_log_with_time_in_force_and_oco_replays_with_the_current_engine(client, auth):
    assert CURRENT >= 18
    expected = te.replay("m01-false-breakout", 5, V18_LOG)
    assert all(r is None or r["accepted"] for r in expected.results)
    body = run_body(run_id="run-v18-tif-oco", actions=V18_LOG, clientDigest=expected.digest,
                    clientActionCount=len(V18_LOG))
    out = sync(client, auth, [body]).json()[0]
    assert out["error"] is None
    assert out["serverDigest"] == expected.digest
    assert out["digestMatch"] is True


def test_a_v18_action_under_the_v17_engine_is_a_bad_log_not_a_crash(client, auth):
    _require_cache()
    bad = run_body(run_id="run-old-policy-oco", executionPolicy=OLD, actions=[{"k": "oco", "a": 1, "b": 2}])
    assert sync(client, auth, [bad], key="d").json()[0]["error"] == "invalid_log"
