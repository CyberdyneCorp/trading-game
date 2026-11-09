"""Server progress: lesson and campaign achievements from replayed runs; preferences (last writer wins)."""

from __future__ import annotations

import re
import threading
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest
import tradelab_engine as te
from sqlalchemy.orm import Session
from test_api import run_body, sync

from tradelab_api.adapters.db import SqlPreferenceStore
from tradelab_api.application.game import GameService
from tradelab_api.domain.errors import LimitExceeded
from tradelab_api.domain.models import Preference, Run
from tradelab_api.domain.preferences import merge
from tradelab_api.domain.progress import (
    CAMPAIGN_MIN_CAPITAL,
    CAMPAIGN_MISSIONS,
    CAMPAIGN_MISSIONS_V3,
    achievements,
    mission_progress,
)

WEB_GAME = Path(__file__).resolve().parents[3] / "apps" / "web" / "src" / "lib" / "game"
FINISH = {"k": "adv", "n": 2000}  # advance past the last bar: the run finishes flat
RIGHT = [{"k": "lesson", "q": 0, "choice": 1, "value": None, "skipped": False},
         {"k": "lesson", "q": 1, "choice": 0, "value": "200", "skipped": False}]
WRONG = [{"k": "lesson", "q": 0, "choice": 0, "value": None, "skipped": False},
         {"k": "lesson", "q": 1, "choice": 0, "value": "200", "skipped": False}]


def test_campaign_order_and_minimum_match_the_web_client():
    curriculum = (WEB_GAME / "curriculum.ts").read_text()
    block = curriculum.split("export const STAGES")[1].split("\n];")[0]
    # One stage per `{ id: ...` entry; an elective stage (`campaign: false`) is never a campaign leg.
    entries = re.split(r"\n\t\{", block)
    order: list[str] = []
    for entry in entries:
        group = re.search(r"missions: \[([^\]]*)\]", entry)
        if group is None or "campaign: false" in entry:
            continue
        for mission in re.findall(r"'([^']+)'", group.group(1)):
            if mission not in order:
                order.append(mission)
    assert tuple(order) == CAMPAIGN_MISSIONS
    campaign_ts = (WEB_GAME / "campaign.ts").read_text()
    v3 = re.findall(r"'(m\d\d-[a-z-]+)'", campaign_ts.split("CAMPAIGN_ORDER_V3")[1].split("];")[0])
    assert tuple(v3) == CAMPAIGN_MISSIONS_V3
    campaign = (WEB_GAME / "campaign.ts").read_text()
    assert f"CAMPAIGN_MIN_CAPITAL = '{CAMPAIGN_MIN_CAPITAL}'" in campaign


def test_lesson_achievement_from_recorded_lesson_answers(client, auth):
    out = sync(client, auth, [run_body(run_id="run-lesson-wrong", actions=[*WRONG, FINISH]),
                              run_body(run_id="run-lesson-right", actions=[*RIGHT, FINISH], seed=6)]).json()
    assert [o["status"] for o in out] == ["finished", "finished"]
    progress = client.get("/api/v1/progress", headers=auth()).json()
    m01 = progress["missions"][0]
    assert (m01["lessonPoints"], m01["lessonTotal"]) == (2, 2)  # the best lesson of the two runs
    assert [(a["id"], a["kind"], a["runId"]) for a in progress["achievements"]] == [
        ("lesson:m01-false-breakout", "lesson", "run-lesson-right")]


def test_unanswered_or_active_runs_earn_nothing(client, auth):
    sync(client, auth, [run_body(run_id="run-active-0001", actions=RIGHT)])  # lesson answered, run not finished
    progress = client.get("/api/v1/progress", headers=auth()).json()
    assert progress["achievements"] == []
    assert progress["missions"][0]["lessonPoints"] is None


def test_campaign_mission_achievement_needs_the_mission_at_its_index(client, auth):
    leg = run_body(run_id="run-camp-leg-0", missionId="m06-thin-book", actions=[FINISH],
                   campaign={"id": "camp-1", "index": 0}, options={"startingBalance": "10000"})
    wrong_index = run_body(run_id="run-camp-leg-x", missionId="m01-false-breakout", actions=[FINISH],
                           campaign={"id": "camp-1", "index": 0})
    plain = run_body(run_id="run-plain-m03", missionId="m03-volatility-shock", actions=[FINISH])
    out = sync(client, auth, [leg, wrong_index, plain]).json()
    assert [o["error"] for o in out] == [None, None, None]
    earned = client.get("/api/v1/progress", headers=auth()).json()["achievements"]
    assert [(a["id"], a["campaignId"], a["runId"]) for a in earned] == [
        ("campaign:m06-thin-book", "camp-1", "run-camp-leg-0")]
    assert client.get("/api/v1/runs/run-camp-leg-0", headers=auth()).json()["campaign"] == {"id": "camp-1", "index": 0}


def test_regression_campaign_link_is_fixed_by_the_first_sync(client, auth):
    # Moving a synced run into another campaign (or out of one) would rewrite progress: conflict.
    first = run_body(run_id="run-camp-fixed", missionId="m06-thin-book", actions=[],
                     campaign={"id": "camp-1", "index": 0})
    sync(client, auth, [first], key="a")
    moved = {**first, "campaign": {"id": "camp-2", "index": 0}}
    assert sync(client, auth, [moved], key="b").json()[0]["error"] == "conflict"
    dropped = {k: v for k, v in first.items() if k != "campaign"}
    assert sync(client, auth, [dropped], key="c").json()[0]["error"] == "conflict"
    assert sync(client, auth, [run_body(run_id="run-camp-fixed2", campaign={"id": "c", "index": 99})],
                key="d").status_code == 422


# ---- pure progress rules -----------------------------------------------------------------------

T0 = datetime(2026, 11, 1, tzinfo=UTC)


def _run(run_id: str, mission: str, minutes: int, status: str = "finished", equity: str = "9000",
         campaign: dict[str, Any] | None = None, lessons: dict[str, Any] | None = None) -> Run:
    report: dict[str, Any] = {"finalEquity": equity, "score": {"total": 50}, "netPnl": "0"}
    if lessons is not None:
        report["lessons"] = lessons
    return Run(run_id, "p", mission, 1, 17, "d", T0 + timedelta(minutes=minutes), status=status,
               score_total=50, report=report, campaign=campaign)


def _legs(cid: str, start: int, skip: int | None = None) -> list[Run]:
    return [_run(f"{cid}-{i}", m, start + i, campaign={"id": cid, "index": i})
            for i, m in enumerate(CAMPAIGN_MISSIONS) if i != skip]


def test_campaign_complete_is_earned_by_the_run_that_completed_the_first_full_campaign():
    runs = _legs("late", 100) + _legs("early", 0) + _legs("partial", -50, skip=3)
    earned = {a.id: a for a in achievements(runs)}
    assert earned["campaign-complete"].campaign_id == "early"
    assert earned["campaign-complete"].run_id == f"early-{len(CAMPAIGN_MISSIONS) - 1}"
    assert earned["campaign:m06-thin-book"].campaign_id == "partial"  # earliest leg wins
    assert len([a for a in earned.values() if a.kind == "campaign_mission"]) == len(CAMPAIGN_MISSIONS)


def test_bankrupt_or_drained_legs_do_not_pass_and_union_ignores_order():
    below = str(CAMPAIGN_MIN_CAPITAL - 1)
    runs = [_run("a", "m06-thin-book", 0, status="bankrupt", campaign={"id": "c", "index": 0}),
            _run("b", "m06-thin-book", 1, equity=below, campaign={"id": "c", "index": 0}),
            _run("c", "m06-thin-book", 2, status="active", campaign={"id": "c", "index": 0}),
            _run("d", "m01-false-breakout", 3, lessons={"points": 1, "total": 2}),
            _run("e", "m01-false-breakout", 4, lessons={"points": 2, "total": 2})]
    assert [a.id for a in achievements(runs)] == ["lesson:m01-false-breakout"]
    assert achievements(runs) == achievements(list(reversed(runs)))
    m01 = next(p for p in mission_progress(runs) if p.mission_id == "m01-false-breakout")
    assert (m01.lesson_points, m01.lesson_total) == (2, 2)


# ---- preferences -------------------------------------------------------------------------------


def prefs(client, auth, values: dict[str, Any], key: str, sub: str = "player-1"):
    body = {"preferences": {k: {"value": v, "updatedAt": t} for k, (v, t) in values.items()}}
    return client.patch("/api/v1/preferences", json=body, headers=auth(sub, key))


def test_preferences_last_writer_wins_per_key(client, auth):
    assert client.get("/api/v1/preferences", headers=auth()).json() == {"preferences": {}}
    prefs(client, auth, {"locale": ("en", 2000), "sound": (True, 1000)}, "a")
    res = prefs(client, auth, {"locale": ("pt-BR", 1500), "sound": (False, 3000)}, "b")  # older locale loses
    assert res.status_code == 200
    expected = {"locale": {"value": "en", "updatedAt": 2000}, "sound": {"value": False, "updatedAt": 3000}}
    assert res.json() == {"preferences": expected}
    assert client.get("/api/v1/preferences", headers=auth()).json() == {"preferences": expected}
    assert client.get("/api/v1/preferences", headers=auth("someone-else")).json() == {"preferences": {}}
    assert client.get("/api/v1/me/export", headers=auth()).json()["preferences"] == expected


def test_preferences_need_idempotency_and_valid_keys(client, auth):
    first = prefs(client, auth, {"volume": (0.5, 10)}, "same")
    assert prefs(client, auth, {"volume": (0.5, 10)}, "same").json() == first.json()
    assert prefs(client, auth, {"volume": (0.9, 11)}, "same").status_code == 409
    body = {"preferences": {"volume": {"value": 1, "updatedAt": 1}}}
    assert client.patch("/api/v1/preferences", json=body, headers=auth()).status_code == 400
    assert prefs(client, auth, {"bad key": (1, 1)}, "k2").status_code == 422
    assert prefs(client, auth, {"big": ("x" * 5000, 1)}, "k3").status_code == 422
    assert client.patch("/api/v1/preferences", json=body).status_code == 401


def test_regression_future_timestamps_are_clamped_so_a_value_is_never_pinned(client, auth):
    far_future = 10**15  # year 33658: a device with a broken clock
    prefs(client, auth, {"theme": ("amber", far_future)}, "a")
    stored = client.get("/api/v1/preferences", headers=auth()).json()["preferences"]["theme"]
    assert stored["updatedAt"] < far_future
    later = stored["updatedAt"] + 60_000
    prefs(client, auth, {"theme": ("green", later)}, "b")
    assert client.get("/api/v1/preferences", headers=auth()).json()["preferences"]["theme"]["value"] == "green"


def test_preferences_are_erased_with_the_players_data(client, auth):
    prefs(client, auth, {"locale": ("en", 1)}, "a")
    client.delete("/api/v1/me/data", headers=auth(key="del"))
    assert client.get("/api/v1/preferences", headers=auth()).json() == {"preferences": {}}


def test_regression_overlapping_preference_writes_keep_the_newest(client):
    # Before: set_preferences read, merged and wrote without a lock, so a write that read before an
    # overlapping newer write committed after it and replaced the newer value (PostgreSQL).
    engine = client.app.state.engine
    if engine.dialect.name != "postgresql":
        pytest.skip("needs concurrent connections (CI runs this on PostgreSQL)")

    class Clock:
        def now(self) -> datetime:
            return datetime.now(UTC)

    def newer() -> None:
        with Session(engine) as s:
            service = GameService(None, None, Clock(), frozenset(), SqlPreferenceStore(s))  # type: ignore[arg-type]
            service.set_preferences("racer", [Preference("ui.lang", "new", 200)])
            s.commit()

    with Session(engine) as first:  # an older write that has already read the stored map
        store = SqlPreferenceStore(first)
        store.lock("racer")
        seen = store.all("racer")
        other = threading.Thread(target=newer)
        other.start()
        other.join(0.5)
        assert other.is_alive()  # the newer write waits for this transaction
        store.put("racer", merge(seen, [Preference("ui.lang", "old", 100)], 10**13))
        first.commit()
        other.join()
    with Session(engine) as s:
        assert SqlPreferenceStore(s).all("racer")["ui.lang"].value == "new"


def test_merge_is_order_independent_and_bounded():
    a, b = Preference("k", "a", 5), Preference("k", "b", 5)  # same time: the canonical value breaks the tie
    assert merge({}, [a, b], 100) == merge({}, [b, a], 100) == [b]
    assert merge({"k": b}, [a], 100) == []
    many = {f"k{i}": Preference(f"k{i}", i, 1) for i in range(100)}
    with pytest.raises(LimitExceeded):
        merge(many, [Preference("one-more", 1, 1)], 100)
    assert merge(many, [Preference("k1", 2, 2)], 100) == [Preference("k1", 2, 2)]  # updating a key is fine


def test_engine_still_reports_lessons_in_the_debrief():
    # The lesson achievement reads debrief["lessons"]; this pins the field the server depends on.
    debrief = te.replay("m01-false-breakout", 5, [*RIGHT, FINISH]).debrief
    assert debrief["lessons"]["points"] == debrief["lessons"]["total"] == 2


def test_a_campaign_started_before_progress_v4_completes_with_its_17_missions():
    old = [_run(f"old-{i}", m, i, campaign={"id": "old", "index": i}) for i, m in enumerate(CAMPAIGN_MISSIONS_V3)]
    earned = {a.id: a for a in achievements(old)}
    assert earned["campaign-complete"].campaign_id == "old"
    assert "campaign:m22-scale-manage" not in earned
    # A leg at index 5 is m04 in the old order and m22 in the new one: both are campaign legs.
    mixed = [_run("a5", "m04-options-buyer", 0, campaign={"id": "a", "index": 5}),
             _run("b5", "m22-scale-manage", 1, campaign={"id": "b", "index": 5})]
    assert {a.id for a in achievements(mixed)} == {"campaign:m04-options-buyer", "campaign:m22-scale-manage"}
    # 17 legs of the new order are not a complete campaign.
    partial = [_run(f"n-{i}", m, i, campaign={"id": "n", "index": i}) for i, m in enumerate(CAMPAIGN_MISSIONS[:17])]
    assert "campaign-complete" not in {a.id for a in achievements(partial)}
