"""Player preferences (last writer wins per key) and the run record's campaign link.

Revision ID: 0003
Revises: 0002
Create Date: 2026-11-05
"""

from __future__ import annotations

import sqlalchemy as sa
from alembic import op

revision = "0003"
down_revision = "0002"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column("runs", sa.Column("campaign", sa.JSON(), nullable=True))
    op.create_table(
        "preferences",
        sa.Column("owner", sa.String(length=128), nullable=False),
        sa.Column("key", sa.String(length=64), nullable=False),
        sa.Column("value", sa.JSON(), nullable=False),
        sa.Column("updated_at", sa.BigInteger(), nullable=False),
        sa.PrimaryKeyConstraint("owner", "key"),
    )


def downgrade() -> None:
    op.drop_table("preferences")
    with op.batch_alter_table("runs") as batch:
        batch.drop_column("campaign")
