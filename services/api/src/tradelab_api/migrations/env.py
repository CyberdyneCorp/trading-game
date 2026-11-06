"""Alembic environment. The app passes its own connection (config.attributes["connection"]); the
CLI (`uv run alembic ...` in services/api) connects to TRADELAB_DATABASE_URL."""

from __future__ import annotations

from logging.config import fileConfig

from alembic import context
from sqlalchemy import Connection

from tradelab_api.adapters.db import Base, make_engine
from tradelab_api.config import Settings

config = context.config
if config.config_file_name is not None:  # the CLI (alembic.ini); the app keeps its own logging
    fileConfig(config.config_file_name, disable_existing_loggers=False)
target_metadata = Base.metadata


def _run(connection: Connection) -> None:
    context.configure(connection=connection, target_metadata=target_metadata,
                      render_as_batch=connection.dialect.name == "sqlite")
    with context.begin_transaction():
        context.run_migrations()


def run_migrations_online() -> None:
    connection = config.attributes.get("connection")
    if connection is not None:
        _run(connection)
        return
    engine = make_engine(Settings().database_url)
    with engine.connect() as conn:
        _run(conn)
    engine.dispose()


if context.is_offline_mode():
    raise SystemExit("offline (--sql) migrations are not supported: connect to a database")
run_migrations_online()
