"""Phase-2 batch B missions (m09-m12) through the Python binding: the announced-events list (G3) and
the drawn outcomes (G2). Their goldens and the m11 reveal digest are shared fixtures (engine/tests/golden,
replayed by test_golden.py and the C++ tests), so the server re-simulates them exactly like every client."""

import golden
from tradelab_engine import Run, mission_reveal, missions


def test_batch_b_missions_are_listed_with_their_rules():
    by_id = {m["id"]: m for m in missions()}
    assert by_id["m09-event-straddle"]["leverageCap"] == 3
    assert by_id["m10-hedged-book"]["leverageCap"] == 5
    assert by_id["m11-news-calendar"]["totalBars"] == 200
    assert by_id["m12-capstone"]["totalBars"] == 230
    assert by_id["m12-capstone"]["leverageCap"] == 10


def test_announced_release_is_upcoming_with_its_consensus_only():
    reveal = mission_reveal("m09-event-straddle", 5)
    release = reveal["events"][0]["step"]
    with Run("m09-event-straddle", 5) as run:
        state = run.state()
        assert state["upcoming"] == [{"step": release, "kind": "rate_decision", "expectation": 0}]
        assert state["news"] == []
        state = run.advance(release - state["step"])
    assert state["upcoming"] == []
    assert state["news"][0]["actual"] == reveal["events"][0]["actual"]


def test_reveal_parity_with_native_cpp():
    # Same bytes as the C++ test "G4 with G2: the reveal reports each event's drawn outcome ...".
    fx = golden.load("reveal-m11")
    assert golden.reveal_sha256(fx) == fx["expected"]["sha256"]
    reveal = mission_reveal("m11-news-calendar", 7)
    assert len(reveal["events"]) == 4
    assert {e["kind"] for e in reveal["events"]} == {"macro_data", "exchange_incident", "rate_decision", "network_upgrade"}
    assert all(70 <= e["intensityPct"] <= 130 for e in reveal["events"])
    assert [e["announceLead"] for e in reveal["events"]] == [15, 0, 20, 25]
