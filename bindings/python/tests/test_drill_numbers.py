"""Daily drill answers checked against the engine (add-practice-and-advanced-strategies design D15).

The web's drills.ts generates items from the drill templates (apps/web/src/lib/content/drills) and
computes their answers with the lesson checks; the parity fixture apps/desktop/tests/fixtures/web/
drills.json holds 20 generated items of every template (and four whole sessions). Here the option kinds
(payoff at a price, breakevens) are valued with the engine's expiry analytics through `option_whatif`,
and the trailing-stop kinds with the engine's trigger formula, which test_academy_numbers.py proves
against a scripted run.
"""

import json
from pathlib import Path

import pytest

from test_academy_numbers import _expiry, _payoff_at, _trail_trigger

ROOT = Path(__file__).resolve().parents[3]
CONTENT = ROOT / "apps" / "web" / "src" / "lib" / "content" / "drills"
FIXTURE = ROOT / "apps" / "desktop" / "tests" / "fixtures" / "web" / "drills.json"


def _templates():
    out = {}
    for path in CONTENT.glob("*.json"):
        for t in json.loads(path.read_text())["templates"]:
            out[t["id"]] = t
    return out


TEMPLATES = _templates()


def _items():
    cases = json.loads(FIXTURE.read_text())["cases"]
    items = [c["out"] for c in cases if c["fn"] == "makeItem"]
    for c in cases:
        if c["fn"] == "buildSession":
            items += c["out"]["items"]
    return items


def _resolve(v, params):
    if isinstance(v, str) and v.startswith("$"):
        return float(params[v[1:]])
    if isinstance(v, list):
        return [_resolve(x, params) for x in v]
    if isinstance(v, dict):
        return {k: _resolve(x, params) for k, x in v.items()}
    return v


ITEMS = _items()
OPTION_ITEMS = [i for i in ITEMS if TEMPLATES[i["template"]]["answer"]["kind"] in ("payoff", "breakeven")]
TRAIL_ITEMS = [i for i in ITEMS if TEMPLATES[i["template"]]["answer"]["kind"] == "trail"]


def test_the_fixture_covers_every_option_and_trail_template():
    kinds = {i["template"] for i in OPTION_ITEMS + TRAIL_ITEMS}
    expected = {t for t, x in TEMPLATES.items() if x["answer"]["kind"] in ("payoff", "breakeven", "trail")}
    assert kinds == expected and len(OPTION_ITEMS) >= 80


@pytest.mark.parametrize("item", OPTION_ITEMS, ids=[f"{i['template']}-{n}" for n, i in enumerate(OPTION_ITEMS)])
def test_option_drill_answer_is_the_engine_number(item):
    a = _resolve(TEMPLATES[item["template"]]["answer"], item["params"])
    if a["kind"] == "payoff":
        got = _payoff_at(a["legs"], a["at"])
    else:
        got = [float(b) for b in _expiry(a["legs"])["breakevens"]][a.get("index", 0)]
    assert abs(got - item["answer"]) <= 0.005 + 1e-9, (item, got)


@pytest.mark.parametrize("item", TRAIL_ITEMS, ids=[f"{i['template']}-{n}" for n, i in enumerate(TRAIL_ITEMS)])
def test_trail_drill_answer_is_the_engine_trigger(item):
    a = _resolve(TEMPLATES[item["template"]]["answer"], item["params"])
    rate = a["ratePct"] / 100 if "ratePct" in a else None
    got = _trail_trigger("sell", item["params"]["close"], "0.01", rate=rate, distance=item["params"].get("dist"))
    assert float(got) == item["answer"], item
