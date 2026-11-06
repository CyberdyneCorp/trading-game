"""Baseline: runs, run_actions, idempotency_keys (the schema create_all built before migrations).

Revision ID: 0001
Revises:
Create Date: 2026-11-05
"""

from __future__ import annotations

import sqlalchemy as sa
from alembic import op

revision = "0001"
down_revision = None
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "idempotency_keys",
        sa.Column("owner", sa.String(length=128), nullable=False),
        sa.Column("key", sa.String(length=128), nullable=False),
        sa.Column("request_hash", sa.String(length=64), nullable=False),
        sa.Column("response", sa.Text(), nullable=False),
        sa.PrimaryKeyConstraint("owner", "key"),
    )
    op.create_table(
        "runs",
        sa.Column("run_id", sa.String(length=80), nullable=False),
        sa.Column("owner", sa.String(length=128), nullable=False),
        sa.Column("mission_id", sa.String(length=64), nullable=False),
        sa.Column("seed", sa.Integer(), nullable=False),
        sa.Column("execution_policy", sa.Integer(), nullable=False),
        sa.Column("device_id", sa.String(length=80), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("actions", sa.Integer(), nullable=False),
        sa.Column("status", sa.String(length=16), nullable=False),
        sa.Column("server_digest", sa.String(length=64), nullable=False),
        sa.Column("client_digest", sa.String(length=64), nullable=False),
        sa.Column("digest_mismatch", sa.Boolean(), nullable=False),
        sa.Column("score_total", sa.Integer(), nullable=True),
        sa.Column("report", sa.JSON(), nullable=True),
        sa.Column("options", sa.JSON(), nullable=True),
        sa.PrimaryKeyConstraint("run_id"),
    )
    op.create_index("ix_runs_owner", "runs", ["owner"], unique=False)
    op.create_table(
        "run_actions",
        sa.Column("run_id", sa.String(length=80), nullable=False),
        sa.Column("seq", sa.Integer(), nullable=False),
        sa.Column("action", sa.JSON(), nullable=False),
        sa.ForeignKeyConstraint(["run_id"], ["runs.run_id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("run_id", "seq"),
    )


def downgrade() -> None:
    op.drop_table("run_actions")
    op.drop_index("ix_runs_owner", table_name="runs")
    op.drop_table("runs")
    op.drop_table("idempotency_keys")
