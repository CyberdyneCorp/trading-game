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
        "tl_missions": (c_char_p, []),
        "tl_run_create": (c_void_p, [c_char_p, u32]),
        "tl_run_create_ex": (c_void_p, [c_char_p, u32, u32, u32, i32]),
        "tl_request_advice": (c_char_p, [c_void_p, u32]),
        "tl_run_destroy": (None, [c_void_p]),
        "tl_place_order_ex": (c_char_p, [c_void_p, u32, i32, i32, c_char_p, c_char_p, i32, c_char_p, c_char_p, c_char_p]),
        "tl_place_option_order": (c_char_p, [c_void_p, u32, u32, i32, i32, c_char_p, c_char_p]),
        "tl_cancel_order": (c_char_p, [c_void_p, u32, u32]),
        "tl_set_leverage": (c_char_p, [c_void_p, u32, i32]),
        "tl_close_position": (c_char_p, [c_void_p, u32]),
        "tl_adjust_margin": (c_char_p, [c_void_p, u32, c_char_p]),
        "tl_record_plan": (c_char_p, [c_void_p, u32, i32, i32, i32, i32, c_char_p, c_char_p, i32, c_char_p]),
        "tl_answer_question": (c_char_p, [c_void_p, u32, i32, i32]),
        "tl_checkpoint": (c_char_p, [c_void_p]),
        "tl_run_restore": (c_void_p, [c_char_p, u32, c_char_p]),
        "tl_advance": (c_char_p, [c_void_p, i32]),
        "tl_state": (c_char_p, [c_void_p]),
        "tl_debrief": (c_char_p, [c_void_p]),
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


def missions() -> list[dict[str, Any]]:
    return _json(load_library().tl_missions())


@dataclass(frozen=True)
class RunOptions:
    """Start options (execution policy v11). The defaults are a run without advisor or perks."""

    advice_allowance: int = 0
    news_lead: int = 0  # fast wire: headlines this many bars early (0..255)
    calendar: bool = False

    @classmethod
    def from_json(cls, data: Mapping[str, Any] | None) -> RunOptions:
        """Reads the client's run-record shape {"adviceAllowance", "newsLead", "calendar"}."""
        data = data or {}
        return cls(int(data.get("adviceAllowance", 0)), int(data.get("newsLead", 0)), bool(data.get("calendar", False)))


class Run:
    """One simulation run. Every method mirrors one C ABI call and returns its parsed JSON."""

    def __init__(self, mission_id: str, seed: int, options: RunOptions | None = None, *,
                 _handle: int | None = None) -> None:
        self._lib = load_library()
        o = options or RunOptions()
        handle = _handle if _handle is not None else self._lib.tl_run_create_ex(
            mission_id.encode(), seed, o.advice_allowance, o.news_lead, int(o.calendar))
        if not handle:
            raise EngineError(f"unknown mission {mission_id!r} or invalid start options")
        self._h = ctypes.c_void_p(handle)
        self.mission_id = mission_id
        self.seed = seed

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

    def place_option_order(self, cid: int, series: int, side: int, type_: int, qty: str, premium: str | None) -> Any:
        return _json(self._lib.tl_place_option_order(self._h, cid, series, side, type_, _s(qty), _s(premium)))

    def cancel(self, cid: int, order_id: int) -> Any:
        return _json(self._lib.tl_cancel_order(self._h, cid, order_id))

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
