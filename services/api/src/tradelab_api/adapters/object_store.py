"""Filesystem object store (ports.ObjectStore): keys are paths below a root directory.

Used in development and tests (TRADELAB_PACKS_DIR, for example the web client's static catalog
directory). Production storage is MinIO behind the same port (an S3 adapter: HeadObject/GetObject).
"""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

from ..ports import ObjectInfo

CHUNK = 64 * 1024


class FilesystemObjectStore:
    def __init__(self, root: str | Path) -> None:
        self._root = Path(root).resolve()

    def _path(self, key: str) -> Path | None:
        if not key or key.startswith("/") or "\\" in key:
            return None
        path = (self._root / key).resolve()
        return path if path.is_relative_to(self._root) and path.is_file() else None

    def stat(self, key: str) -> ObjectInfo | None:
        path = self._path(key)
        if path is None:
            return None
        st = path.stat()
        return ObjectInfo(st.st_size, f"{st.st_mtime_ns}-{st.st_size}")

    def read(self, key: str, start: int = 0, length: int | None = None) -> Iterator[bytes]:
        path = self._path(key)
        if path is None:
            raise FileNotFoundError(key)
        return self._chunks(path, start, length)

    @staticmethod
    def _chunks(path: Path, start: int, length: int | None) -> Iterator[bytes]:
        with path.open("rb") as f:
            f.seek(start)
            left = length
            while left is None or left > 0:
                chunk = f.read(CHUNK if left is None else min(CHUNK, left))
                if not chunk:
                    return
                if left is not None:
                    left -= len(chunk)
                yield chunk
