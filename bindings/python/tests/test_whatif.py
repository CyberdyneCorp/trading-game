"""What-if valuation, combo dry run and combo tags through the Python binding (add-academy-and-strategies
tasks E2, E3 and E6). The native engine tests (engine/tests/test_whatif.cpp) cover the same calls."""

import copy
import json

from tradelab_engine import Run, option_whatif, replay
from tradelab_engine.engine import load_library


def _leg(kind, side, strike, entry, qty="1"):
    return {"kind": kind, "side": side, "qty": qty, "strike": strike, "minutesToExpiry": 30 * 1440, "iv": "0.6",
            "entry": entry}


def _runless(legs, **shift):
    return {"forward": "100", "minutesPerStep": 1440, "multiplier": "1", "legs": legs, "shift": shift}


def test_iron_condor_analytics():
    r = option_whatif(_runless([_leg("put", "buy", "90", "0.5"), _leg("put", "sell", "95", "2"),
                                _leg("call", "sell", "105", "2"), _leg("call", "buy", "110", "0.5")]))
    assert r["expiry"]["breakevens"] == ["92.00000000", "108.00000000"]
    assert r["expiry"]["maxLoss"] == "2.00000000"
    assert r["expiry"]["maxProfit"] == "3.00000000"


def test_short_strangle_is_unbounded_above_and_the_c_abi_agrees():
    request = _runless([_leg("call", "sell", "105", "2"), _leg("put", "sell", "95", "2")])
    r = option_whatif(request)
    assert r["expiry"]["maxLoss"] is None
    assert r["expiry"]["breakevens"] == ["91.00000000", "109.00000000"]
    raw = load_library().tl_option_whatif(json.dumps(request).encode())
    assert json.loads(raw) == r
    assert option_whatif({"legs": []}) == {"error": "invalid_input"}


def test_run_whatif_with_no_shift_matches_the_chain():
    with Run("m05-options-writer", 3) as run:
        chain = [q for q in run.state()["options"]["chain"] if q["listed"]]
        assert chain
        for q in chain:
            r = run.option_whatif({"legs": [{"series": q["id"], "side": "buy", "qty": "1"}]})
            leg = r["legs"][0]
            assert leg["entry"] == q["ask"]
            assert [leg[g] for g in ("delta", "gamma", "theta", "vega", "iv")] == \
                   [q[g] for g in ("delta", "gamma", "theta", "vega", "iv")]
            tick = 100  # premium tick 0.01 in units of 1e-8 is 1_000_000; compare in cents
            assert round(float(leg["value"]) * tick) == round(float(q["mid"]) * tick)


def test_combo_preview_changes_nothing_and_matches_placement():
    with Run("m05-options-writer", 3) as run:
        state = run.state()
        chain = sorted((q for q in state["options"]["chain"] if q["listed"] and not q["settling"]),
                       key=lambda q: (q["expiryStep"], float(q["strike"])))
        expiry = chain[0]["expiryStep"]
        calls = [q for q in chain if q["expiryStep"] == expiry and q["kind"] == "call"]
        legs = [{"series": calls[2]["id"], "side": "buy", "qty": "1"},
                {"kind": "perp", "side": "buy", "qty": "1"},
                {"series": calls[3]["id"], "side": "sell", "qty": "1"}]
        before = json.dumps(run.state())
        preview = run.combo_preview(legs)
        assert json.dumps(run.state()) == before
        assert preview["accepted"] is True
        results = [run.place_option_order(1, legs[0]["series"], 0, 0, "1", None),
                   run.place_order(2, 0, 0, "1", None, False),
                   run.place_option_order(3, legs[2]["series"], 1, 0, "1", None)]
        assert [r["accepted"] for r in results] == [leg["accepted"] for leg in preview["legs"]]
        assert run.state()["account"]["available"] == preview["availableAfter"]


def test_combo_tags_do_not_change_replay():
    with Run("m05-options-writer", 3) as run:
        series = [q["id"] for q in run.state()["options"]["chain"] if q["listed"]][:2]
    log = [{"k": "opt", "series": series[0], "side": "buy", "type": "market", "qty": "1", "premium": None},
           {"k": "opt", "series": series[1], "side": "sell", "type": "market", "qty": "1", "premium": None},
           {"k": "adv", "n": 5}]
    tagged = copy.deepcopy(log)
    for a in tagged[:2]:
        a["combo"] = "c1"
    assert replay("m05-options-writer", 3, tagged).digest == replay("m05-options-writer", 3, log).digest
