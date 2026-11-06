#!/usr/bin/env python3
"""Fails when the Raven commit pinned in apps/desktop/cmake/Dependencies.cmake differs from the one the
desktop README and the add-native-clients OpenSpec change name (design "Bumping").

    python3 apps/desktop/scripts/check_raven_pin.py [--root <repo root>]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

CMAKE = Path("apps/desktop/cmake/Dependencies.cmake")
# Every document that names the pinned commit must name the same one.
DOCS = (
    Path("apps/desktop/README.md"),
    Path("openspec/changes/add-native-clients/design.md"),
    Path("openspec/changes/add-native-clients/proposal.md"),
    Path("openspec/changes/add-native-clients/tasks.md"),
    Path("openspec/changes/add-native-clients/specs/desktop-client/spec.md"),
)
REQUIRED = (Path("apps/desktop/README.md"), Path("openspec/changes/add-native-clients/design.md"))

SHA_RE = re.compile(r"\b[0-9a-f]{40}\b")
CMAKE_RE = re.compile(r"set\(TRADELAB_RAVEN_SHA\s+([0-9a-f]{40})\)")


def raven_shas(text: str) -> set[str]:
    """40-hex shas on lines that talk about Raven (other pins, e.g. Vulkan-Headers, are ignored)."""
    return {sha for line in text.splitlines() if "raven" in line.lower() for sha in SHA_RE.findall(line)}


def check(root: Path) -> list[str]:
    cmake = (root / CMAKE).read_text(encoding="utf-8")
    m = CMAKE_RE.search(cmake)
    if not m:
        return [f"{CMAKE}: no `set(TRADELAB_RAVEN_SHA <40-hex sha>)`"]
    pinned = m.group(1)
    problems = []
    for doc in DOCS:
        path = root / doc
        if not path.exists():
            if doc in REQUIRED:
                problems.append(f"{doc}: missing")
            continue
        shas = raven_shas(path.read_text(encoding="utf-8"))
        if doc in REQUIRED and pinned not in shas:
            problems.append(f"{doc}: does not name the pinned Raven commit {pinned}")
        for other in sorted(shas - {pinned}):
            problems.append(f"{doc}: names Raven commit {other}, but {CMAKE} pins {pinned}")
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args(argv)
    problems = check(args.root)
    for p in problems:
        print(f"raven pin: {p}", file=sys.stderr)
    if not problems:
        print("raven pin: build files and documentation agree")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
