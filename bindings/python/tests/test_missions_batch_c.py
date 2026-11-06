"""Strategy missions (m13-m17, add-academy-and-strategies design D8) through the Python binding: their
rules, chains, objectives and lesson rubrics. Their goldens are shared fixtures (engine/tests/golden,
replayed by test_golden.py and the C++ tests), so the server re-simulates them like every other mission."""

import pytest

import golden
from tradelab_engine import Run, lesson, mission_reveal, missions

STRATEGIES = ["m13-call-spread", "m14-covered-call", "m15-protective-put", "m16-range-condor", "m17-before-expiry"]
OBJECTIVES = {
    "m13-call-spread": ["spread_breakevens", "premium_budget", "cover_short_leg"],
    "m14-covered-call": ["capped_max_profit", "premium_vs_funding", "risk_from_stop"],
    "m15-protective-put": ["floor_before_buying", "premium_budget", "record_tenor"],
    "m16-range-condor": ["size_by_max_loss", "breakout_exit_rule", "maintenance_buffer"],
    "m17-before-expiry": ["decide_before_expiry", "no_roll_without_thesis", "risk_per_idea"],
}


def test_strategy_missions_follow_the_capstone_in_the_catalog():
    ids = [m["id"] for m in missions()]
    assert ids[-5:] == STRATEGIES
    by_id = {m["id"]: m for m in missions()}
    for mission_id in STRATEGIES:
        assert by_id[mission_id]["leverageCap"] == 3
        assert by_id[mission_id]["warmupBars"] == 60


@pytest.mark.parametrize("mission_id", STRATEGIES)
def test_rules_chain_and_objectives(mission_id):
    with Run(mission_id, 3) as run:
        state = run.state()
    rules = state["missionRules"]
    assert [o["code"] for o in rules["objectives"]] == OBJECTIVES[mission_id]
    assert rules["permissions"]["optionsBuy"] is True
    assert rules["permissions"]["optionsWrite"] is (mission_id != "m15-protective-put")  # m15: the buyer's lesson
    assert state["options"]["enabled"] is True
    chain = state["options"]["chain"]
    assert all(round(float(s["strike"]) * 100) % 250 == 0 for s in chain)  # 2.5 steps
    assert {s["listStep"] for s in chain} == {60}  # listed at the first decision


@pytest.mark.parametrize("mission_id", STRATEGIES)
def test_rubrics_ship_unreviewed(mission_id):
    info = lesson(mission_id)
    assert info["reviewed"] is False
    assert len(info["questions"]) == 2


def test_drawn_outcomes_are_in_the_reveal():
    severities = {mission_reveal("m15-protective-put", seed)["events"][0]["actual"] for seed in range(1, 30)}
    assert severities == {-2, -1, 0}
    rallies = {mission_reveal("m14-covered-call", seed)["events"][0]["actual"] for seed in range(1, 30)}
    assert rallies == {0, 2}
    assert mission_reveal("m17-before-expiry", 3)["events"] == []


@pytest.mark.parametrize("mission_id", STRATEGIES)
def test_golden_replays(mission_id):
    fx = golden.load(mission_id)
    assert golden.actual(fx) == fx["expected"]
