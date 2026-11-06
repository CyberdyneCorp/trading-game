"""Engine adapters over the Python binding (same deterministic core as the clients).

The engine's C ABI returns JSON through process-wide buffers, so calls are serialized with one lock:
FastAPI runs plain `def` endpoints on a thread pool.
"""

from __future__ import annotations

import threading
from collections.abc import Mapping, Sequence
from typing import Any

import tradelab_engine as te

from ..domain.models import Action

ENGINE_LOCK = threading.Lock()


class EngineReplayer:
    def supported_policy(self) -> int:
        with ENGINE_LOCK:
            return int(te.engine_version()["executionPolicy"])

    def replay(self, mission_id: str, seed: int, actions: Sequence[Action],
               options: Mapping[str, Any] | None = None) -> dict[str, Any]:
        try:
            with ENGINE_LOCK:
                result = te.replay(mission_id, seed, actions, te.RunOptions.from_json(options))
        except (te.InvalidAction, te.EngineError, KeyError, TypeError, ValueError) as exc:
            raise ValueError(str(exc)) from exc
        return {"digest": result.digest, "status": result.status, "debrief": result.debrief}

    def missions(self) -> list[dict[str, Any]]:
        with ENGINE_LOCK:
            return te.missions()


class EngineCsvValidator:
    """The engine's CSV import report (`tl_csv_validate`): the same rules, codes and limits as every
    client's import wizard."""

    def validate(self, csv: str, meta: Mapping[str, Any]) -> dict[str, Any]:
        with ENGINE_LOCK:
            return te.csv_validate(csv, dict(meta))


ACTION_KINDS = te.ACTION_KINDS
