#!/usr/bin/env python3
"""Checks or regenerates the shared golden fixtures in engine/tests/golden (format: engine/README.md).

  python3 tools/regen_golden.py           # check: exit 1 when an "expected"/"versions" block or the
                                          # file layout differs from what this engine build produces
  python3 tools/regen_golden.py --write   # rewrite them (only for an intentional policy change)

The inputs (market, seed, options, actions, description) are hand-written and never rewritten; the
script replays them through the Python binding (libtradelab from engine/build/native-release, or
TRADELAB_LIB) and fills "expected" and "versions".
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "bindings" / "python"), str(ROOT / "bindings" / "python" / "tests")]

import golden  # noqa: E402
from tradelab_engine import engine_version  # noqa: E402

WIDTH = 100


def dump(value: Any, indent: int = 0) -> str:
    """JSON with one record (a flat object, such as an action) or short container per line: diff-friendly
    and the same on every run."""
    flat = json.dumps(value, ensure_ascii=False)
    scalar_record = isinstance(value, dict) and not any(isinstance(v, (dict, list)) for v in value.values())
    if scalar_record or not isinstance(value, (dict, list)) or len(flat) + indent <= WIDTH or not value:
        return flat
    pad = " " * (indent + 2)
    if isinstance(value, dict):
        body = [f"{pad}{json.dumps(k)}: {dump(v, indent + 2)}" for k, v in value.items()]
        return "{\n" + ",\n".join(body) + "\n" + " " * indent + "}"
    body = [pad + dump(v, indent + 2) for v in value]
    return "[\n" + ",\n".join(body) + "\n" + " " * indent + "]"


def regenerate(fx: dict[str, Any]) -> dict[str, Any]:
    out = {k: v for k, v in fx.items() if k not in ("expected", "versions")}
    out["expected"] = golden.actual(fx)
    out["versions"] = engine_version()
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write", action="store_true", help="rewrite the fixtures instead of checking them")
    args = parser.parse_args(argv)
    stale = []
    for name in golden.index():
        path = golden.GOLDEN_DIR / f"{name}.json"
        text = dump(regenerate(golden.load(name))) + "\n"
        if path.read_text() == text:
            continue
        stale.append(name)
        if args.write:
            path.write_text(text)
    verb = "rewrote" if args.write else "stale"
    for name in stale:
        print(f"{verb}: engine/tests/golden/{name}.json")
    return 0 if args.write or not stale else 1


if __name__ == "__main__":
    sys.exit(main())
