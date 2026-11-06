"""Ports (hexagonal architecture): the application depends on these protocols only."""

from __future__ import annotations

from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass
from typing import Any, Protocol

from .domain.models import Action, ErasureEntry, Preference, Run


class RunRepository(Protocol):
    def get(self, run_id: str) -> Run | None: ...
    def add(self, run: Run) -> None: ...
    def save(self, run: Run) -> None: ...
    def actions(self, run_id: str) -> list[tuple[int, Action]]: ...
    def add_actions(self, run_id: str, actions: Sequence[tuple[int, Action]]) -> None: ...
    def runs_of(self, owner: str) -> list[Run]: ...
    def delete_owner(self, owner: str) -> int: ...


class PreferenceStore(Protocol):
    def lock(self, owner: str) -> None:
        """Serializes read-merge-write of one player's preferences until the transaction ends."""
        ...

    def all(self, owner: str) -> dict[str, Preference]: ...
    def put(self, owner: str, prefs: Sequence[Preference]) -> None: ...
    def delete_owner(self, owner: str) -> None: ...


class IdempotencyStore(Protocol):
    def get(self, owner: str, key: str) -> tuple[str, Any] | None: ...
    def put(self, owner: str, key: str, request_hash: str, response: Any) -> None: ...


class Replayer(Protocol):
    def supported_policy(self) -> int:
        """The engine's current execution policy (what new runs are recorded with)."""
        ...

    def supported_policies(self) -> frozenset[int]:
        """Every policy this server can replay: the current one plus the support window's older ones."""
        ...

    def replay(self, mission_id: str, seed: int, actions: Sequence[Action],
               options: Mapping[str, Any] | None = None, policy: int | None = None) -> dict[str, Any]:
        """{digest, status, debrief} with the engine of `policy` (None = current); ValueError on a bad log."""
        ...


class ErasureLog(Protocol):
    def record(self, entry: ErasureEntry) -> None: ...


class WebhookReceipts(Protocol):
    def claim(self, webhook_id: str, event_type: str, at: Any) -> bool: ...


class CsvValidator(Protocol):
    def validate(self, csv: str, meta: Mapping[str, Any]) -> dict[str, Any]: ...


class TokenVerifier(Protocol):
    def verify(self, token: str) -> dict[str, Any]: ...


class Clock(Protocol):
    def now(self) -> Any: ...


@dataclass(frozen=True)
class ObjectInfo:
    size: int
    version: str  # changes whenever the object's bytes change (S3 ETag, file mtime + size)


class ObjectStore(Protocol):
    """Immutable pack objects and the catalog metadata, by key (paths relative to the store root).

    Implemented by the filesystem adapter (`adapters/object_store.py`). An S3/MinIO adapter maps
    `stat` to HeadObject and `read` to GetObject with `Range: bytes=start-(start+length-1)`, so a
    pack is streamed through the API (entitlement check) and never served from a public bucket URL.
    """

    def stat(self, key: str) -> ObjectInfo | None: ...
    def read(self, key: str, start: int = 0, length: int | None = None) -> Iterator[bytes]: ...
