"""Replay worker for one older execution policy (runs in its own process).

The process imports the binding and library built from the commit of that policy (see
tools/build_policy_cache.py), never the server's current binding, so each policy keeps its own C ABI
and action-log reader. This module must not import `tradelab_engine` at module level.
"""

from __future__ import annotations

import importlib
import os
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any

_engine: Any = None


def init(policy_dir: str, library: str, policy: int) -> None:
    """Process initializer: load the cached binding of `policy` and check the library reports it."""
    global _engine
    if "tradelab_engine" in sys.modules:
        raise RuntimeError("a tradelab_engine binding is already loaded in this worker")
    os.environ["TRADELAB_LIB"] = library
    sys.path.insert(0, policy_dir)
    _engine = importlib.import_module("tradelab_engine")
    if not Path(_engine.__file__).resolve().is_relative_to(Path(policy_dir).resolve()):
        raise RuntimeError(f"imported {_engine.__file__} instead of the cached binding in {policy_dir}")
    reported = int(_engine.engine_version()["executionPolicy"])
    if reported != policy:
        raise RuntimeError(f"{library} reports execution policy {reported}, expected {policy}")


def replay(mission_id: str, seed: int, actions: Sequence[Mapping[str, Any]],
           options: Mapping[str, Any] | None) -> dict[str, Any]:
    """{digest, status, debrief}, or {error} when this policy's engine refuses the log."""
    te = _engine
    try:
        if options:
            result = te.replay(mission_id, seed, list(actions), te.RunOptions.from_json(options))
        else:
            result = te.replay(mission_id, seed, list(actions))
    except Exception as exc:  # noqa: BLE001 - isolation boundary: every engine refusal is a bad log
        return {"error": f"{type(exc).__name__}: {exc}"}
    return {"digest": result.digest, "status": result.status, "debrief": result.debrief}
