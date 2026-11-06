"""Replays runs with the engine of the execution policy they were recorded with (spec game-api
"Engine version support").

The current policy replays in-process through the installed binding. Older policies inside the
support window (the last `window` policy versions, the current one included) replay in a process
pool per policy whose workers load that policy's library and binding from the versioned cache built
by tools/build_policy_cache.py (`<cache>/policy-<v>/`). A policy outside the window or missing from
the cache is unsupported (the sync reports `unsupported_policy` for that run).
"""

from __future__ import annotations

import json
import multiprocessing
import threading
from collections.abc import Mapping, Sequence
from concurrent.futures import ProcessPoolExecutor
from concurrent.futures import TimeoutError as FutureTimeout
from concurrent.futures.process import BrokenProcessPool
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ..domain.errors import ReplayUnavailable, UnsupportedPolicy
from ..domain.models import Action
from . import policy_worker
from .engine_replayer import EngineReplayer


@dataclass(frozen=True)
class CachedPolicy:
    policy: int
    directory: Path  # holds the binding package `tradelab_engine/` of that policy
    library: Path
    commit: str


def discover(cache_dir: str | Path | None) -> dict[int, CachedPolicy]:
    """Cached policies with their library present; malformed entries are ignored."""
    found: dict[int, CachedPolicy] = {}
    root = Path(cache_dir) if cache_dir else None
    if root is None or not root.is_dir():
        return found
    for meta_path in sorted(root.glob("policy-*/meta.json")):
        try:
            meta = json.loads(meta_path.read_text())
            policy = int(meta["policy"])
            library = meta_path.parent / "tradelab_engine" / str(meta["library"])
        except (OSError, ValueError, KeyError, TypeError):
            continue
        if library.is_file() and meta_path.parent.name == f"policy-{policy}":
            found[policy] = CachedPolicy(policy, meta_path.parent, library, str(meta.get("commit", "")))
    return found


class PolicyReplayer:
    def __init__(self, current: EngineReplayer, cache_dir: str | Path | None = None, *, window: int = 3,
                 workers: int = 2, timeout: float = 60.0) -> None:
        self._current = current
        now = current.supported_policy()
        self._cached = {p: c for p, c in discover(cache_dir).items() if now - window < p < now}
        self._workers = workers
        self._timeout = timeout
        self._pools: dict[int, ProcessPoolExecutor] = {}
        self._lock = threading.Lock()

    def supported_policy(self) -> int:
        return self._current.supported_policy()

    def supported_policies(self) -> frozenset[int]:
        return frozenset({self.supported_policy(), *self._cached})

    def missions(self) -> list[dict[str, Any]]:
        return self._current.missions()

    def replay(self, mission_id: str, seed: int, actions: Sequence[Action],
               options: Mapping[str, Any] | None = None, policy: int | None = None) -> dict[str, Any]:
        if policy is None or policy == self.supported_policy():
            return self._current.replay(mission_id, seed, actions, options)
        if policy not in self._cached:
            raise UnsupportedPolicy()
        pool = self._pool(policy)
        try:
            out = pool.submit(policy_worker.replay, mission_id, seed, list(actions),
                              dict(options) if options else None).result(self._timeout)
        except (BrokenProcessPool, FutureTimeout) as exc:
            self._discard(policy, pool)
            raise ReplayUnavailable(f"policy {policy} replay worker failed") from exc
        if "error" in out:
            raise ValueError(out["error"])
        return out

    def _pool(self, policy: int) -> ProcessPoolExecutor:
        with self._lock:
            pool = self._pools.get(policy)
            if pool is None:
                cached = self._cached[policy]
                pool = ProcessPoolExecutor(
                    max_workers=self._workers, mp_context=multiprocessing.get_context("spawn"),
                    initializer=policy_worker.init,
                    initargs=(str(cached.directory), str(cached.library), policy))
                self._pools[policy] = pool
            return pool

    def _discard(self, policy: int, pool: ProcessPoolExecutor) -> None:
        with self._lock:
            if self._pools.get(policy) is pool:
                del self._pools[policy]
        _terminate(pool)

    def close(self) -> None:
        with self._lock:
            pools, self._pools = list(self._pools.values()), {}
        for pool in pools:
            pool.shutdown(wait=True, cancel_futures=True)


def _terminate(pool: ProcessPoolExecutor) -> None:
    """Stops a pool whose worker hangs or died (shutdown alone would wait for a stuck replay)."""
    processes = list((getattr(pool, "_processes", None) or {}).values())
    pool.shutdown(wait=False, cancel_futures=True)
    for process in processes:
        process.terminate()
