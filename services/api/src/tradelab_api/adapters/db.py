"""SQLAlchemy adapter (PostgreSQL in production, SQLite in unit tests)."""

from __future__ import annotations

import hashlib
import json
from collections.abc import Sequence
from datetime import datetime
from typing import Any

from sqlalchemy import JSON, Boolean, DateTime, ForeignKey, Integer, String, Text, create_engine, delete, select
from sqlalchemy.engine import Engine
from sqlalchemy.orm import DeclarativeBase, Mapped, Session, mapped_column, sessionmaker

from ..domain.models import Action, Run


class Base(DeclarativeBase):
    pass


class RunRow(Base):
    __tablename__ = "runs"
    run_id: Mapped[str] = mapped_column(String(80), primary_key=True)
    owner: Mapped[str] = mapped_column(String(128), index=True)
    mission_id: Mapped[str] = mapped_column(String(64))
    seed: Mapped[int] = mapped_column(Integer)
    execution_policy: Mapped[int] = mapped_column(Integer)
    device_id: Mapped[str] = mapped_column(String(80))
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True))
    actions: Mapped[int] = mapped_column(Integer, default=0)
    status: Mapped[str] = mapped_column(String(16), default="active")
    server_digest: Mapped[str] = mapped_column(String(64), default="")
    client_digest: Mapped[str] = mapped_column(String(64), default="")
    digest_mismatch: Mapped[bool] = mapped_column(Boolean, default=False)
    score_total: Mapped[int | None] = mapped_column(Integer, nullable=True)
    report: Mapped[dict[str, Any] | None] = mapped_column(JSON, nullable=True)


class ActionRow(Base):
    __tablename__ = "run_actions"
    run_id: Mapped[str] = mapped_column(ForeignKey("runs.run_id", ondelete="CASCADE"), primary_key=True)
    seq: Mapped[int] = mapped_column(Integer, primary_key=True)
    action: Mapped[dict[str, Any]] = mapped_column(JSON)


class IdempotencyRow(Base):
    __tablename__ = "idempotency_keys"
    owner: Mapped[str] = mapped_column(String(128), primary_key=True)
    key: Mapped[str] = mapped_column(String(128), primary_key=True)
    request_hash: Mapped[str] = mapped_column(String(64))
    response: Mapped[str] = mapped_column(Text)


_FIELDS = (
    "run_id", "owner", "mission_id", "seed", "execution_policy", "device_id", "created_at", "actions", "status",
    "server_digest", "client_digest", "digest_mismatch", "score_total", "report",
)


def _to_domain(row: RunRow) -> Run:
    return Run(**{f: getattr(row, f) for f in _FIELDS})


def make_engine(url: str) -> Engine:
    kwargs: dict[str, Any] = {"future": True}
    if url.startswith("sqlite"):
        kwargs["connect_args"] = {"check_same_thread": False}
        if ":memory:" in url:
            from sqlalchemy.pool import StaticPool

            kwargs["poolclass"] = StaticPool
    engine = create_engine(url, **kwargs)
    Base.metadata.create_all(engine)
    return engine


class SqlRunRepository:
    """One instance per request (unit of work); commit() persists all changes atomically."""

    def __init__(self, session: Session) -> None:
        self._s = session

    def get(self, run_id: str) -> Run | None:
        row = self._s.get(RunRow, run_id)
        return _to_domain(row) if row else None

    def add(self, run: Run) -> None:
        self._s.add(RunRow(**{f: getattr(run, f) for f in _FIELDS}))
        self._s.flush()

    def save(self, run: Run) -> None:
        row = self._s.get(RunRow, run.run_id)
        if row is None:
            return self.add(run)
        for f in _FIELDS:
            setattr(row, f, getattr(run, f))
        self._s.flush()
        return None

    def actions(self, run_id: str) -> list[tuple[int, Action]]:
        rows = self._s.scalars(select(ActionRow).where(ActionRow.run_id == run_id).order_by(ActionRow.seq))
        return [(r.seq, r.action) for r in rows]

    def add_actions(self, run_id: str, actions: Sequence[tuple[int, Action]]) -> None:
        self._s.add_all(ActionRow(run_id=run_id, seq=seq, action=a) for seq, a in actions)
        self._s.flush()

    def runs_of(self, owner: str) -> list[Run]:
        return [_to_domain(r) for r in self._s.scalars(select(RunRow).where(RunRow.owner == owner))]

    def delete_owner(self, owner: str) -> int:
        ids = list(self._s.scalars(select(RunRow.run_id).where(RunRow.owner == owner)))
        if ids:
            self._s.execute(delete(ActionRow).where(ActionRow.run_id.in_(ids)))
            self._s.execute(delete(RunRow).where(RunRow.run_id.in_(ids)))
        self._s.execute(delete(IdempotencyRow).where(IdempotencyRow.owner == owner))
        self._s.flush()
        return len(ids)


class SqlIdempotencyStore:
    def __init__(self, session: Session) -> None:
        self._s = session

    def get(self, owner: str, key: str) -> tuple[str, Any] | None:
        row = self._s.get(IdempotencyRow, (owner, key))
        return (row.request_hash, json.loads(row.response)) if row else None

    def put(self, owner: str, key: str, request_hash: str, response: Any) -> None:
        self._s.add(IdempotencyRow(owner=owner, key=key, request_hash=request_hash, response=json.dumps(response)))
        self._s.flush()


def request_hash(body: bytes) -> str:
    return hashlib.sha256(body).hexdigest()


def session_factory(engine: Engine) -> sessionmaker[Session]:
    return sessionmaker(bind=engine, expire_on_commit=False, future=True)
