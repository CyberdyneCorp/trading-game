"""Alembic migrations run from code (startup and tests) on the app's own engine.

The schema is defined only by `migrations/versions/`; nothing calls `Base.metadata.create_all`.
`pending_model_changes` is the autogenerate check CI runs: models and migrations must agree.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from alembic import command
from alembic.autogenerate import compare_metadata
from alembic.config import Config
from alembic.migration import MigrationContext
from sqlalchemy import text
from sqlalchemy.engine import Connection, Engine

from .db import Base

SCRIPTS = Path(__file__).resolve().parent.parent / "migrations"
# pg_advisory_xact_lock key serializing schema changes (any constant shared by every API process).
_LOCK_KEY = 0x7472_6164_656C_6162 & 0x7FFF_FFFF_FFFF_FFFF  # "tradelab"


def _config() -> Config:
    cfg = Config()
    cfg.set_main_option("script_location", str(SCRIPTS))
    return cfg


def _serialize(conn: Connection) -> None:
    """Several API processes start at once (uvicorn workers, rolling deploys): on PostgreSQL the first
    one migrates while the others wait, then find the schema at head. Held until the transaction ends."""
    if conn.dialect.name == "postgresql":
        conn.execute(text("SELECT pg_advisory_xact_lock(:k)"), {"k": _LOCK_KEY})


def upgrade(engine: Engine, revision: str = "head") -> None:
    with engine.begin() as conn:
        _serialize(conn)
        cfg = _config()
        cfg.attributes["connection"] = conn
        command.upgrade(cfg, revision)


def downgrade(engine: Engine, revision: str) -> None:
    with engine.begin() as conn:
        _serialize(conn)
        cfg = _config()
        cfg.attributes["connection"] = conn
        command.downgrade(cfg, revision)


def pending_model_changes(engine: Engine) -> list[Any]:
    """Differences between the migrated database and the SQLAlchemy models (empty = in sync)."""
    with engine.connect() as conn:
        return list(compare_metadata(MigrationContext.configure(conn), Base.metadata))
