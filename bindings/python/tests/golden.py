"""Shared golden fixtures (engine/tests/golden, format in engine/README.md): loading and replay
through the Python binding. engine/tests/test_golden.cpp replays the same files natively."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from tradelab_engine import (ReplayResult, Run, RunOptions, build_csv_pack, device_key, option_whatif, replay,
                             replay_on)
from tradelab_engine.engine import load_library

GOLDEN_DIR = Path(__file__).resolve().parents[3] / "engine" / "tests" / "golden"
FORMAT = 1


def index() -> list[str]:
    doc = json.loads((GOLDEN_DIR / "index.json").read_text())
    assert doc["format"] == FORMAT
    return list(doc["fixtures"])


def load(name: str) -> dict[str, Any]:
    fx = json.loads((GOLDEN_DIR / f"{name}.json").read_text())
    assert fx["format"] == FORMAT and fx["name"] == name
    return fx


def csv_text(pack: dict[str, Any]) -> str:
    return "\n".join(pack["csv"]) + "\n"


def _pack_run(pack: dict[str, Any], seed: int) -> Run:
    result, data = build_csv_pack(csv_text(pack), pack["meta"], pack["deviceSeed"])
    assert result["ok"], result
    k = device_key(pack["deviceSeed"])
    return Run.from_pack(data, [{"id": k["id"], "key": k["publicKey"]}], seed)


def replay_run(fx: dict[str, Any]) -> ReplayResult:
    """Replays a "run" fixture: its market (a built-in mission or a CSV pack), seed, start options and
    action log (the web client's format, tradelab_engine/replay.py)."""
    assert fx["kind"] == "run"
    if "csvPack" in fx:
        with _pack_run(fx["csvPack"], fx["seed"]) as run:
            return replay_on(run, fx["actions"])
    return replay(fx["mission"], fx["seed"], fx["actions"], RunOptions.from_json(fx.get("options")))


def reveal_sha256(fx: dict[str, Any]) -> str:
    """SHA-256 of the exact bytes tl_mission_reveal returns for a "reveal" fixture."""
    assert fx["kind"] == "reveal"
    return hashlib.sha256(load_library().tl_mission_reveal(fx["mission"].encode(), fx["seed"])).hexdigest()


def whatif_result(fx: dict[str, Any]) -> dict[str, Any]:
    """tl_option_whatif's output for a "whatif" fixture's request (every value a string or null)."""
    assert fx["kind"] == "whatif"
    return option_whatif(fx["request"])


def actual(fx: dict[str, Any]) -> dict[str, Any]:
    """The values a fixture's "expected" block freezes, as produced by this engine build."""
    if fx["kind"] == "reveal":
        return {"sha256": reveal_sha256(fx)}
    if fx["kind"] == "whatif":
        return {"result": whatif_result(fx)}
    result = replay_run(fx)
    rejected = [{"action": i, "reason": r["reason"]} for i, r in enumerate(result.results)
                if r is not None and not r["accepted"]]
    return {"digest": result.state["digest"], "eventsDigest": result.state["eventsDigest"],
            "status": result.status, "rejected": rejected}
