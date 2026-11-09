"""Missions m18-m25 and scoring v14 (add-practice-and-advanced-strategies design D12, D13) through the
Python binding: their rules, chains, risk limits and rubrics; the rubric numbers recomputed by hand; the
golden runs' bias findings, episodes and strategy comparison; and the scores of every earlier golden,
byte-identical to the v13 engine's (bindings/python/tests/data/scores_v13.json)."""

import json
from decimal import Decimal
from pathlib import Path

import pytest

import golden
from tradelab_engine import Run, lesson, mission_reveal, missions
from tradelab_engine.engine import RunOptions
from tradelab_engine.replay import replay

BATCH_D = ["m18-gamma-scalp", "m19-funding-carry", "m20-event-vol", "m21-calendar", "m22-scale-manage",
           "m23-second-chance", "m24-fast-market", "m25-the-dip"]
OBJECTIVES = {
    "m18-gamma-scalp": [("delta_within_limit", 1500), ("rehedge_rule_first", 0), ("premium_budget", 300)],
    "m19-funding-carry": [("carry_breakeven_first", 0), ("liq_distance", 1500), ("delta_within_limit", 1000)],
    "m20-event-vol": [("straddle_breakevens", 0), ("hold_through_decision", 0), ("vega_within_limit", 150)],
    "m21-calendar": [("net_debit_first", 0), ("decide_before_expiry", 0), ("gamma_within_limit", 50)],
    "m22-scale-manage": [("total_risk_all_entries", 100), ("partial_exit_or_trail", 0), ("never_stop_away", 0)],
    "m23-second-chance": [("cooldown_after_stop", 0), ("risk_per_idea", 100), ("plan_every_entry", 0)],
    "m24-fast-market": [("plan_before_entry_atr_stop", 0), ("risk_per_idea", 100), ("no_liquidation_wait_ok", 0)],
    "m25-the-dip": [("no_add_beyond_plan", 0), ("risk_per_idea", 100), ("plan_and_stop_every_entry", 0)],
}
LIMITS = {"m18-gamma-scalp": {"deltaBp": 1500}, "m19-funding-carry": {"deltaBp": 1000}, "m20-event-vol": {"vegaBp": 150},
          "m21-calendar": {"gammaBp": 50}}
SCORES_V13 = Path(__file__).parent / "data" / "scores_v13.json"


def test_batch_d_closes_the_missions_and_the_rounds_follow():
    ids = [m["id"] for m in missions() if "practice" not in m]
    assert ids[-8:] == BATCH_D
    rounds = [m for m in missions() if m.get("practice") == "round"]  # design D19: after every mission
    assert [m["id"] for m in rounds] == ["r01-trend", "r02-range", "r03-breakout", "r04-event"]
    assert [m["id"] for m in missions()][-4:] == [m["id"] for m in rounds]
    caps = {m["id"]: m["leverageCap"] for m in missions()}
    assert [caps[m] for m in BATCH_D] == [3, 5, 3, 3, 5, 3, 3, 3]


@pytest.mark.parametrize("mission_id", BATCH_D)
def test_rules_objectives_and_limits(mission_id):
    with Run(mission_id, 3) as run:
        state = run.state()
    rules = state["missionRules"]
    assert [(o["code"], o["valueBp"]) for o in rules["objectives"]] == OBJECTIVES[mission_id]
    limits = rules.get("riskLimits")
    assert (limits or None) == (LIMITS.get(mission_id) or None) or all(
        limits.get(k) == v for k, v in LIMITS[mission_id].items())
    assert rules.get("calendarMargin", False) is (mission_id == "m21-calendar")
    options = mission_id in LIMITS
    assert state["options"]["enabled"] is options
    if options:
        assert state["options"]["allowShort"] is (mission_id != "m18-gamma-scalp")


@pytest.mark.parametrize("mission_id", BATCH_D)
def test_rubrics_ship_unreviewed(mission_id):
    info = lesson(mission_id)
    assert info["reviewed"] is False
    assert len(info["questions"]) == 2


RUBRICS = Path(__file__).resolve().parents[3] / "engine" / "rubrics" / "lessons.json"


def numeric_answer(mission_id):
    """The answer key (engine/rubrics/lessons.json; tl_lesson never sends it to a client)."""
    rubric = [r for r in json.loads(RUBRICS.read_text())["rubrics"] if r["mission"] == mission_id][0]
    q = [q for q in rubric["questions"] if q["kind"] == "numeric"][0]
    return Decimal(q["answer"])


def test_rubric_numbers_recomputed_by_hand():
    # m18: delta +0.20 per unit on 50 units -> sell 0.20 x 50 = 10 perpetual units.
    assert numeric_answer("m18-gamma-scalp") == Decimal("0.20") * 50
    # m19: a short of 20 at 100, +0.03% per interval, 3 intervals a day: 20 x 100 x 0.0003 x 3 = 1.80.
    assert numeric_answer("m19-funding-carry") == 20 * 100 * Decimal("0.0003") * 3
    # m20: an ATM 100 straddle bought for 6: breakevens 94 and 106 (the upper one is asked).
    assert numeric_answer("m20-event-vol") == 100 + 6
    # m21: sell the near 100 call for 2.00, buy the far one for 3.50: net debit 1.50.
    assert numeric_answer("m21-calendar") == Decimal("3.50") - Decimal("2.00")
    # m22: long 30 at 100, stop 96; sell 15 at 104 and move the stop to 100: 15 left x (100 - 100) = 0 at the
    # stop, and the 15 sold already banked 15 x 4 = 60: the worst result left for the trade is +60.
    assert numeric_answer("m22-scale-manage") == 15 * (104 - 100) + 15 * (100 - 100)
    # m23: two 1% losses, then a 2% loss: 4% before compounding.
    assert numeric_answer("m23-second-chance") == 1 + 1 + 2
    # m24: entry 112, invalidation 104, risk budget 100: 100 / 8 = 12.5.
    assert numeric_answer("m24-fast-market") == Decimal(100) / (112 - 104)
    # m25: long 10 at 100, add 10 at 90, stop 85: 10 x 15 + 10 x 5 = 200.
    assert numeric_answer("m25-the-dip") == 10 * (100 - 85) + 10 * (90 - 85)


def test_drawn_stories_stay_in_the_reveal():
    assert {mission_reveal("m20-event-vol", s)["events"][0]["announceLead"] for s in range(1, 10)} == {35}
    labels = {tuple(sp["label"] for sp in mission_reveal("m19-funding-carry", s)["spans"]) for s in range(1, 40)}
    assert any("high_vol" in spans for spans in labels) and any("high_vol" not in spans for spans in labels)


@pytest.mark.parametrize("mission_id", BATCH_D)
def test_golden_replays(mission_id):
    fx = golden.load(mission_id)
    assert golden.actual(fx) == fx["expected"]


def debrief_of(name):
    fx = golden.load(name)
    return replay(fx["mission"], fx["seed"], fx["actions"], RunOptions.from_json(fx.get("options"))).debrief


def findings(debrief, code):
    return [f for f in debrief["findings"] if f["code"] == code]


def test_v14_findings_name_the_bias_with_evidence():
    d = debrief_of("m23-second-chance")
    assert d["score"]["version"] == 14
    (revenge,) = findings(d, "revenge_trade")
    ev = revenge["evidence"]
    assert ev["trade"] == 1 and ev["entryStep"] - ev["exitStep"] <= 3 and ev["sizeRatioBp"] == 20000 and not ev["planned"]
    assert Decimal(ev["loss"]) > 0
    (fomo,) = findings(debrief_of("m24-fast-market"), "fomo_entry")
    assert fomo["evidence"]["closes"] >= 4 and fomo["evidence"]["extensionAtr"] > 200
    (avg,) = findings(debrief_of("m25-the-dip"), "averaging_down")
    ev = avg["evidence"]
    assert Decimal(ev["avgAfter"]) < Decimal(ev["avgBefore"])
    assert Decimal(ev["riskAfter"]) > Decimal(ev["planRisk"]) > 0
    (limit,) = findings(debrief_of("m18-gamma-scalp"), "risk_limit_exceeded")
    assert limit["evidence"]["limit"] == "delta" and limit["evidence"]["limitBp"] == 1500
    assert limit["evidence"]["worstBp"] > 1500


def test_episodes_and_comparison():
    d = debrief_of("m22-scale-manage")
    (ep,) = d["episodes"]
    assert ep["side"] == "long" and len(ep["entries"]) == 1 and len(ep["exits"]) >= 1
    assert [m["kind"] for m in ep["stopMoves"]] == ["to_breakeven"]
    assert ep["mfeRBp"] is not None and ep["maeRBp"] is not None
    names = [r["name"] for r in d["strategyComparison"]["rows"]]
    assert names == ["all_out_2r", "managed_exit", "hold_initial_stop"]
    rows = {m: [r["name"] for r in debrief_of(m)["strategyComparison"]["rows"]] for m in BATCH_D[:4]}
    assert rows["m18-gamma-scalp"] == ["unhedged_straddle", "hedged_every_4", "hedged_band"]
    assert rows["m19-funding-carry"] == ["unhedged_short", "hedged_carry_5x", "hedged_carry_2x"]
    assert rows["m20-event-vol"] == ["early_sell_before", "early_hold_through", "late_hold_through", "short_straddle_through"]
    assert rows["m21-calendar"] == ["calendar_atm", "diagonal_up", "short_straddle_near"]
    for m in BATCH_D[4 + 1:]:
        assert debrief_of(m)["strategyComparison"] is None


def test_earlier_scores_are_byte_identical_to_v13():
    snapshot = json.loads(SCORES_V13.read_text())["runs"]
    assert len(snapshot) >= 15
    for name, want in snapshot.items():
        d = debrief_of(name)
        score = dict(d["score"])
        score.pop("version")
        got = {
            "score": score,
            "findings": [[f["code"], f["step"], f["count"], f["amount"]] for f in d["findings"] if "evidence" not in f],
            "lesson": d["lesson"],
            "netPnl": d["netPnl"],
            "trades": [[t["n"], t["net"], t["rBp"]] for t in d["trades"]],
            "strategyComparison": d.get("strategyComparison"),
        }
        assert got == want, name


def test_taught_carry_and_calendar_are_not_naked_writing():
    """Regression (wave 2 cross-check): m19's covered put (the put written beside a short perpetual) and
    m21's calendar (the near call written against the later long) read as uncovered writing."""
    for name in ("m19-funding-carry", "m21-calendar"):
        assert findings(debrief_of(name), "wrote_options") == [], name
