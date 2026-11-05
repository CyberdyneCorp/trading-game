"""Phase-2 batch A missions (m06-m08) through the Python binding: the same goldens and reveal digest
as engine/tests/test_missions.cpp, so the server re-simulates them exactly like every client."""

import hashlib

from tradelab_engine import Run, mission_reveal, missions


def _digests(state):
    return state["digest"], state["eventsDigest"]


def test_batch_a_missions_are_listed_with_their_rules():
    by_id = {m["id"]: m for m in missions()}
    assert by_id["m06-thin-book"]["allowShort"] is False
    assert by_id["m06-thin-book"]["leverageCap"] == 2
    assert by_id["m07-trend-trailing"]["leverageCap"] == 5
    assert by_id["m08-regime-switch"]["totalBars"] == 180


def test_m06_golden_matches_native_cpp():
    with Run("m06-thin-book", 2026) as run:
        run.set_leverage(1, 2)
        run.place_order(2, 1, 0, "40", "99.5", False, "97.5", "102")
        run.advance(30)
        run.place_order(3, 0, 0, "150", None, False)
        run.advance(40)
        run.close_position(4)
        state = run.advance(1000)
    assert _digests(state) == (
        "23648bda21adb3552ce57253b0c943955e7969f4fb6a607bc540174ade3c72b5",
        "b084466a7e0ab87dacc53c40794afcbc017917decfac736279404e82558f009a",
    )


def test_m07_golden_matches_native_cpp():
    with Run("m07-trend-trailing", 2026) as run:
        run.set_leverage(1, 5)
        run.advance(15)
        run.place_order(2, 0, 0, "60", None, False)
        run.advance(1)
        run.place_order(3, 4, 1, "60", None, True, None, None, "2.5")
        run.advance(40)
        run.place_order(4, 0, 1, "30", None, False)
        state = run.advance(1000)
    assert _digests(state) == (
        "bc09fbe3240b31707462fd0638c6d14dddbfe73adef73a5359b1f1b7a084f6c8",
        "911e8dce2b537d94c11f7c8a8af67a1472f169032d0c6e98d137b4bb95488bd0",
    )


def test_m08_golden_and_signals_by_regime_match_native_cpp():
    with Run("m08-regime-switch", 2026) as run:
        run.set_leverage(1, 3)
        run.place_order(2, 0, 1, "50", None, False, "104", "96")
        run.advance(35)
        run.close_position(3)
        run.place_order(4, 0, 0, "80", None, False, "90")
        run.advance(30)
        state = run.advance(1000)
        debrief = run.debrief()
    assert _digests(state) == (
        "8e469afe0aa2c6373e71b1d487fce597354699e3383601f15d1fd1ebd95d8913",
        "e96bbb50aedd88cce5e21de92081431436bc4595affec276d05384d682d1a6bb",
    )
    assert debrief["score"]["version"] == 10
    assert debrief["regimes"][0]["start"] == 60
    assert {r["label"] for r in debrief["regimes"]} <= {"range", "trend", "high_vol"}
    assert all(t["regime"] in {"range", "trend", "high_vol"} for t in debrief["trades"])


def test_reveal_parity_with_native_cpp():
    # Same bytes as the C++ test "G4: tl_mission_reveal is a pure function of mission and seed".
    from tradelab_engine.engine import load_library

    raw = load_library().tl_mission_reveal(b"m08-regime-switch", 7)
    assert hashlib.sha256(raw).hexdigest() == "5ac2b53bfc31fa0cdf7d25d56bda08345c3bde8978808ef514f9ba76af9ee02f"
    reveal = mission_reveal("m08-regime-switch", 7)
    assert sum(s["bars"] for s in reveal["spans"]) == 180
    assert reveal["events"] == []
    assert mission_reveal("nope", 1) == {"error": "unknown_mission"}
