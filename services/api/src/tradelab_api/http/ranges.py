"""HTTP conditional and range requests for immutable packs (RFC 9110 sections 13 and 14).

One `bytes=` range is served (`a-b`, `a-`, `-n`); a malformed or multi-range header is ignored
(the whole pack is sent with 200, as the RFC allows); a range starting past the end is 416.
`If-Range` with another validator also falls back to the whole pack, and `If-None-Match` with the
pack's ETag (or `*`) answers 304.
"""

from __future__ import annotations

import re

_RANGE_RE = re.compile(r"^bytes=(\d*)-(\d*)$")


class Unsatisfiable(Exception):
    pass


def byte_range(header: str | None, size: int) -> tuple[int, int] | None:
    """(start, end inclusive) of a single satisfiable range, None to send everything."""
    m = _RANGE_RE.fullmatch((header or "").strip())
    if m is None or (m.group(1) == "" and m.group(2) == ""):
        return None
    if m.group(1) == "":
        suffix = int(m.group(2))
        if suffix == 0 or size == 0:
            raise Unsatisfiable()
        return max(0, size - suffix), size - 1
    start = int(m.group(1))
    if m.group(2) and int(m.group(2)) < start:
        return None  # syntactically invalid: ignored
    if start >= size:
        raise Unsatisfiable()
    return start, min(int(m.group(2)), size - 1) if m.group(2) else size - 1


def matches(header: str | None, etag: str) -> bool:
    """If-None-Match: `*` or a list containing the ETag (weak comparison)."""
    if not header:
        return False
    tags = [t.strip().removeprefix("W/") for t in header.split(",")]
    return "*" in tags or etag in tags


def range_applies(if_range: str | None, etag: str) -> bool:
    """A Range is honoured unless If-Range names another (or a date) validator."""
    return if_range is None or if_range.strip() == etag
