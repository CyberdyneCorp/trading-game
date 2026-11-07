"""Server progress derived from server-replayed runs only (spec game-api "Progress").

Nothing here trusts a client: mission stats, lesson results and campaign legs all come from the
engine debrief the server stored when it replayed the run. Achievements are a union over every run
of the player, so playing on several devices and syncing in any order yields the same set.

- `lesson:<mission>`: a finished run whose engine-scored mission lesson (the `lesson` actions
  recorded in its log) has every point.
- `campaign:<mission>`: the mission finished in a campaign run, not bankrupt and leaving at least
  CAMPAIGN_MIN_CAPITAL (the web client's `assessed`, which passes curriculum stages).
- `campaign-complete`: one campaign run with such a leg for every campaign mission.

The campaign order and minimum mirror apps/web/src/lib/game/curriculum.ts and campaign.ts
(tests/test_progress.py fails when they drift). The capital carried between legs is the client's
`startingBalance` start option, which the replay uses but does not chain-check against the previous
leg's ending equity.
"""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from decimal import Decimal, InvalidOperation

from .models import Achievement, MissionProgress, Run

CAMPAIGN_MISSIONS: tuple[str, ...] = (
    "m06-thin-book", "m01-false-breakout", "m03-volatility-shock", "m02-funding-squeeze", "m07-trend-trailing",
    "m04-options-buyer", "m09-event-straddle", "m05-options-writer", "m10-hedged-book", "m13-call-spread",
    "m14-covered-call", "m15-protective-put", "m16-range-condor", "m17-before-expiry", "m08-regime-switch",
    "m11-news-calendar", "m12-capstone",
)
CAMPAIGN_MIN_CAPITAL = Decimal(100)


def _chronological(runs: Iterable[Run]) -> list[Run]:
    return sorted(runs, key=lambda r: (r.created_at, r.run_id))


def _lesson(run: Run) -> tuple[int, int] | None:
    lessons = (run.report or {}).get("lessons")
    if not isinstance(lessons, dict):
        return None
    points, total = lessons.get("points"), lessons.get("total")
    if not isinstance(points, int) or not isinstance(total, int) or total <= 0:
        return None
    return points, total


def _better_lesson(p: MissionProgress, lesson: tuple[int, int]) -> None:
    if p.lesson_points is None or lesson[0] * (p.lesson_total or 1) > p.lesson_points * lesson[1]:
        p.lesson_points, p.lesson_total = lesson


def mission_progress(runs: Iterable[Run]) -> list[MissionProgress]:
    by_mission: dict[str, MissionProgress] = {}
    for run in runs:
        p = by_mission.setdefault(run.mission_id, MissionProgress(run.mission_id))
        p.runs += 1
        if run.status == "active":
            continue
        p.completed += 1
        if run.score_total is not None and (p.best_score is None or run.score_total > p.best_score):
            p.best_score = run.score_total
            p.best_net_pnl = (run.report or {}).get("netPnl")
        lesson = _lesson(run)
        if lesson is not None:
            _better_lesson(p, lesson)
    return sorted(by_mission.values(), key=lambda p: p.mission_id)


def _final_equity(run: Run) -> Decimal | None:
    try:
        return Decimal(str((run.report or {})["finalEquity"]))
    except (KeyError, InvalidOperation):
        return None


def campaign_leg(run: Run) -> tuple[str, int] | None:
    """(campaign id, mission index) when the run is a campaign mission that was passed."""
    link = run.campaign or {}
    cid, index = link.get("id"), link.get("index")
    if not isinstance(cid, str) or not isinstance(index, int) or not 0 <= index < len(CAMPAIGN_MISSIONS):
        return None
    if CAMPAIGN_MISSIONS[index] != run.mission_id or run.status != "finished":
        return None
    equity = _final_equity(run)
    return (cid, index) if equity is not None and equity >= CAMPAIGN_MIN_CAPITAL else None


def _earn(out: dict[str, Achievement], achievement: Achievement) -> None:
    out.setdefault(achievement.id, achievement)  # runs arrive oldest first: the first one keeps it


def _completed_campaign(runs: Sequence[Run]) -> Achievement | None:
    legs: dict[str, dict[int, Run]] = {}
    for run in runs:
        leg = campaign_leg(run)
        if leg is not None:
            legs.setdefault(leg[0], {}).setdefault(leg[1], run)
    complete = [by_index for by_index in legs.values() if len(by_index) == len(CAMPAIGN_MISSIONS)]
    if not complete:
        return None
    last = min((max(by_index.values(), key=lambda r: (r.created_at, r.run_id)) for by_index in complete),
               key=lambda r: (r.created_at, r.run_id))
    return Achievement("campaign-complete", "campaign_complete", last.run_id, last.created_at,
                       campaign_id=str((last.campaign or {}).get("id")))


def achievements(runs: Iterable[Run]) -> list[Achievement]:
    ordered = _chronological(runs)
    out: dict[str, Achievement] = {}
    for run in ordered:
        lesson = _lesson(run)
        if lesson is not None and run.status != "active" and lesson[0] == lesson[1]:
            _earn(out, Achievement(f"lesson:{run.mission_id}", "lesson", run.run_id, run.created_at, run.mission_id))
        leg = campaign_leg(run)
        if leg is not None:
            _earn(out, Achievement(f"campaign:{run.mission_id}", "campaign_mission", run.run_id, run.created_at,
                                   run.mission_id, leg[0]))
    done = _completed_campaign(ordered)
    if done is not None:
        out[done.id] = done
    return sorted(out.values(), key=lambda a: a.id)
