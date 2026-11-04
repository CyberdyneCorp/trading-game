"""TradeLab engine binding: the same deterministic C++ core the clients run, via its C ABI.

The server uses it to re-simulate runs from their action logs and to compute the authoritative
score (spec game-api "Server re-simulation"). Never trust client-reported balances or scores.
"""

from .engine import (
    EngineError,
    Run,
    build_csv_pack,
    csv_validate,
    device_key,
    engine_version,
    load_library,
    missions,
    pack_validate,
)
from .replay import ACTION_KINDS, InvalidAction, ReplayResult, replay

__all__ = [
    "ACTION_KINDS",
    "EngineError",
    "InvalidAction",
    "ReplayResult",
    "Run",
    "build_csv_pack",
    "csv_validate",
    "device_key",
    "engine_version",
    "load_library",
    "missions",
    "pack_validate",
    "replay",
]
