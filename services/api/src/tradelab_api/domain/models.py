"""Domain types (no framework imports). A run is owned by one player (`sub`) and identified by a
client-generated id; its action log is append-only and keyed by (run_id, seq)."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
from typing import Any

Action = dict[str, Any]


@dataclass
class Run:
    run_id: str
    owner: str
    mission_id: str
    seed: int
    execution_policy: int
    device_id: str
    created_at: datetime
    actions: int = 0  # committed actions (gap-free prefix)
    status: str = "active"  # server-computed: active | finished | bankrupt
    server_digest: str = ""
    client_digest: str = ""
    digest_mismatch: bool = False
    score_total: int | None = None
    report: dict[str, Any] | None = None  # engine debrief/score report once finished
    # Start options as the client recorded them (advice allowance, intel perks, campaign capital and
    # leverage cap); None = the mission's defaults. The replay starts the engine with them.
    options: dict[str, Any] | None = None
    # Campaign runs: {"id": campaign run id, "index": mission index in the curriculum order}; None otherwise.
    campaign: dict[str, Any] | None = None

    @property
    def with_intel(self) -> bool:
        """Played with an intel perk (fast wire or calendar): its score is labeled, never mixed silently."""
        o = self.options or {}
        return bool(o.get("newsLead")) or bool(o.get("calendar"))


@dataclass(frozen=True)
class IncomingRun:
    run_id: str
    mission_id: str
    seed: int
    execution_policy: int
    actions: list[tuple[int, Action]]  # (seq, action)
    client_digest: str | None = None
    client_action_count: int | None = None
    options: dict[str, Any] | None = None  # start options of the run record (None = defaults)
    campaign: dict[str, Any] | None = None  # campaign link of the run record (None = not a campaign run)


@dataclass(frozen=True)
class SyncOutcome:
    run_id: str
    accepted_through: int  # number of committed actions after this batch
    status: str
    server_digest: str
    digest_match: bool | None  # None when the client did not send a digest for its full log
    score_total: int | None
    error: str | None = None


@dataclass
class MissionProgress:
    mission_id: str
    runs: int = 0
    completed: int = 0
    best_score: int | None = None
    best_net_pnl: str | None = None
    # Best engine-scored lesson result of a finished run (points of total); None = no lesson answered yet.
    lesson_points: int | None = None
    lesson_total: int | None = None


@dataclass(frozen=True)
class Achievement:
    """Earned by the earliest server-replayed run that qualifies; removed only by erasing the player's data."""

    id: str
    kind: str  # lesson | campaign_mission | campaign_complete
    run_id: str
    earned_at: datetime
    mission_id: str | None = None
    campaign_id: str | None = None


@dataclass(frozen=True)
class Preference:
    """One preference key; the newest `updated_at` (ms since the epoch) wins (last writer wins per key)."""

    key: str
    value: Any
    updated_at: int


@dataclass
class Player:
    sub: str
    runs: list[Run] = field(default_factory=list)


@dataclass(frozen=True)
class ErasureEntry:
    """One erasure in the audit log: whose (SHA-256 of the `sub`), why, how much, when."""

    subject_sha256: str
    source: str  # "player" (DELETE /me/data) | "webhook" (CyberdyneAuth user.deleted)
    reference: str | None  # the webhook id for upstream deletions
    deleted_runs: int
    erased_at: datetime
