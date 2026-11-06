"""Per-player rate limit (spec game-api "Limits"): one token bucket per verified `sub`.

Buckets live in process memory, so each API process limits on its own (the deployment runs one).
A bucket holds `burst` tokens and refills at `per_minute / 60` tokens a second; a request takes one
token or is refused with the seconds until the next token is available.
"""

from __future__ import annotations

import math
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass


@dataclass
class _Bucket:
    tokens: float
    updated: float


class TokenBuckets:
    MAX_KEYS = 10_000  # above this, full (idle) buckets are dropped: they hold no information

    def __init__(self, per_minute: float, burst: int, clock: Callable[[], float] = time.monotonic) -> None:
        self._rate = per_minute / 60.0
        self._burst = float(max(burst, 1))
        self._clock = clock
        self._buckets: dict[str, _Bucket] = {}
        self._lock = threading.Lock()

    @property
    def enabled(self) -> bool:
        return self._rate > 0

    def take(self, key: str) -> float:
        """0 when the request may proceed, otherwise the seconds to wait (Retry-After)."""
        if not self.enabled:
            return 0.0
        with self._lock:
            now = self._clock()
            bucket = self._buckets.get(key)
            if bucket is None:
                self._prune(now)
                bucket = self._buckets[key] = _Bucket(self._burst, now)
            bucket.tokens = min(self._burst, bucket.tokens + (now - bucket.updated) * self._rate)
            bucket.updated = now
            if bucket.tokens >= 1.0:
                bucket.tokens -= 1.0
                return 0.0
            return (1.0 - bucket.tokens) / self._rate

    def _prune(self, now: float) -> None:
        if len(self._buckets) < self.MAX_KEYS:
            return
        full = [k for k, b in self._buckets.items() if b.tokens + (now - b.updated) * self._rate >= self._burst]
        for k in full:
            del self._buckets[k]


def retry_after_header(wait: float) -> str:
    return str(max(1, math.ceil(wait)))
