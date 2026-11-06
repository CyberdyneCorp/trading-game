"""Webhook receipts (CyberdyneAuth event dedupe) and the erasure audit log.

Revision ID: 0002
Revises: 0001
Create Date: 2026-11-05
"""

from __future__ import annotations

import sqlalchemy as sa
from alembic import op

revision = "0002"
down_revision = "0001"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "erasure_log",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("subject_sha256", sa.String(length=64), nullable=False),
        sa.Column("source", sa.String(length=16), nullable=False),
        sa.Column("reference", sa.String(length=128), nullable=True),
        sa.Column("deleted_runs", sa.Integer(), nullable=False),
        sa.Column("erased_at", sa.DateTime(timezone=True), nullable=False),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("ix_erasure_log_subject_sha256", "erasure_log", ["subject_sha256"], unique=False)
    op.create_table(
        "webhook_receipts",
        sa.Column("webhook_id", sa.String(length=128), nullable=False),
        sa.Column("event_type", sa.String(length=64), nullable=False),
        sa.Column("received_at", sa.DateTime(timezone=True), nullable=False),
        sa.PrimaryKeyConstraint("webhook_id"),
    )


def downgrade() -> None:
    op.drop_table("webhook_receipts")
    op.drop_index("ix_erasure_log_subject_sha256", table_name="erasure_log")
    op.drop_table("erasure_log")
