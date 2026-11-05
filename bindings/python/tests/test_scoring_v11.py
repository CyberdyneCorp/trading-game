"""Scoring v11 through the binding: mission rules in the state, lesson answers in the replay log and the
debrief, explicit decision records (tasks 4.1, 4.2, 4.5)."""

from tradelab_engine import Run, lesson, replay


def test_lesson_lists_questions_without_the_key():
    m01 = lesson("m01-false-breakout")
    assert m01["mission"] == "m01-false-breakout"
    assert m01["reviewed"] is False
    assert [q["id"] for q in m01["questions"]] == ["breakout-plan", "risk-two-percent"]
    assert all("answer" not in q for q in m01["questions"])
    assert lesson("csv-unknown") == {"error": "no_lesson"}


def test_mission_rules_in_the_state():
    with Run("m07-trend-trailing", 4) as run:
        rules = run.state()["missionRules"]
    assert rules["permissions"] == {"long": True, "short": True, "optionsBuy": False, "optionsWrite": False}
    assert rules["drawdownBudgetBp"] == 1000
    assert rules["objectives"][0] == {"code": "risk_from_stop", "valueBp": 100}
    assert rules["confirmStopBeyondLiq"] is True


def test_replayed_lesson_answers_and_decisions_reach_the_debrief():
    log = [
        {"k": "lesson", "q": 0, "choice": 1, "value": None, "skipped": False},
        {"k": "lesson", "q": 1, "choice": 0, "value": "200", "skipped": False},
        {"k": "adv", "n": 1},
        {"k": "order", "type": "market", "side": "buy", "qty": "1.0", "price": None, "ro": False,
         "stop": None, "tp": None, "trail": None},
        {"k": "adv", "n": 1000},
    ]
    a = replay("m01-false-breakout", 5, log)
    b = replay("m01-false-breakout", 5, log)
    assert a.digest == b.digest
    assert [r["accepted"] for r in a.results if r is not None] == [True, True, True]
    lessons = a.debrief["lessons"]
    assert lessons["points"] == 2 and lessons["total"] == 2 and lessons["reviewed"] is False
    assert lessons["questions"][1]["given"] == "200.00000000"
    # The shipped rubric is not reviewed yet: answered, reported, not scored.
    assert a.debrief["score"]["dims"]["understanding"]["score"] is None
    kinds = [d["kind"] for d in a.debrief["decisions"][:3]]
    assert kinds == ["wait", "act", "hold"]
    assert a.debrief["missionRules"]["liqWarningBp"] == 300
    without = replay("m01-false-breakout", 5, [x for x in log if x["k"] != "lesson"])
    assert without.state["eventsDigest"] == a.state["eventsDigest"]
    assert without.digest != a.digest
