"""Catalog use cases: list the packs a caller may see, describe one, open one for download."""

from __future__ import annotations

import json
import threading
from collections.abc import Iterator
from typing import Any

from ..domain.catalog import PackEntry, check_access, manifest, parse_catalog
from ..domain.errors import NotFound
from ..ports import ObjectInfo, ObjectStore

CATALOG_KEY = "catalog.json"


class CatalogService:
    """Reads the source manifest from the object store, re-parsing it only when its version changes."""

    def __init__(self, store: ObjectStore | None, catalog_key: str = CATALOG_KEY) -> None:
        self._store = store
        self._key = catalog_key
        self._lock = threading.Lock()
        self._cached: tuple[str, list[PackEntry]] | None = None

    def entries(self) -> list[PackEntry]:
        info = self._store.stat(self._key) if self._store else None
        if self._store is None or info is None:
            return []
        with self._lock:
            if self._cached is None or self._cached[0] != info.version:
                raw = b"".join(self._store.read(self._key))
                try:
                    doc = json.loads(raw)
                except ValueError:
                    doc = None
                self._cached = (info.version, parse_catalog(doc))
            return self._cached[1]

    def listing(self, claims: dict[str, Any] | None) -> dict[str, Any]:
        return manifest(self.entries(), claims)

    def describe(self, pack_id: str, claims: dict[str, Any] | None) -> dict[str, Any]:
        entry = next((e for e in self.entries() if e.id == pack_id), None)
        if entry is None:
            raise NotFound()
        check_access(entry, claims)
        return entry.published

    def open(self, digest: str, claims: dict[str, Any] | None) -> tuple[PackEntry, ObjectInfo]:
        entry = next((e for e in self.entries() if e.digest == digest), None)
        info = self._store.stat(entry.key) if entry is not None and self._store is not None else None
        if entry is None or info is None:
            raise NotFound()
        check_access(entry, claims)
        return entry, info

    def read(self, entry: PackEntry, start: int, length: int) -> Iterator[bytes]:
        if self._store is None:
            raise NotFound()
        return self._store.read(entry.key, start, length)
