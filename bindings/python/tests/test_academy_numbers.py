"""Academy lesson numbers checked against the engine (add-academy-and-strategies design D2, task W6).

Every option value or Greek a lesson states is a `whatif` check: a step's what-if widget, a slider
position and a field of the engine's result. This test builds the request exactly as the web's
`academyRequest` (apps/web/src/lib/game/whatifLab.ts) does and values it with `option_whatif`. The
expiry arithmetic checks of lessons and glossary examples (payoff at a price, breakevens, maximum profit
and loss) must also agree with the engine's expiry analytics. The web unit tests check the same through
the WASM build (apps/web/src/lib/content/academyEngine.test.ts).
"""

import json
import math
from pathlib import Path

import pytest

from tradelab_engine import option_whatif

CONTENT = Path(__file__).resolve().parents[3] / "apps" / "web" / "src" / "lib" / "content"
MINUTES_PER_DAY = 1440
GRID_POINTS = 61


def _lessons():
    index = json.loads((CONTENT / "academy" / "index.json").read_text())
    return [json.loads((CONTENT / "academy" / f"{m['id']}.json").read_text()) for m in index["lessons"]]


def _fixed(x, digits):
    """JavaScript's Number.prototype.toFixed for the values the web sends (half away from zero on the decimal)."""
    return f"{x:.{digits}f}"


def _leg(leg):
    entry = _fixed(leg["entry"], 2)
    if leg["kind"] == "perp":
        return {"kind": "perp", "side": leg["side"], "qty": str(leg["qty"]), "entry": entry}
    return {
        "kind": leg["kind"],
        "side": leg["side"],
        "qty": str(leg["qty"]),
        "strike": _fixed(leg["strike"], 2),
        "minutesToExpiry": round(leg["days"] * MINUTES_PER_DAY),
        "iv": _fixed(leg["ivPct"] / 100, 4),
        "entry": entry,
    }


def academy_request(legs, shift, forward):
    """Mirror of whatifLab.ts academyRequest: one step is one day, a contract is one unit."""
    price, time, iv = shift.get("price", 0), shift.get("time", 0), shift.get("iv", 0)
    return {
        "forward": _fixed(forward, 2),
        "minutesPerStep": MINUTES_PER_DAY,
        "multiplier": "1",
        "legs": [_leg(l) for l in legs],
        "shift": {"forward": _fixed(forward * (1 + price / 100), 2), "minutesElapsed": round(time * MINUTES_PER_DAY),
                  "ivPoints": str(iv)},
        "grid": {"lo": _fixed(forward * 0.7, 2), "hi": _fixed(forward * 1.3, 2), "points": GRID_POINTS},
    }


def field_of(result, path):
    v = result
    for k in path.split("."):
        v = v[int(k)] if isinstance(v, list) else v.get(k)
        if v is None:
            return None
    return float(v)


def _within(got, expect, digits):
    return got is not None and abs(got - expect) <= 0.5 * 10 ** -digits + 1e-9


def _whatif_checks():
    for lesson in _lessons():
        widgets = {s["id"]: s.get("widget") for s in lesson["steps"]}
        for c in lesson.get("checks", []):
            if c["kind"] == "whatif":
                yield lesson["id"], c, widgets[c["step"]]


WHATIF_CHECKS = list(_whatif_checks())


def test_lessons_state_engine_numbers():
    assert len(WHATIF_CHECKS) >= 15


@pytest.mark.parametrize("lesson,check,widget", WHATIF_CHECKS,
                         ids=[f"{l}-{c['step']}-{c['field']}-{json.dumps(c.get('shift', {}))}" for l, c, _ in WHATIF_CHECKS])
def test_whatif_check(lesson, check, widget):
    assert widget is not None and widget["kind"] == "whatif", f"{lesson}: {check['step']} has no what-if widget"
    r = option_whatif(academy_request(widget["legs"], check.get("shift", {}), widget["forward"]))
    got = field_of(r, check["field"])
    assert _within(got, check["expect"], check.get("digits", 8)), f"{lesson} {check}: engine gives {got}"


def test_whatif_widgets_enter_at_the_engine_value():
    for lesson in _lessons():
        for s in lesson["steps"]:
            w = s.get("widget") or {}
            if w.get("kind") != "whatif":
                continue
            r = option_whatif(academy_request(w["legs"], {}, w["forward"]))
            for leg, view in zip(w["legs"], r["legs"]):
                if leg["kind"] != "perp":
                    assert f"{float(view['value']):.2f}" == f"{leg['entry']:.2f}", f"{lesson['id']} {s['id']}"


# ---- expiry arithmetic (lessons and glossary examples) against the engine's analytics ----

def _expiry(legs):
    academy = [{"kind": g["kind"], "side": g["side"], "qty": g["qty"], "strike": g.get("strike", 0), "days": 30,
                "ivPct": 50, "entry": g["price"]} for g in legs]
    forward = sum(g.get("strike", g["price"]) for g in legs) / len(legs)
    request = academy_request(academy, {}, forward)
    request["grid"] = {"lo": "0.00", "hi": _fixed(4 * forward, 2), "points": 2}
    return option_whatif(request)["expiry"]


def _payoff_at(legs, s):
    request = academy_request(
        [{"kind": g["kind"], "side": g["side"], "qty": g["qty"], "strike": g.get("strike", 0), "days": 30, "ivPct": 50,
          "entry": g["price"]} for g in legs], {}, 100)
    request["grid"] = {"lo": _fixed(s, 2), "hi": _fixed(s + 1, 2), "points": 2}
    return float(option_whatif(request)["expiry"]["curve"][0][1])


def _expiry_checks():
    sources = [(l["id"], l.get("checks", [])) for l in _lessons()]
    glossary = json.loads((CONTENT / "glossary.json").read_text())
    sources += [(t["id"], t.get("checks", [])) for t in glossary["terms"]]
    for owner, checks in sources:
        for c in checks:
            if c["kind"] in ("payoff", "breakeven", "maxProfit", "maxLoss"):
                yield owner, c


EXPIRY_CHECKS = list(_expiry_checks())


@pytest.mark.parametrize("owner,check", EXPIRY_CHECKS, ids=[f"{o}-{c['kind']}-{i}" for i, (o, c) in enumerate(EXPIRY_CHECKS)])
def test_expiry_check(owner, check):
    digits = check.get("digits", 8)
    if check["kind"] == "payoff":
        assert _within(_payoff_at(check["legs"], check["at"]), check["expect"], digits), owner
        return
    e = _expiry(check["legs"])
    if check["kind"] == "breakeven":
        got = [float(b) for b in e["breakevens"]]
        assert len(got) == len(check["expect"]) and all(_within(g, x, digits) for g, x in zip(got, check["expect"])), owner
        return
    v = e["maxProfit" if check["kind"] == "maxProfit" else "maxLoss"]
    if check["expect"] is None:
        assert v is None, owner
    else:
        assert v is not None and _within(float(v), check["expect"], digits), owner


def test_mirror_matches_the_web_request_shape():
    # Guard the mirror against drift: the web's request for a known leg (whatifLab.test.ts uses the same).
    r = academy_request([{"kind": "call", "side": "buy", "strike": 100, "qty": 1, "days": 30, "ivPct": 60, "entry": 6.85}],
                        {"price": 10, "time": 2, "iv": -5}, 100)
    assert r["shift"] == {"forward": "110.00", "minutesElapsed": 2880, "ivPoints": "-5"}
    assert r["legs"][0]["iv"] == "0.6000" and r["legs"][0]["minutesToExpiry"] == 43200
    assert math.isclose(float(option_whatif(r)["legs"][0]["iv"]), 0.55)
