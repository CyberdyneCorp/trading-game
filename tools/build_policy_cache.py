#!/usr/bin/env python3
"""Builds the engine C ABI library of older execution-policy versions into a versioned cache.

The game server replays a run only with the execution policy it was recorded with
(spec game-api "Engine version support"). For the support window (the last N policy versions,
the current one included) this script finds, on the first-parent history of a ref, the newest
commit that still carried each older policy, builds `tradelab_capi` from that commit and stores
the library together with that commit's Python binding:

    <cache>/policy-<v>/meta.json          {"policy", "commit", "engine", "library"}
    <cache>/policy-<v>/tradelab_engine/   binding package of that commit + libtradelab.{so,dylib}

The current policy is not cached: the server replays it with the binding it was installed with.
Entries already built from the same commit are kept; entries outside the window are removed
(--keep to retain them). Needs git history (a full clone), CMake and a C++20 compiler.

    python3 tools/build_policy_cache.py --cache services/api/.policy-cache --window 3
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
HEADER = "engine/include/tradelab/engine.hpp"
POLICY_RE = re.compile(r"kExecutionPolicyVersion\s*=\s*(\d+)")
LIB_NAME = {"darwin": "libtradelab.dylib", "win32": "tradelab.dll"}.get(sys.platform, "libtradelab.so")


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(REPO), *args], check=True, capture_output=True, text=True).stdout


def policy_at(commit: str) -> int | None:
    try:
        text = git("show", f"{commit}:{HEADER}")
    except subprocess.CalledProcessError:
        return None
    m = POLICY_RE.search(text)
    return int(m.group(1)) if m else None


def policy_commits(ref: str, wanted: set[int]) -> dict[int, str]:
    """Newest first-parent commit carrying each wanted policy (the state just before its bump)."""
    found: dict[int, str] = {}
    head = git("rev-parse", ref).strip()
    current = policy_at(head)
    if current in wanted:
        found[current] = head
    for commit in git("log", "--first-parent", "--format=%H", ref, "--", HEADER).split():
        if wanted <= found.keys():
            break
        parent = f"{commit}^1"
        before = policy_at(parent)
        if before in wanted and before not in found and before != policy_at(commit):
            found[before] = git("rev-parse", parent).strip()
    return found


def cached_commit(entry: Path) -> str | None:
    try:
        meta = json.loads((entry / "meta.json").read_text())
    except (OSError, ValueError):
        return None
    lib = entry / "tradelab_engine" / str(meta.get("library", ""))
    return str(meta.get("commit")) if lib.is_file() else None


def build_library(commit: str, work: Path) -> Path:
    """Exports `engine/` and the binding of `commit` into `work` and builds the C ABI library."""
    archive = subprocess.run(["git", "-C", str(REPO), "archive", commit, "engine", "bindings/python/tradelab_engine"],
                             check=True, capture_output=True).stdout
    subprocess.run(["tar", "-x", "-C", str(work)], input=archive, check=True)
    build = work / "build"
    generator = ["-G", "Ninja"] if shutil.which("ninja") else []
    subprocess.run(["cmake", "-S", str(work / "engine"), "-B", str(build), *generator, "-DCMAKE_BUILD_TYPE=Release",
                    "-DTRADELAB_BUILD_TESTS=OFF"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", str(build), "--target", "tradelab_capi", "-j", str(os.cpu_count() or 4)],
                   check=True, stdout=subprocess.DEVNULL)
    return build / LIB_NAME


def probe(package_dir: Path, lib: Path) -> dict[str, object]:
    """Loads the built library through its own binding in a fresh interpreter."""
    code = "import json, tradelab_engine as te; print(json.dumps(te.engine_version()))"
    env = {**os.environ, "TRADELAB_LIB": str(lib), "PYTHONPATH": str(package_dir)}
    out = subprocess.run([sys.executable, "-c", code], check=True, capture_output=True, text=True, env=env,
                         cwd=package_dir)
    return json.loads(out.stdout)


def install(policy: int, commit: str, cache: Path) -> None:
    with tempfile.TemporaryDirectory(prefix=f"tradelab-policy-{policy}-") as tmp:
        work = Path(tmp)
        lib = build_library(commit, work)
        staging = cache / f".policy-{policy}.tmp"
        shutil.rmtree(staging, ignore_errors=True)
        shutil.copytree(work / "bindings" / "python" / "tradelab_engine", staging / "tradelab_engine")
        shutil.copy2(lib, staging / "tradelab_engine" / LIB_NAME)
        version = probe(staging, staging / "tradelab_engine" / LIB_NAME)
        if version.get("executionPolicy") != policy:
            raise SystemExit(f"commit {commit[:10]} built policy {version.get('executionPolicy')}, expected {policy}")
        meta = {"policy": policy, "commit": commit, "engine": version.get("engine"), "library": LIB_NAME}
        (staging / "meta.json").write_text(json.dumps(meta, indent=1) + "\n")
        target = cache / f"policy-{policy}"
        shutil.rmtree(target, ignore_errors=True)
        staging.rename(target)


def prune(cache: Path, keep: set[int]) -> None:
    for entry in cache.glob("policy-*"):
        m = re.fullmatch(r"policy-(\d+)", entry.name)
        if m and int(m.group(1)) not in keep:
            shutil.rmtree(entry)
            print(f"removed {entry.name} (outside the support window)")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--cache", type=Path, default=REPO / "services" / "api" / ".policy-cache")
    ap.add_argument("--window", type=int, default=3, help="policy versions supported, the current one included")
    ap.add_argument("--ref", default="HEAD", help="history to search (first parent)")
    ap.add_argument("--keep", action="store_true", help="keep cache entries outside the window")
    args = ap.parse_args(argv)
    current = policy_at(git("rev-parse", args.ref).strip())
    if current is None:
        raise SystemExit(f"no execution policy found in {HEADER} at {args.ref}")
    older = {current - i for i in range(1, args.window) if current - i >= 1}
    commits = policy_commits(args.ref, older)
    args.cache.mkdir(parents=True, exist_ok=True)
    for policy in sorted(older):
        commit = commits.get(policy)
        if commit is None:
            print(f"policy {policy}: no commit in the history of {args.ref} (shallow clone?)", file=sys.stderr)
            return 1
        if cached_commit(args.cache / f"policy-{policy}") == commit:
            print(f"policy {policy}: cached ({commit[:10]})")
            continue
        print(f"policy {policy}: building {commit[:10]}")
        install(policy, commit, args.cache)
    if not args.keep:
        prune(args.cache, older)
    return 0


if __name__ == "__main__":
    sys.exit(main())
