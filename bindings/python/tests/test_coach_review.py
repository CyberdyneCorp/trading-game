"""Coach review through the Python binding (add-practice-and-advanced-strategies design D17): refused
while the run is active, report-only, and the m23 golden run's moments read at their own steps."""

import golden
from tradelab_engine import Run, RunOptions, replay_on


def test_refused_while_active_and_nothing_changes():
    with Run("m23-second-chance", 3) as run:
        assert run.coach_review() == {"error": "run_active"}
        replay_on(run, [{"k": "order", "type": "market", "side": "buy", "qty": "10", "price": None, "ro": False},
                        {"k": "adv", "n": 1000}])
        before = run.state()
        first = run.coach_review()
        assert run.coach_review() == first
        assert run.state() == before
        codes = [m["code"] for m in first["moments"]]
        assert "entry_without_stop" in codes
        moment = first["moments"][codes.index("entry_without_stop")]
        assert moment["tone"] == "warn" and moment["lesson"] == "risk-sizing" and moment["glossary"] == "stop-loss"


def test_m23_revenge_moment_matches_the_debrief_finding():
    fx = golden.load("coach-m23-second-chance")
    with Run(fx["mission"], fx["seed"], RunOptions.from_json(fx.get("options"))) as run:
        replay_on(run, fx["actions"])
        review = run.coach_review()
        debrief = run.debrief()
    revenge = next(m for m in review["moments"] if m["code"] == "revenge_trade")
    finding = next(f for f in debrief["findings"] if f["code"] == "revenge_trade")
    assert revenge["step"] == finding["step"]
    assert revenge["evidence"]["sizeRatioBp"] == finding["evidence"]["sizeRatioBp"]
    assert revenge["evidence"]["loss"] == finding["evidence"]["loss"]
    assert len(review["moments"]) <= 12
