"""TradeLab engine binding: the same deterministic C++ core the clients run, via its C ABI.

The server uses it to re-simulate runs from their action logs and to compute the authoritative
score (spec game-api "Server re-simulation"). Never trust client-reported balances or scores.
"""

from .engine import (
    EngineError,
    Run,
    RunOptions,
    abi_version,
    build_csv_pack,
    csv_validate,
    device_key,
    engine_version,
    lesson,
    load_library,
    mission_reveal,
    missions,
    option_whatif,
    pack_resign,
    pack_source_csv,
    pack_source_mission,
    pack_validate,
    pack_write,
    seal_run_file,
    signing_key,
    verify_run_file,
)
from .replay import ACTION_KINDS, InvalidAction, ReplayResult, replay, replay_on

__all__ = [
    "ACTION_KINDS",
    "EngineError",
    "InvalidAction",
    "ReplayResult",
    "Run",
    "RunOptions",
    "abi_version",
    "build_csv_pack",
    "csv_validate",
    "device_key",
    "engine_version",
    "lesson",
    "load_library",
    "mission_reveal",
    "missions",
    "option_whatif",
    "pack_resign",
    "pack_source_csv",
    "pack_source_mission",
    "pack_validate",
    "pack_write",
    "seal_run_file",
    "replay",
    "replay_on",
    "signing_key",
    "verify_run_file",
]
