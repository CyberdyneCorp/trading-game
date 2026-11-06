"""Cross-binding goldens: every shared fixture in engine/tests/golden replays through the Python
binding to the digests engine/tests/test_golden.cpp checks natively (and the WASM build in CI)."""

import pytest

import golden
from tradelab_engine import engine_version


def test_index_lists_every_fixture_file():
    files = sorted(p.stem for p in golden.GOLDEN_DIR.glob("*.json") if p.name != "index.json")
    assert sorted(golden.index()) == files
    assert len(files) == len(set(golden.index()))


@pytest.mark.parametrize("name", golden.index())
def test_fixture_replays_to_its_frozen_digests(name):
    fx = golden.load(name)
    assert fx["versions"]["executionPolicy"] == engine_version()["executionPolicy"]
    assert golden.actual(fx) == fx["expected"]
