import pytest

from tradelab_engine import EngineError, InvalidAction, Run, RunOptions, engine_version, missions, replay


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
    assert state["digest"] == "42fb769798588fd66737e4c975dbbd0f9e4fc56ea9d14ae70a2d95b2853533ad"
    assert state["eventsDigest"] == "ac136e4b4ff59a6b7b2ae93973250bf79f3cc82d22187ffae13be1d4e9f63f19"


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
    assert a.debrief["score"]["version"] == 6
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


def test_cross_binding_golden_with_advice_matches_native_cpp():
    """Same calls as the C++ test 'Golden: scripted run with start options and advice'."""
    with Run("m02-funding-squeeze", 2026, RunOptions(advice_allowance=3, news_lead=1, calendar=True)) as run:
        assert run.request_advice(1)["accepted"]
        run.set_leverage(2, 3)
        run.record_plan(3, 0, False, False, True, "95", "115", 60, "100")
        run.place_order(4, 0, 0, "50", None, False, "95", "115")
        run.advance(25)
        run.request_advice(5)
        state = run.advance(1000)
    assert state["advisor"]["left"] == 1
    assert len(state["advisor"]["advice"]) == 2
    assert state["digest"] == "8921cab669fc115f6990231395a50d0d76f71d01022cf5826f40399b4e2fb5f4"
    assert state["eventsDigest"] == "c83e5525e4d03ad71d5eafcef3af65a2e1d43798c5303ec43e51cbc89e7a888f"


def test_replay_with_advice_and_options():
    log = [{"k": "advice"}, {"k": "adv", "n": 5}, {"k": "advice"}, {"k": "advice"}, {"k": "adv", "n": 500}]
    opts = RunOptions.from_json({"adviceAllowance": 2, "newsLead": 0, "calendar": False})
    r = replay("m01-false-breakout", 3, log, opts)
    assert [x["accepted"] for x in r.results if x is not None] == [True, True, False]
    assert r.results[3]["reason"] == "no_advice_left"
    assert r.debrief is not None
    assert len(r.debrief["advice"]) == 2
    assert r.debrief["advisorPointsEarned"] == 2
    # Without options the allowance is 0: every request is rejected and the run is the default one.
    plain = replay("m01-false-breakout", 3, log)
    assert all(not x["accepted"] for x in plain.results if x is not None)
    assert plain.digest == replay("m01-false-breakout", 3, [{"k": "adv", "n": 505}]).digest
