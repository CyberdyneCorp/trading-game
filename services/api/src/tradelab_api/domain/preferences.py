"""Player preferences: last writer wins per key (spec game-api "Preferences").

Each key carries the time the client changed it (`updatedAt`, ms since the epoch). A write replaces
the stored value only when it is newer; equal times are broken by the canonical JSON of the value,
so merging the same writes in any order gives the same result. A time in the future is clamped to
the server's clock, so a device with a fast clock cannot pin a value forever.
"""

from __future__ import annotations

import json
import re
from collections.abc import Iterable, Mapping
from typing import Any

from .errors import LimitExceeded
from .models import Preference

KEY_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,63}$")
MAX_KEYS = 100
MAX_VALUE_BYTES = 4096


def canonical(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def wins(incoming: Preference, stored: Preference | None) -> bool:
    if stored is None:
        return True
    return (incoming.updated_at, canonical(incoming.value)) > (stored.updated_at, canonical(stored.value))


def merge(stored: Mapping[str, Preference], incoming: Iterable[Preference], now_ms: int) -> list[Preference]:
    """The incoming preferences that replace the stored ones (clamped to `now_ms`)."""
    changed: dict[str, Preference] = {}
    for pref in incoming:
        pref = Preference(pref.key, pref.value, min(pref.updated_at, now_ms))
        if wins(pref, changed.get(pref.key) or stored.get(pref.key)):
            changed[pref.key] = pref
    if len(set(stored) | set(changed)) > MAX_KEYS:
        raise LimitExceeded(f"at most {MAX_KEYS} preference keys")
    return list(changed.values())
