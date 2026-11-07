"""Run files through the binding (add-run-sharing-and-journal-stats D2, D9): every shared fixture in
engine/tests/runfiles gets the verdict listed in its index.json, as on every client."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from tradelab_engine import verify_run_file

RUNFILES = Path(__file__).resolve().parents[3] / "engine" / "tests" / "runfiles"
INDEX = json.loads((RUNFILES / "index.json").read_text())


@pytest.mark.parametrize("entry", INDEX["files"], ids=lambda e: e["name"])
def test_shared_run_file_verdict(entry: dict) -> None:
    verdict = verify_run_file((RUNFILES / entry["name"]).read_text())
    for key, value in entry["expected"].items():
        assert verdict[key] == value, key
    if verdict["ok"]:
        again = verify_run_file(verdict["run"])
        assert again["ok"] and again["fileDigest"] == verdict["fileDigest"]


def test_index_lists_every_file() -> None:
    assert sorted(e["name"] for e in INDEX["files"]) == sorted(p.name for p in RUNFILES.glob("*.tlrun"))
