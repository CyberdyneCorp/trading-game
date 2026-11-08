"""Execution policy v18 through the binding: time in force, OCO links and replay tags
(add-practice-and-advanced-strategies D3, D6, D7)."""

from __future__ import annotations

import pytest

from tradelab_engine import Run, engine_version
from tradelab_engine.replay import replay

MISSION, SEED = "m01-false-breakout", 3


def _order(type_: str, side: str, qty: str, price: str | None = None, **extra: object) -> dict[str, object]:
    action: dict[str, object] = {"k": "order", "type": type_, "side": side, "qty": qty, "price": price, "ro": False,
                                 "stop": None, "tp": None, "trail": None}
    action.update(extra)
    return action


def test_policy_version() -> None:
    assert engine_version()["executionPolicy"] == 18


def test_place_order_tif_and_refusals() -> None:
    with Run(MISSION, SEED) as run:
        assert run.place_order(1, 1, 0, "10", "99.5", False, tif="bars", tif_bars=3)["accepted"]
        assert run.place_order(2, 0, 0, "10", None, False, tif="ioc")["reason"] == "invalid_time_in_force"
        assert run.place_order(3, 1, 0, "10", "99", False, tif="bars", tif_bars=0)["reason"] == "invalid_time_in_force"
        order = next(o for o in run.state()["orders"] if o["id"] == 1)
        assert (order["tif"], order["tifBars"], order["expireStep"]) == ("bars", 3, 63)
        run.advance(3)
        assert all(o["id"] != 1 for o in run.state()["orders"])
        expired = [e for e in run.state()["events"] if e["orderId"] == 1 and e["reason"] == "time_in_force"]
        assert expired and expired[0]["status"] == "expired" and expired[0]["step"] == 63
    with pytest.raises(ValueError):
        with Run(MISSION, SEED) as run:
            run.place_order(1, 1, 0, "10", "99", False, tif="day")


def test_link_oco_one_of_two_stops_fills() -> None:
    with Run(MISSION, SEED) as run:
        run.advance(2)  # step 62: bar 63 trades from 99.70 to 101.03
        assert run.place_order(1, 2, 0, "10", "100.9", False)["accepted"]
        assert run.place_order(2, 2, 1, "10", "99.8", False)["accepted"]
        link = run.link_oco(3, 1, 2)
        assert link["accepted"] and link["orderId"] == 1
        assert run.link_oco(4, 1, 2)["reason"] == "invalid_oco"
        assert {o["linkGroup"] for o in run.state()["orders"]} == {1}
        state = run.advance(1)
        fills = [e for e in state["events"] if e["type"] == "fill" and e["orderId"] in (1, 2)]
        assert len(fills) == 1 and fills[0]["orderId"] == 1
        assert any(e["orderId"] == 2 and e["reason"] == "oco_sibling_filled" for e in state["events"])


def test_option_order_tif() -> None:
    with Run("m09-event-straddle", 3) as run:
        series = run.state()["options"]["chain"][0]["id"]
        res = run.place_option_order(1, series, 0, 1, "1", "0.01", tif="ioc")
        assert res["accepted"]
        state = run.advance(1)
        assert any(e["orderId"] == res["orderId"] and e["reason"] == "time_in_force" for e in state["events"])
        assert run.place_option_order(2, series, 0, 0, "1", None, tif="fok")["reason"] == "invalid_time_in_force"


def test_tagged_log_replays_to_the_untagged_digest() -> None:
    plain = [
        {"k": "lev", "lev": 3},
        _order("limit", "buy", "10", "101.5", tif="ioc"),
        _order("stop_market", "sell", "10", "99.8"),
        {"k": "adv", "n": 1},
        {"k": "cancel", "id": 2},
        {"k": "adv", "n": 2},
    ]
    tags = [{}, {"combo": "c1", "roll": "1"}, {"combo": "c1", "adjust": "2"}, {}, {"combo": "c1", "adjust": "3"}, {}]
    tagged = [{**a, **t} for a, t in zip(plain, tags)]
    assert replay(MISSION, SEED, tagged).digest == replay(MISSION, SEED, plain).digest
    assert replay(MISSION, SEED, tagged).state["eventsDigest"] == replay(MISSION, SEED, plain).state["eventsDigest"]


def test_replay_applies_oco_and_tif_actions() -> None:
    actions = [
        {"k": "adv", "n": 2},
        _order("stop_market", "buy", "10", "100.9"),
        _order("stop_market", "sell", "10", "99.8", tif="bars", tifBars=2),
        {"k": "oco", "a": 1, "b": 2},
        {"k": "adv", "n": 1},
    ]
    result = replay(MISSION, SEED, actions)
    assert all(r is None or r["accepted"] for r in result.results)
    assert result.state["position"]["qty"] == "10.00000000"
