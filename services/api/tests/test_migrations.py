"""Alembic: the migrations build exactly the schema of the models (the autogenerate check CI runs on
SQLite and PostgreSQL), they downgrade cleanly, and the app no longer creates tables itself."""

from __future__ import annotations

import multiprocessing

import pytest
from conftest import database_url_for_tests
from sqlalchemy import inspect

from tradelab_api.adapters import migrations
from tradelab_api.adapters.db import Base, make_engine
from tradelab_api.config import Settings

TABLES = {"runs", "run_actions", "idempotency_keys", "webhook_receipts", "erasure_log", "preferences"}


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
    assert not names & {"webhook_receipts", "erasure_log", "preferences"}
    assert "campaign" not in {c["name"] for c in inspect(engine).get_columns("runs")}
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


def test_upgrade_from_0002_keeps_runs_and_adds_campaign_and_preferences():
    # A database migrated before preferences and campaign links existed: its runs survive the upgrade.
    from sqlalchemy import text

    engine = make_engine(database_url_for_tests())
    migrations.upgrade(engine, "0002")
    with engine.begin() as conn:
        conn.execute(text(
            "INSERT INTO runs (run_id, owner, mission_id, seed, execution_policy, device_id, created_at, actions,"
            " status, server_digest, client_digest, digest_mismatch) VALUES ('run-old-0001', 'p', 'm01', 1, 15,"
            " 'd', '2026-01-01T00:00:00+00:00', 0, 'active', '', '', false)"))
    migrations.upgrade(engine)
    with engine.connect() as conn:
        assert conn.execute(text("SELECT run_id, campaign FROM runs")).all() == [("run-old-0001", None)]
        assert conn.execute(text("SELECT count(*) FROM preferences")).scalar() == 0
    assert migrations.pending_model_changes(engine) == []
    engine.dispose()


def _upgrade_after(url: str, barrier) -> str:
    engine = make_engine(url)
    barrier.wait()
    try:
        migrations.upgrade(engine)
        return "ok"
    except Exception as exc:  # noqa: BLE001
        return type(exc).__name__
    finally:
        engine.dispose()


def test_regression_concurrent_startups_migrate_once():
    # Before: API processes starting together on an empty PostgreSQL (uvicorn --workers, a rolling
    # deploy) all ran the DDL at once and every one but the first crashed with a UniqueViolation.
    url = database_url_for_tests()
    if url.startswith("sqlite"):
        pytest.skip("concurrent schema changes need a server database (CI runs this on PostgreSQL)")
    ctx = multiprocessing.get_context("spawn")
    with ctx.Manager() as manager:
        barrier = manager.Barrier(4)
        with ctx.Pool(4) as pool:
            assert pool.starmap(_upgrade_after, [(url, barrier)] * 4) == ["ok"] * 4
    engine = make_engine(url)
    assert migrations.pending_model_changes(engine) == []
    engine.dispose()
