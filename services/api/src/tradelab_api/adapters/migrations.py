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
from sqlalchemy.engine import Engine

from .db import Base

SCRIPTS = Path(__file__).resolve().parent.parent / "migrations"


def _config() -> Config:
    cfg = Config()
    cfg.set_main_option("script_location", str(SCRIPTS))
    return cfg


def upgrade(engine: Engine, revision: str = "head") -> None:
    with engine.begin() as conn:
        cfg = _config()
        cfg.attributes["connection"] = conn
        command.upgrade(cfg, revision)


def downgrade(engine: Engine, revision: str) -> None:
    with engine.begin() as conn:
        cfg = _config()
        cfg.attributes["connection"] = conn
        command.downgrade(cfg, revision)


def pending_model_changes(engine: Engine) -> list[Any]:
    """Differences between the migrated database and the SQLAlchemy models (empty = in sync)."""
    with engine.connect() as conn:
        return list(compare_metadata(MigrationContext.configure(conn), Base.metadata))
