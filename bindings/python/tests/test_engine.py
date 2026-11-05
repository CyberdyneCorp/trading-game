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
    assert state["digest"] == "47717ee74f4985aaff00132ddf635f744c5bcbf6707ab295daa2ff2a22cadf9f"
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
    assert a.debrief["score"]["version"] == 9
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
    assert state["digest"] == "9f95179e529dedbb9d62308206d136d6a922fcdef70e2b4a8bb6d20acd0ac21d"
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


def test_campaign_options_carry_capital_and_gate_leverage():
    """Policy v12: a campaign mission starts from the carried capital, with the curriculum's leverage cap."""
    opts = RunOptions.from_json({"adviceAllowance": 0, "newsLead": 0, "calendar": False,
                                 "startingBalance": "8000.5", "leverageCap": 2})
    assert opts.starting_balance == "8000.5" and opts.leverage_cap == 2
    with Run("m01-false-breakout", 5, opts) as run:
        state = run.state()
        assert state["initialBalance"].startswith("8000.5")
        assert state["contract"]["leverageCap"] == 2
        assert run.set_leverage(1, 3)["reason"] == "leverage_above_cap"
        state = run.advance(10000)
        assert run.debrief()["finalEquity"].startswith("8000.5")
    r = replay("m01-false-breakout", 5, [{"k": "lev", "lev": 3}, {"k": "adv", "n": 500}], opts)
    assert r.results[0]["reason"] == "leverage_above_cap"
    assert r.digest == state["digest"]
    with pytest.raises(EngineError):
        Run("m01-false-breakout", 5, RunOptions(starting_balance="0"))


def test_intel_perks_in_the_state_view_leave_the_market_unchanged():
    """Fast wire shows a headline early (flagged) and the calendar lists the rate decision's bar only."""
    with Run("m02-funding-squeeze", 2026, RunOptions(news_lead=1, calendar=True)) as intel, \
            Run("m02-funding-squeeze", 2026) as plain:
        cal = intel.state()["calendar"]
        assert len(cal) == 1 and plain.state()["calendar"] == []
        rate = cal[0]
        a = plain.advance(rate - 1 - plain.state()["step"])
        b = intel.advance(rate - 1 - intel.state()["step"])
        early = [n for n in b["news"] if n["early"]]
        assert [n["kind"] for n in early] == ["rate_decision"] and early[0]["step"] == rate
        assert all(n["step"] != rate for n in a["news"])
        assert b["calendar"] == []
        assert a["bars"] == b["bars"] and a["eventsDigest"] == b["eventsDigest"]


def test_replay_move_keeps_the_bracket_stop_in_its_oco_group():
    """Policy v13: {"k":"move"} moves a working exit in place (same id, OCO group kept)."""
    log = [
        {"k": "ans", "pBp": 5000, "skipped": True},
        {"k": "order", "type": "market", "side": "buy", "qty": "1.0", "price": None, "ro": False,
         "stop": "80.00", "tp": "150.00", "trail": None},
        {"k": "adv", "n": 1},
    ]
    before = replay("m01-false-breakout", 5, log)
    stop = next(o for o in before.state["orders"] if o["type"] == "stop_market")
    moved = replay("m01-false-breakout", 5, [*log, {"k": "move", "id": stop["id"], "price": "81.00"}])
    assert moved.results[-1]["accepted"]
    after = next(o for o in moved.state["orders"] if o["id"] == stop["id"])
    assert after["price"] == "81.00000000"
    assert after["ocoGroup"] == stop["ocoGroup"] != 0
    assert len(moved.state["orders"]) == len(before.state["orders"])
