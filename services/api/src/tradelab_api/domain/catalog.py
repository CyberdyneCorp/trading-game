"""Pack catalog metadata and the entitlement gate (spec game-api "Pack delivery").

The catalog source is the static manifest the web client reads (`apps/web/static/catalog/catalog.json`,
schema 1: `{schemaVersion, packs: [{id, file, digest, size, scenarioId, keyId, intervalSeconds, bars,
leverageCap, mode?, title, summary}]}`) with one optional field per pack: `entitlement`, a CyberdyneAuth
entitlement (`product_key` or `product_key:plan`). A pack without it is free.

The API publishes the same manifest, minus `entitlement`, with each `file` rewritten to `packs/<digest>`:
relative to `GET /api/v1/scenarios` it names `GET /api/v1/packs/<digest>`, so the web client only needs
VITE_CATALOG_URL pointed at the API. Entitled packs are listed only to players holding the entitlement;
fetching one without it fails closed (no token: unauthenticated; token without it: forbidden).
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any

from .errors import Forbidden, Unauthenticated

SCHEMA_VERSION = 1
DIGEST_RE = re.compile(r"^[0-9a-f]{64}$")
ENTITLEMENT_RE = re.compile(r"^[A-Za-z0-9_.-]{1,80}(:[A-Za-z0-9_.-]{1,80})?$")
_KEY_RE = re.compile(r"^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$")


@dataclass(frozen=True)
class PackEntry:
    id: str
    digest: str
    key: str  # object key of the .tlpack in the object store (the source manifest's `file`)
    entitlement: str | None
    published: dict[str, Any]  # the manifest entry the API publishes

    @property
    def free(self) -> bool:
        return self.entitlement is None


def _object_key(file: Any) -> str | None:
    if not isinstance(file, str) or not _KEY_RE.fullmatch(file):
        return None
    return None if any(part in {".", ".."} for part in file.split("/")) else file


def _entry(raw: Any) -> PackEntry | None:
    if not isinstance(raw, dict) or not isinstance(raw.get("id"), str):
        return None
    digest, key, entitlement = raw.get("digest"), _object_key(raw.get("file")), raw.get("entitlement")
    if not isinstance(digest, str) or not DIGEST_RE.fullmatch(digest) or key is None:
        return None
    if entitlement is not None and (not isinstance(entitlement, str) or not ENTITLEMENT_RE.fullmatch(entitlement)):
        return None
    published = {k: v for k, v in raw.items() if k != "entitlement"}
    published["file"] = f"packs/{digest}"
    return PackEntry(raw["id"], digest, key, entitlement, published)


def parse_catalog(doc: Any) -> list[PackEntry]:
    """Entries of a source manifest; a wrong schema yields none, a malformed entry is skipped."""
    if not isinstance(doc, dict) or doc.get("schemaVersion") != SCHEMA_VERSION or not isinstance(doc.get("packs"), list):
        return []
    entries: list[PackEntry] = []
    for entry in map(_entry, doc["packs"]):
        if entry is not None and all(e.digest != entry.digest and e.id != entry.id for e in entries):
            entries.append(entry)
    return entries


def has_entitlement(required: str, claims: dict[str, Any] | None) -> bool:
    """`product` is satisfied by `product` or any `product:plan`; `product:plan` only by itself.
    A missing or malformed `entitlements` claim grants nothing (fail closed)."""
    granted = (claims or {}).get("entitlements")
    if not isinstance(granted, list):
        return False
    product, _, plan = required.partition(":")
    for item in granted:
        if isinstance(item, str) and (item == required or (not plan and item.partition(":")[0] == product)):
            return True
    return False


def visible(entry: PackEntry, claims: dict[str, Any] | None) -> bool:
    return entry.free or has_entitlement(entry.entitlement or "", claims)


def check_access(entry: PackEntry, claims: dict[str, Any] | None) -> None:
    if entry.free:
        return
    if claims is None:
        raise Unauthenticated("this pack needs a signed-in player")
    if not has_entitlement(entry.entitlement or "", claims):
        raise Forbidden("missing entitlement")


def manifest(entries: list[PackEntry], claims: dict[str, Any] | None) -> dict[str, Any]:
    return {"schemaVersion": SCHEMA_VERSION, "packs": [e.published for e in entries if visible(e, claims)]}
