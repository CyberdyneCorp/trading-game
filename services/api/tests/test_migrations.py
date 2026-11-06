"""Alembic: the migrations build exactly the schema of the models (the autogenerate check CI runs on
SQLite and PostgreSQL), they downgrade cleanly, and the app no longer creates tables itself."""

from __future__ import annotations

from conftest import database_url_for_tests
from sqlalchemy import inspect

from tradelab_api.adapters import migrations
from tradelab_api.adapters.db import Base, make_engine
from tradelab_api.config import Settings

TABLES = {"runs", "run_actions", "idempotency_keys", "webhook_receipts", "erasure_log"}


def test_models_and_migrations_agree():
    engine = make_engine(database_url_for_tests())
    migrations.upgrade(engine)
    assert migrations.pending_model_changes(engine) == []
    assert TABLES <= set(inspect(engine).get_table_names())
    engine.dispose()


def test_downgrade_to_baseline_and_back():
    engine = make_engine(database_url_for_tests())
    migrations.upgrade(engine)
    migrations.downgrade(engine, "0001")
    names = set(inspect(engine).get_table_names())
    assert {"runs", "run_actions", "idempotency_keys"} <= names
    assert not names & {"webhook_receipts", "erasure_log"}
    migrations.upgrade(engine)
    assert migrations.pending_model_changes(engine) == []
    engine.dispose()


def test_every_model_table_comes_from_a_migration():
    assert set(Base.metadata.tables) == TABLES


def test_regression_engine_creation_does_not_create_tables():
    # Before: make_engine called Base.metadata.create_all, so the schema never went through Alembic.
    engine = make_engine(database_url_for_tests())  # PostgreSQL: emptied first
    assert inspect(engine).get_table_names() == []
    engine.dispose()


def test_startup_migrates_unless_disabled(client_with):
    assert Settings().migrate_on_start is True
    client = client_with(migrate_on_start=False)
    assert client.get("/api/v1/health").status_code == 200  # the release step owns the schema then
