"""TradeLab engine binding: the same deterministic C++ core the clients run, via its C ABI.

The server uses it to re-simulate runs from their action logs and to compute the authoritative
score (spec game-api "Server re-simulation"). Never trust client-reported balances or scores.
"""

from .engine import (
    EngineError,
    Run,
    RunOptions,
    build_csv_pack,
    csv_validate,
    device_key,
    engine_version,
    lesson,
    load_library,
    mission_reveal,
    missions,
    pack_resign,
    pack_source_csv,
    pack_source_mission,
    pack_validate,
    pack_write,
    signing_key,
)
from .replay import ACTION_KINDS, InvalidAction, ReplayResult, replay

__all__ = [
    "ACTION_KINDS",
    "EngineError",
    "InvalidAction",
    "ReplayResult",
    "Run",
    "RunOptions",
    "build_csv_pack",
    "csv_validate",
    "device_key",
    "engine_version",
    "lesson",
    "load_library",
    "mission_reveal",
    "missions",
    "pack_resign",
    "pack_source_csv",
    "pack_source_mission",
    "pack_validate",
    "pack_write",
    "replay",
    "signing_key",
]
