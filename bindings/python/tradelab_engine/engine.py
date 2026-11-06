"""ctypes wrapper over engine/include/tradelab/c_api.h (one Run = one tl_run handle)."""

from __future__ import annotations

import ctypes
import json
import os
import sys
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any, Mapping

_LIB_NAMES = {"darwin": "libtradelab.dylib", "win32": "tradelab.dll"}


class EngineError(RuntimeError):
    """Raised when the engine library is missing or a run cannot be created/restored."""


def _candidates() -> list[Path]:
    name = _LIB_NAMES.get(sys.platform, "libtradelab.so")
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    env = os.environ.get("TRADELAB_LIB")
    paths = [Path(env)] if env else []
    paths.append(here / name)
    for preset in ("native-release", "native-debug"):
        paths.append(repo / "engine" / "build" / preset / name)
    return paths


@lru_cache(maxsize=1)
def load_library() -> ctypes.CDLL:
    """Loads libtradelab (TRADELAB_LIB, the package directory, then the repo build directories)."""
    for path in _candidates():
        if path.is_file():
            return _declare(ctypes.CDLL(str(path)))
    raise EngineError("libtradelab not found; build engine target tradelab_capi or set TRADELAB_LIB")


def _declare(lib: ctypes.CDLL) -> ctypes.CDLL:
    c_char_p, c_void_p, u32, i32 = ctypes.c_char_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int32
    signatures: dict[str, tuple[Any, list[Any]]] = {
        "tl_version": (c_char_p, []),
        "tl_abi_version": (u32, []),
        "tl_missions": (c_char_p, []),
        "tl_run_create": (c_void_p, [c_char_p, u32]),
        "tl_run_create_ex": (c_void_p, [c_char_p, u32, u32, u32, i32, c_char_p, i32]),
        "tl_request_advice": (c_char_p, [c_void_p, u32]),
        "tl_run_destroy": (None, [c_void_p]),
        "tl_place_order_ex": (c_char_p, [c_void_p, u32, i32, i32, c_char_p, c_char_p, i32, c_char_p, c_char_p, c_char_p]),
        "tl_place_trailing_rate": (c_char_p, [c_void_p, u32, i32, c_char_p, i32, c_char_p]),
        "tl_place_option_order": (c_char_p, [c_void_p, u32, u32, i32, i32, c_char_p, c_char_p]),
        "tl_cancel_order": (c_char_p, [c_void_p, u32, u32]),
        "tl_move_order": (c_char_p, [c_void_p, u32, u32, c_char_p]),
        "tl_set_leverage": (c_char_p, [c_void_p, u32, i32]),
        "tl_close_position": (c_char_p, [c_void_p, u32]),
        "tl_adjust_margin": (c_char_p, [c_void_p, u32, c_char_p]),
        "tl_record_plan": (c_char_p, [c_void_p, u32, i32, i32, i32, i32, c_char_p, c_char_p, i32, c_char_p]),
        "tl_answer_question": (c_char_p, [c_void_p, u32, i32, i32]),
        "tl_answer_lesson": (c_char_p, [c_void_p, u32, u32, u32, c_char_p, i32]),
        "tl_lesson": (c_char_p, [c_char_p]),
        "tl_checkpoint": (c_char_p, [c_void_p]),
        "tl_run_restore": (c_void_p, [c_char_p, u32, c_char_p]),
        "tl_advance": (c_char_p, [c_void_p, i32]),
        "tl_state": (c_char_p, [c_void_p]),
        "tl_debrief": (c_char_p, [c_void_p]),
        "tl_mission_reveal": (c_char_p, [c_char_p, u32]),
        "tl_csv_validate": (c_char_p, [c_char_p, c_char_p]),
        "tl_pack_build_csv": (c_char_p, [c_char_p, c_char_p, c_char_p]),
        "tl_blob_data": (ctypes.POINTER(ctypes.c_uint8), []),
        "tl_blob_size": (u32, []),
        "tl_device_key": (c_char_p, [c_char_p]),
        "tl_pack_validate": (c_char_p, [c_char_p, u32, c_char_p, i32]),
        "tl_run_create_pack": (c_void_p, [c_char_p, u32, c_char_p, u32]),
        "tl_run_restore_pack": (c_void_p, [c_char_p, u32, c_char_p, u32, c_char_p]),
        "tl_pack_source_mission": (c_char_p, [c_char_p, u32]),
        "tl_pack_source_csv": (c_char_p, [c_char_p, c_char_p]),
        "tl_pack_write": (c_char_p, [c_char_p, c_char_p, u32, c_char_p, c_char_p]),
        "tl_pack_resign": (c_char_p, [c_char_p, u32, c_char_p, c_char_p]),
        "tl_signing_key": (c_char_p, [c_char_p, c_char_p]),
    }
    for name, (restype, argtypes) in signatures.items():
        fn = getattr(lib, name)
        fn.restype = restype
        fn.argtypes = argtypes
    return lib


def _json(raw: bytes) -> Any:
    return json.loads(raw.decode("utf-8"))


def _s(value: str | None) -> bytes | None:
    return None if value is None else value.encode("utf-8")


def engine_version() -> dict[str, Any]:
    return _json(load_library().tl_version())


def abi_version() -> int:
    """The engine's C ABI version (tl_abi_version)."""
    return int(load_library().tl_abi_version())


def missions() -> list[dict[str, Any]]:
    return _json(load_library().tl_missions())


def mission_reveal(mission_id: str, seed: int) -> dict[str, Any]:
    """G4 seeded reveal of a built-in mission: regime spans as played and event outcomes, or
    {"error": "unknown_mission"}. It describes a run's future: only for finished runs (design D6)."""
    return _json(load_library().tl_mission_reveal(mission_id.encode(), seed))


def lesson(mission_id: str) -> dict[str, Any]:
    """The mission's lesson questions without the answer key (task 4.5), or {"error": "no_lesson"}."""
    return _json(load_library().tl_lesson(mission_id.encode()))


# ---- scenario packs and CSV import (spec scenario-packs) ----

TrustedKeys = list[dict[str, str]]  # [{"id": ..., "key": "<64 hex>"}]


def csv_validate(csv: str, meta: dict[str, Any]) -> dict[str, Any]:
    """Engine CSV report (same rules and codes as every client's import wizard)."""
    return _json(load_library().tl_csv_validate(csv.encode(), json.dumps(meta).encode()))


def device_key(seed_hex: str) -> dict[str, str]:
    """{"id", "publicKey"} of the Ed25519 key derived from a 32-byte seed (64 hex)."""
    return _json(load_library().tl_device_key(seed_hex.encode()))


def build_csv_pack(csv: str, meta: dict[str, Any], seed_hex: str) -> tuple[dict[str, Any], bytes]:
    """Builds a private, masked pack signed with the seed's key; returns (result, pack bytes)."""
    lib = load_library()
    result = _json(lib.tl_pack_build_csv(csv.encode(), json.dumps(meta).encode(), seed_hex.encode()))
    data = ctypes.string_at(lib.tl_blob_data(), lib.tl_blob_size()) if result["ok"] else b""
    return result, data


def pack_validate(pack: bytes, trusted: TrustedKeys, *, publish: bool = False) -> dict[str, Any]:
    """Verifies a pack for install or publishing (LIC01): {"ok", "reason", "detail", "pack"}."""
    return _json(load_library().tl_pack_validate(pack, len(pack), json.dumps(trusted).encode(), int(publish)))


# ---- pack authoring (tools/packer, design D5) ----

def _with_blob(raw: bytes) -> tuple[dict[str, Any], bytes]:
    lib = load_library()
    result = _json(raw)
    data = ctypes.string_at(lib.tl_blob_data(), lib.tl_blob_size()) if result["ok"] else b""
    return result, data


def pack_source_mission(mission_id: str, seed: int) -> tuple[dict[str, Any], bytes]:
    """A built-in mission generated for a seed as a synthetic pack source: (result, archive of
    manifest.json and the uncompressed data files)."""
    return _with_blob(load_library().tl_pack_source_mission(mission_id.encode(), seed))


def pack_source_csv(csv: str, meta: dict[str, Any]) -> tuple[dict[str, Any], bytes]:
    """A masked CSV window as a publishable historical (or, with branchStep/branchMission/branchSeed
    in `meta`, branch) pack source: (result, archive)."""
    return _with_blob(load_library().tl_pack_source_csv(csv.encode(), json.dumps(meta).encode()))


def pack_write(manifest: str, files_tar: bytes, seed_hex: str, key_prefix: str) -> tuple[dict[str, Any], bytes]:
    """Signs and writes a pack from a manifest and an archive of stored files (.bin = zstd frames
    declaring their content size): (result, pack bytes)."""
    lib = load_library()
    return _with_blob(lib.tl_pack_write(manifest.encode(), files_tar, len(files_tar), seed_hex.encode(), key_prefix.encode()))


def pack_resign(pack: bytes, seed_hex: str, key_prefix: str) -> tuple[dict[str, Any], bytes]:
    """Signs a non-private pack again with another key: (result, pack bytes)."""
    return _with_blob(load_library().tl_pack_resign(pack, len(pack), seed_hex.encode(), key_prefix.encode()))


def signing_key(seed_hex: str, key_prefix: str) -> dict[str, str]:
    """{"id": "<prefix>-<16 hex>", "publicKey"} of the key a seed signs with."""
    return _json(load_library().tl_signing_key(seed_hex.encode(), key_prefix.encode()))
@dataclass(frozen=True)
class RunOptions:
    """Start options (execution policy v11; v12 adds the campaign fields). The defaults are a plain
    run with the mission's own rules, without advisor or perks."""

    advice_allowance: int = 0
    news_lead: int = 0  # fast wire: headlines this many bars early (0..255)
    calendar: bool = False
    starting_balance: str | None = None  # campaign capital carried in (decimal > 0); None = the mission's
    leverage_cap: int = 0  # > 0 lowers the mission's leverage cap (curriculum unlocks)

    @classmethod
    def from_json(cls, data: Mapping[str, Any] | None) -> RunOptions:
        """Reads the client's run-record shape {"adviceAllowance", "newsLead", "calendar",
        "startingBalance", "leverageCap"}."""
        data = data or {}
        balance = data.get("startingBalance")
        return cls(int(data.get("adviceAllowance", 0)), int(data.get("newsLead", 0)), bool(data.get("calendar", False)),
                   str(balance) if balance else None, int(data.get("leverageCap", 0)))


class Run:
    """One simulation run. Every method mirrors one C ABI call and returns its parsed JSON."""

    def __init__(self, mission_id: str, seed: int, options: RunOptions | None = None, *,
                 _handle: int | None = None) -> None:
        self._lib = load_library()
        o = options or RunOptions()
        handle = _handle if _handle is not None else self._lib.tl_run_create_ex(
            mission_id.encode(), seed, o.advice_allowance, o.news_lead, int(o.calendar),
            o.starting_balance.encode() if o.starting_balance else None, o.leverage_cap)
        if not handle:
            raise EngineError(f"unknown mission {mission_id!r} or invalid start options")
        self._h = ctypes.c_void_p(handle)
        self.mission_id = mission_id
        self.seed = seed

    @classmethod
    def from_pack(cls, pack: bytes, trusted: TrustedKeys, seed: int) -> Run:
        """A run on a verified scenario pack; EngineError (with the reason) when it fails validation."""
        handle = load_library().tl_run_create_pack(pack, len(pack), json.dumps(trusted).encode(), seed)
        if not handle:
            raise EngineError(pack_validate(pack, trusted)["reason"] or "pack rejected")
        return cls("pack", seed, _handle=handle)

    @classmethod
    def restore_pack(cls, pack: bytes, trusted: TrustedKeys, seed: int, checkpoint_hex: str) -> Run:
        handle = load_library().tl_run_restore_pack(pack, len(pack), json.dumps(trusted).encode(), seed,
                                                    checkpoint_hex.encode())
        if not handle:
            raise EngineError("pack rejected, or checkpoint damaged or for another scenario")
        return cls("pack", seed, _handle=handle)

    @classmethod
    def restore(cls, mission_id: str, seed: int, checkpoint_hex: str) -> Run:
        handle = load_library().tl_run_restore(mission_id.encode(), seed, checkpoint_hex.encode())
        if not handle:
            raise EngineError("checkpoint is damaged or incompatible with this engine")
        return cls(mission_id, seed, _handle=handle)  # start options come back with the checkpoint

    def close(self) -> None:
        if self._h:
            self._lib.tl_run_destroy(self._h)
            self._h = ctypes.c_void_p()

    def __enter__(self) -> Run:
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def __del__(self) -> None:  # best effort; prefer `with Run(...)`
        try:
            self.close()
        except Exception:  # noqa: BLE001
            pass

    # --- commands (client_id is the idempotency key, assigned by the caller) ---
    def place_order(self, cid: int, type_: int, side: int, qty: str, price: str | None, reduce_only: bool,
                    stop: str | None = None, tp: str | None = None, trail: str | None = None) -> Any:
        return _json(self._lib.tl_place_order_ex(self._h, cid, type_, side, _s(qty), _s(price), int(reduce_only),
                                                 _s(stop), _s(tp), _s(trail)))

    def place_trailing_rate(self, cid: int, side: int, qty: str, reduce_only: bool, rate: str) -> Any:
        """Trailing stop at a rate of the best close ("0.02" = 2%; policy v16)."""
        return _json(self._lib.tl_place_trailing_rate(self._h, cid, side, _s(qty), int(reduce_only), _s(rate)))

    def place_option_order(self, cid: int, series: int, side: int, type_: int, qty: str, premium: str | None) -> Any:
        return _json(self._lib.tl_place_option_order(self._h, cid, series, side, type_, _s(qty), _s(premium)))

    def cancel(self, cid: int, order_id: int) -> Any:
        return _json(self._lib.tl_cancel_order(self._h, cid, order_id))

    def move_order(self, cid: int, order_id: int, price: str) -> Any:
        return _json(self._lib.tl_move_order(self._h, cid, order_id, _s(price)))

    def set_leverage(self, cid: int, leverage: int) -> Any:
        return _json(self._lib.tl_set_leverage(self._h, cid, leverage))

    def close_position(self, cid: int) -> Any:
        return _json(self._lib.tl_close_position(self._h, cid))

    def adjust_margin(self, cid: int, amount: str) -> Any:
        return _json(self._lib.tl_adjust_margin(self._h, cid, _s(amount)))

    def record_plan(self, cid: int, side: int, skipped: bool, option: bool, lose: bool, invalidation: str | None,
                    target: str | None, confidence: int, risk: str | None) -> Any:
        return _json(self._lib.tl_record_plan(self._h, cid, side, int(skipped), int(option), int(lose),
                                              _s(invalidation), _s(target), confidence, _s(risk)))

    def answer_question(self, cid: int, p_bp: int, skipped: bool) -> Any:
        return _json(self._lib.tl_answer_question(self._h, cid, p_bp, int(skipped)))

    def answer_lesson(self, cid: int, question: int, choice: int, value: str | None, skipped: bool) -> Any:
        return _json(self._lib.tl_answer_lesson(self._h, cid, question, choice, _s(value), int(skipped)))

    def request_advice(self, cid: int) -> Any:
        return _json(self._lib.tl_request_advice(self._h, cid))

    def advance(self, steps: int) -> Any:
        return _json(self._lib.tl_advance(self._h, steps))

    def state(self) -> Any:
        return _json(self._lib.tl_state(self._h))

    def debrief(self) -> Any:
        return _json(self._lib.tl_debrief(self._h))

    def checkpoint(self) -> str:
        return _json(self._lib.tl_checkpoint(self._h))["checkpoint"]
