"""Deterministic replay of a client action log (the format apps/web stores in IndexedDB).

Action kinds and fields (client ids are assigned 1, 2, ... to every non-"adv" action, exactly as
the web client does, so a replay reproduces the client's run bit for bit):

  {"k":"lev","lev":3}
  {"k":"order","type":"market|limit|stop_market|take_profit|trailing_stop","side":"buy|sell","qty":"1.0",
   "price":null,"ro":false,"stop":null,"tp":null,"trail":null}
  {"k":"opt","series":5,"side":"buy","type":"market|limit","qty":"1.0","premium":null}
  {"k":"margin","amount":"100.00"}
  {"k":"plan","side":"buy","skipped":false,"option":false,"lose":false,"inv":"95.50","target":null,"conf":60,"risk":"100.00"}
  {"k":"ans","pBp":6000,"skipped":false}
  {"k":"cancel","id":3}
  {"k":"move","id":3,"price":"95.50"}   (policy v13: a working stop/take-profit moved in place)
  {"k":"close"}
  {"k":"advice"}
  {"k":"adv","n":5}

Start options (advice allowance, perks) are not actions: pass the run record's options to replay().
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable, Mapping, Sequence

from .engine import Run, RunOptions

_ORDER_TYPES = {"market": 0, "limit": 1, "stop_market": 2, "take_profit": 3, "trailing_stop": 4}
_SIDES = {"buy": 0, "sell": 1}
MAX_ADVANCE = 10_000


class InvalidAction(ValueError):
    """The action log contains a malformed action (rejected before touching the engine)."""


def _side(a: Mapping[str, Any]) -> int:
    try:
        return _SIDES[a["side"]]
    except KeyError as exc:
        raise InvalidAction(f"bad side in {a!r}") from exc


def _order(run: Run, cid: int, a: Mapping[str, Any]) -> Any:
    type_ = _ORDER_TYPES.get(a.get("type", ""))
    if type_ is None:
        raise InvalidAction(f"bad order type in {a!r}")
    return run.place_order(cid, type_, _side(a), str(a["qty"]), a.get("price"), bool(a.get("ro", False)),
                           a.get("stop"), a.get("tp"), a.get("trail"))


def _opt(run: Run, cid: int, a: Mapping[str, Any]) -> Any:
    type_ = {"market": 0, "limit": 1}.get(a.get("type", ""))
    if type_ is None:
        raise InvalidAction(f"bad option order type in {a!r}")
    return run.place_option_order(cid, int(a["series"]), _side(a), type_, str(a["qty"]), a.get("premium"))


def _plan(run: Run, cid: int, a: Mapping[str, Any]) -> Any:
    return run.record_plan(cid, _side(a), bool(a.get("skipped")), bool(a.get("option")), bool(a.get("lose")),
                           a.get("inv"), a.get("target"), int(a.get("conf", 0)), a.get("risk"))


_HANDLERS: dict[str, Callable[[Run, int, Mapping[str, Any]], Any]] = {
    "lev": lambda run, cid, a: run.set_leverage(cid, int(a["lev"])),
    "order": _order,
    "opt": _opt,
    "margin": lambda run, cid, a: run.adjust_margin(cid, str(a["amount"])),
    "plan": _plan,
    "ans": lambda run, cid, a: run.answer_question(cid, int(a.get("pBp", 0)), bool(a.get("skipped"))),
    "cancel": lambda run, cid, a: run.cancel(cid, int(a["id"])),
    "move": lambda run, cid, a: run.move_order(cid, int(a["id"]), str(a["price"])),
    "close": lambda run, cid, a: run.close_position(cid),
    "advice": lambda run, cid, a: run.request_advice(cid),
}
ACTION_KINDS = frozenset([*_HANDLERS, "adv"])


@dataclass(frozen=True)
class ReplayResult:
    state: dict[str, Any]
    debrief: dict[str, Any] | None  # None while the run is still active
    results: list[Any]              # command results in order (None for advances)

    @property
    def digest(self) -> str:
        return self.state["digest"]

    @property
    def status(self) -> str:
        return self.state["status"]


def _apply(run: Run, a: Mapping[str, Any], cid: int) -> tuple[Any, int]:
    kind = a.get("k")
    if kind == "adv":
        n = int(a.get("n", 1))
        if not 1 <= n <= MAX_ADVANCE:
            raise InvalidAction(f"bad advance count in {a!r}")
        run.advance(n)
        return None, cid
    handler = _HANDLERS.get(kind)  # type: ignore[arg-type]
    if handler is None:
        raise InvalidAction(f"unknown action kind {kind!r}")
    return handler(run, cid, a), cid + 1


def replay(mission_id: str, seed: int, actions: Sequence[Mapping[str, Any]],
           options: RunOptions | None = None) -> ReplayResult:
    """Re-simulates a run from scratch and returns its final state (and debrief when finished)."""
    with Run(mission_id, seed, options) as run:
        cid = 1
        results: list[Any] = []
        for a in actions:
            res, cid = _apply(run, a, cid)
            results.append(res)
        state = run.state()
        debrief = run.debrief() if state["status"] != "active" else None
        return ReplayResult(state=state, debrief=debrief, results=results)
