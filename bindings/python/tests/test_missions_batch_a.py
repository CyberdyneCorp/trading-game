"""Phase-2 batch A missions (m06-m08) through the Python binding. Their goldens and the m08 reveal
digest are shared fixtures (engine/tests/golden, replayed by test_golden.py and the C++ tests), so the
server re-simulates them exactly like every client."""

import golden
from tradelab_engine import mission_reveal, missions


def test_batch_a_missions_are_listed_with_their_rules():
    by_id = {m["id"]: m for m in missions()}
    assert by_id["m06-thin-book"]["allowShort"] is False
    assert by_id["m06-thin-book"]["leverageCap"] == 2
    assert by_id["m07-trend-trailing"]["leverageCap"] == 5
    assert by_id["m08-regime-switch"]["totalBars"] == 180


def test_m08_golden_signals_by_regime_match_native_cpp():
    """Shared fixture m08-regime-switch (digests checked in test_golden.py)."""
    debrief = golden.replay_run(golden.load("m08-regime-switch")).debrief
    assert debrief is not None
    assert debrief["score"]["version"] == 14
    assert debrief["regimes"][0]["start"] == 60
    assert {r["label"] for r in debrief["regimes"]} <= {"range", "trend", "high_vol"}
    assert all(t["regime"] in {"range", "trend", "high_vol"} for t in debrief["trades"])


def test_reveal_parity_with_native_cpp():
    # Same bytes as the C++ test "G4: tl_mission_reveal is a pure function of mission and seed".
    fx = golden.load("reveal-m08")
    assert golden.reveal_sha256(fx) == fx["expected"]["sha256"]
    reveal = mission_reveal("m08-regime-switch", 7)
    assert sum(s["bars"] for s in reveal["spans"]) == 180
    assert reveal["events"] == []
    assert mission_reveal("nope", 1) == {"error": "unknown_mission"}
