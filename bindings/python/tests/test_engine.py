import pytest

from tradelab_engine import EngineError, InvalidAction, Run, engine_version, missions, replay


def test_version_and_catalog():
    assert engine_version()["executionPolicy"] >= 4
    ids = {m["id"] for m in missions()}
    assert {"m01-false-breakout", "m04-options-buyer"} <= ids


def test_cross_binding_golden_matches_native_cpp():
    """Same calls as the C++ test 'Golden: scripted mission run' => identical digests."""
    with Run("m01-false-breakout", 2026) as run:
        run.set_leverage(1, 5)
        run.place_order(2, 0, 0, "120", None, False)
        run.advance(3)
        run.place_order(3, 2, 1, "120", "99", True)
        run.place_order(4, 3, 1, "60", "104", True)
        run.advance(20)
        run.place_order(5, 0, 1, "200", None, False)
        run.advance(30)
        run.close_position(6)
        state = run.advance(1000)
    assert state["digest"] == "11bcf66b67311307fde84af95ff72be7ef41622b201fba71d432ad01188b4965"
    assert state["eventsDigest"] == "f5902b5a8963380652e1c4e718db648e97c314ea1ea089881ad2ab73f9cde0a7"


def test_replay_action_log_reproduces_and_scores():
    log = [
        {"k": "ans", "pBp": 6000, "skipped": False},
        {"k": "plan", "side": "buy", "skipped": False, "option": False, "lose": False, "inv": "95.50",
         "target": None, "conf": 60, "risk": "100.00"},
        {"k": "order", "type": "market", "side": "buy", "qty": "10.0", "price": None, "ro": False,
         "stop": "95.50", "tp": "110.00", "trail": None},
        {"k": "adv", "n": 200},
    ]
    a = replay("m01-false-breakout", 5, log)
    b = replay("m01-false-breakout", 5, log)
    assert a.digest == b.digest
    assert a.status in {"finished", "bankrupt"}
    assert a.debrief is not None
    assert a.debrief["score"]["version"] == 2
    assert a.debrief["trades"][0]["n"] == 1
    assert [r["accepted"] for r in a.results if r is not None] == [True, True, True]


def test_checkpoint_round_trip():
    with Run("m03-volatility-shock", 9) as run:
        run.advance(5)
        cp = run.checkpoint()
        expected = run.advance(1000)["digest"]
    with Run.restore("m03-volatility-shock", 9, cp) as restored:
        assert restored.advance(1000)["digest"] == expected
    with pytest.raises(EngineError):
        Run.restore("m01-false-breakout", 9, cp)


def test_invalid_actions_are_rejected_before_the_engine():
    with pytest.raises(InvalidAction):
        replay("m01-false-breakout", 1, [{"k": "nope"}])
    with pytest.raises(InvalidAction):
        replay("m01-false-breakout", 1, [{"k": "adv", "n": 0}])
    with pytest.raises(InvalidAction):
        replay("m01-false-breakout", 1, [{"k": "order", "type": "x", "side": "buy", "qty": "1"}])
    with pytest.raises(EngineError):
        Run("unknown", 1)
