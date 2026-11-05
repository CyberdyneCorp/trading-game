"""Phase-2 batch B missions (m09-m12) through the Python binding: the same goldens and reveal digest
as engine/tests/test_missions.cpp, the announced-events list (G3) and the drawn outcomes (G2), so the
server re-simulates them exactly like every client."""

import hashlib

from tradelab_engine import Run, mission_reveal, missions


def _digests(state):
    return state["digest"], state["eventsDigest"]


def test_batch_b_missions_are_listed_with_their_rules():
    by_id = {m["id"]: m for m in missions()}
    assert by_id["m09-event-straddle"]["leverageCap"] == 3
    assert by_id["m10-hedged-book"]["leverageCap"] == 5
    assert by_id["m11-news-calendar"]["totalBars"] == 200
    assert by_id["m12-capstone"]["totalBars"] == 230
    assert by_id["m12-capstone"]["leverageCap"] == 10


def test_m09_golden_matches_native_cpp():
    with Run("m09-event-straddle", 2026) as run:
        run.advance(8)
        run.place_option_order(1, 7, 0, 0, "10", None)
        run.place_option_order(2, 8, 0, 0, "10", None)
        run.advance(12)
        run.place_option_order(3, 7, 1, 0, "10", None)
        run.place_option_order(4, 8, 1, 0, "10", None)
        run.place_order(5, 0, 0, "30", None, False)
        state = run.advance(1000)
    assert _digests(state) == (
        "879aea90be67bf4755b6eed7a822d78d78e2e943cad48a0474fb39ef5eb129ab",
        "6afafb44fd90acf3ba94b9a18ffb072d0757af70047b7103f75adcd0a617ea83",
    )


def test_m10_golden_matches_native_cpp():
    with Run("m10-hedged-book", 2026) as run:
        run.set_leverage(1, 1)
        run.place_order(2, 0, 0, "40", None, False)
        run.place_option_order(3, 20, 0, 0, "40", None)
        run.place_option_order(4, 23, 1, 0, "40", None)
        run.advance(50)
        run.close_position(5)
        state = run.advance(1000)
    assert _digests(state) == (
        "df27e859168062e16953133a9322d1f01958fffd9d6cba9a217d481869c35c67",
        "ae540e1bed50c0d2e6995cc7d63fe6b8d04d772c11c34fd198d65688b87f3251",
    )


def test_m11_golden_matches_native_cpp():
    with Run("m11-news-calendar", 2026) as run:
        run.set_leverage(1, 3)
        run.place_order(2, 0, 0, "60", None, False, "95", "110")
        run.advance(20)
        run.close_position(3)
        run.advance(30)
        run.place_order(4, 0, 1, "60", None, False, "112", "90")
        state = run.advance(1000)
    assert _digests(state) == (
        "9ed49106b9812a758a5f2e0f8e292fbb569733c980dfed7377e409211cfcf074",
        "839e5f90afee251e6cc920fb1b83a13faa1789a97071fabd9f6b538870a1babb",
    )


def test_m12_golden_matches_native_cpp():
    with Run("m12-capstone", 2026) as run:
        run.set_leverage(1, 4)
        run.place_order(2, 0, 0, "80", None, False, "95")
        run.place_option_order(3, 7, 1, 0, "10", None)
        run.place_option_order(4, 8, 1, 0, "10", None)
        run.advance(45)
        run.close_position(5)
        run.advance(30)
        run.place_order(6, 0, 1, "50", None, False, "115")
        state = run.advance(1000)
    assert _digests(state) == (
        "0308160dece972e8883958e8ea45fbc168432068e336115b9212a984c3f9651c",
        "70f0eb16beb797db73246a800de61e636a5f4593dd699e5a6f64bd4c55fac459",
    )


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
    from tradelab_engine.engine import load_library

    raw = load_library().tl_mission_reveal(b"m11-news-calendar", 7)
    assert hashlib.sha256(raw).hexdigest() == "4282fd1e468d68bd8be98bd86422c185e85c48e25559b8c5ff3d6e710b1b3fe8"
    reveal = mission_reveal("m11-news-calendar", 7)
    assert len(reveal["events"]) == 4
    assert {e["kind"] for e in reveal["events"]} == {"macro_data", "exchange_incident", "rate_decision", "network_upgrade"}
    assert all(70 <= e["intensityPct"] <= 130 for e in reveal["events"])
    assert [e["announceLead"] for e in reveal["events"]] == [15, 0, 20, 25]
