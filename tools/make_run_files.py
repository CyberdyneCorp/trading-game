#!/usr/bin/env python3
"""Checks or regenerates the shared run-file fixtures in engine/tests/runfiles (design D9 of
openspec/changes/add-run-sharing-and-journal-stats).

  python3 tools/make_run_files.py           # check: exit 1 when a file or index.json differs
  python3 tools/make_run_files.py --write   # rewrite them

Valid files are built from the scripted golden runs (engine/tests/golden): the same mission, seed,
options and actions, and the golden's expected status and digests as the recorded result. The
invalid files change one thing each. Every file is verified through the Python binding
(libtradelab from engine/build/native-release, or TRADELAB_LIB) and its verdict is written to
index.json; the engine tests, the web unit tests and the desktop compare against it.
"""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "bindings" / "python")]

from tradelab_engine import abi_version, engine_version, replay, verify_run_file  # noqa: E402
from tradelab_engine.engine import RunOptions  # noqa: E402

GOLDEN = ROOT / "engine" / "tests" / "golden"
OUT = ROOT / "engine" / "tests" / "runfiles"
DEFAULT_OPTIONS = {"adviceAllowance": 0, "newsLead": 0, "calendar": False}


def golden(name: str) -> dict[str, Any]:
    return json.loads((GOLDEN / f"{name}.json").read_text())


def run_file(fx: dict[str, Any], *, journal: list[dict[str, Any]] | None = None) -> dict[str, Any]:
    """The run file the web exporter writes for this golden run (same member order)."""
    version = engine_version()
    expected = fx["expected"]
    return {
        "format": "tradelab-run",
        "version": 1,
        "mission": fx["mission"],
        "pack": None,
        "seed": fx["seed"],
        "options": {**DEFAULT_OPTIONS, **(fx.get("options") or {})},
        "engine": {"executionPolicy": version["executionPolicy"], "abi": abi_version(), "version": version["engine"]},
        "labels": {"campaign": False, "assisted": False},
        "actions": fx["actions"],
        "journal": journal or [],
        "result": {"status": expected["status"], "digest": expected["digest"], "eventsDigest": expected["eventsDigest"]},
    }


def active_prefix(fx: dict[str, Any], count: int) -> dict[str, Any]:
    """The first `count` actions of a golden run: a run still in progress, digests from a replay."""
    f = run_file(fx)
    f["actions"] = fx["actions"][:count]
    o = f["options"]
    r = replay(f["mission"], f["seed"], f["actions"], RunOptions(o["adviceAllowance"], o["newsLead"], o["calendar"]))
    f["result"] = {"status": r.status, "digest": r.digest, "eventsDigest": r.state["eventsDigest"]}
    return f


NOTES = [
    {"step": 72, "side": "buy", "thesis": "Range low holds; funding is light.", "invalidation": "97.50", "target": "104.00",
     "plannedRisk": "100.00", "confidence": 60, "skipped": False},
    {"step": 80, "side": "sell", "thesis": "Ação: não opero contra a notícia.", "invalidation": None, "target": None,
     "plannedRisk": None, "confidence": 50, "skipped": True, "loseScenario": "IV crush"},
]


def with_change(base: dict[str, Any], change: Callable[[dict[str, Any]], None]) -> dict[str, Any]:
    f = copy.deepcopy(base)
    change(f)
    return f


def first_order(f: dict[str, Any]) -> dict[str, Any]:
    return next(a for a in f["actions"] if a["k"] == "order")


def valid_files() -> list[tuple[str, str, dict[str, Any]]]:
    return [
        ("m01-finished", "A finished m01 run (golden m01-false-breakout).", run_file(golden("m01-false-breakout"))),
        ("m02-advice-notes", "Start options (advice, fast wire, calendar) and two plan notes included.",
         run_file(golden("m02-advisor-options"), journal=NOTES)),
        ("m08-rejections", "A run whose log has commands the engine rejected (they are part of the run).",
         run_file(golden("m08-regime-switch"))),
        ("m13-call-spread", "Option legs (golden m13-call-spread).", run_file(golden("m13-call-spread"))),
        ("m07-active", "A run still in progress: the first 5 actions of golden m07-trend-trailing.",
         active_prefix(golden("m07-trend-trailing"), 5)),
    ]


def invalid_files(base: dict[str, Any]) -> list[tuple[str, str, Any]]:
    def set_qty(f: dict[str, Any]) -> None:
        first_order(f)["qty"] = str(float(first_order(f)["qty"]) * 2).rstrip("0").rstrip(".")

    text = json.dumps(base, ensure_ascii=False)
    return [
        ("tampered-action", "One order quantity doubled: the replay no longer reproduces the result.", with_change(base, set_qty)),
        ("tampered-digest", "The recorded digest edited.",
         with_change(base, lambda f: f["result"].update(digest="0" * 64))),
        ("tampered-status", "A finished run claiming to be bankrupt.",
         with_change(base, lambda f: f["result"].update(status="bankrupt"))),
        ("other-policy", "Recorded under execution policy 16.",
         with_change(base, lambda f: f["engine"].update(executionPolicy=16))),
        ("unknown-mission", "A mission this engine does not have.", with_change(base, lambda f: f.update(mission="m99-nothing"))),
        ("not-a-run-file", "Another document type.", with_change(base, lambda f: f.update(format="tradelab-pack"))),
        ("future-version", "Format version 2.", with_change(base, lambda f: f.update(version=2))),
        ("unknown-member", "An extra top-level member (a device id).", with_change(base, lambda f: f.update(device="ipad-7"))),
        ("bad-decimal", "A quantity in exponent notation.", with_change(base, lambda f: first_order(f).update(qty="1e3"))),
        ("unknown-action", "An action kind the format does not have.",
         with_change(base, lambda f: f["actions"].insert(0, {"k": "teleport"}))),
        ("long-note", "A plan note longer than 1,000 bytes.",
         with_change(base, lambda f: f.update(journal=[{**NOTES[0], "thesis": "x" * 1001}]))),
        ("pack-missing", "A catalog pack run verified without its pack.",
         with_change(base, lambda f: f.update(pack="ab" * 32, mission="synthetic-sample-a"))),
        ("float-seed", "A non-integer number.", text.replace(f'"seed": {base["seed"]}', '"seed": 1.5', 1)),
        ("truncated", "The first half of a valid file.", text[: len(text) // 2]),
    ]


def encode(content: Any) -> str:
    return content if isinstance(content, str) else json.dumps(content, ensure_ascii=False, indent=1) + "\n"


EXPECTED_KEYS = ("ok", "reason", "path", "status", "digest", "eventsDigest", "fileDigest", "actions", "rejected", "pack", "detail")


def verdict(text: str) -> dict[str, Any]:
    v = verify_run_file(text)
    return {k: v[k] for k in EXPECTED_KEYS if k in v and v[k] != ""}


def build() -> tuple[dict[str, str], dict[str, Any]]:
    files: dict[str, str] = {}
    entries = []
    valid = valid_files()
    cases = [(n, d, c, True) for n, d, c in valid] + [(n, d, c, False) for n, d, c in invalid_files(valid[0][2])]
    for name, description, content, should_pass in cases:
        text = encode(content)
        expected = verdict(text)
        if expected["ok"] != should_pass:
            raise SystemExit(f"{name}: expected ok={should_pass}, engine says {expected}")
        files[f"{name}.tlrun"] = text
        entries.append({"name": f"{name}.tlrun", "description": description, "expected": expected})
    return files, {"format": 1, "files": entries}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write", action="store_true", help="rewrite the fixtures instead of checking them")
    args = parser.parse_args(argv)
    files, index = build()
    files["index.json"] = json.dumps(index, ensure_ascii=False, indent=1) + "\n"
    present = {p.name for p in OUT.glob("*")} if OUT.is_dir() else set()
    stale = sorted(n for n, t in files.items() if not (OUT / n).is_file() or (OUT / n).read_text() != t)
    stale += sorted(present - set(files))
    if args.write:
        OUT.mkdir(parents=True, exist_ok=True)
        for name in present - set(files):
            (OUT / name).unlink()
        for name, text in files.items():
            (OUT / name).write_text(text)
        print(f"wrote {len(files)} files to {OUT.relative_to(ROOT)}")
        return 0
    if stale:
        print("stale run-file fixtures (run tools/make_run_files.py --write):", ", ".join(stale))
        return 1
    print(f"{len(files)} run-file fixtures up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
