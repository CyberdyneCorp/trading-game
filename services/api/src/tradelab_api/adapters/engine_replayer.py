"""Replayer adapter over the Python engine binding (same deterministic core as the clients).

Replays with the installed (current) execution policy only; PolicyReplayer adds the older ones.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any

import tradelab_engine as te

from ..domain.errors import UnsupportedPolicy
from ..domain.models import Action


class EngineReplayer:
    def supported_policy(self) -> int:
        return int(te.engine_version()["executionPolicy"])

    def supported_policies(self) -> frozenset[int]:
        return frozenset({self.supported_policy()})

    def replay(self, mission_id: str, seed: int, actions: Sequence[Action],
               options: Mapping[str, Any] | None = None, policy: int | None = None) -> dict[str, Any]:
        if policy is not None and policy != self.supported_policy():
            raise UnsupportedPolicy()
        try:
            result = te.replay(mission_id, seed, actions, te.RunOptions.from_json(options))
        except (te.InvalidAction, te.EngineError, KeyError, TypeError, ValueError) as exc:
            raise ValueError(str(exc)) from exc
        return {"digest": result.digest, "status": result.status, "debrief": result.debrief}

    def missions(self) -> list[dict[str, Any]]:
        return te.missions()


ACTION_KINDS = te.ACTION_KINDS
