"""Unit tests of the use cases with fake ports: no database, no HTTP, no engine."""

from __future__ import annotations

import hashlib
from collections.abc import Mapping, Sequence
from datetime import UTC, datetime
from typing import Any

import pytest

from tradelab_api.application.erasure import ErasureService
from tradelab_api.application.game import MAX_ACTIONS_PER_RUN, MAX_RUNS_PER_BATCH, GameService
from tradelab_api.application.imports import ImportService
from tradelab_api.domain.errors import InvalidLog, NotFound, TooLarge
from tradelab_api.domain.models import Action, ErasureEntry, IncomingRun, Preference, Run

POLICY = 7
KINDS = frozenset({"ans", "order", "adv"})
NOW = datetime(2026, 11, 5, tzinfo=UTC)


class FakeRuns:
    def __init__(self) -> None:
        self.runs: dict[str, Run] = {}
        self.logs: dict[str, list[tuple[int, Action]]] = {}

    def get(self, run_id: str) -> Run | None:
        run = self.runs.get(run_id)
        return None if run is None else Run(**vars(run))  # a copy, like a fresh row

    def add(self, run: Run) -> None:
        self.runs[run.run_id] = Run(**vars(run))

    def save(self, run: Run) -> None:
        self.runs[run.run_id] = Run(**vars(run))

    def actions(self, run_id: str) -> list[tuple[int, Action]]:
        return list(self.logs.get(run_id, []))

    def add_actions(self, run_id: str, actions: Sequence[tuple[int, Action]]) -> None:
        self.logs.setdefault(run_id, []).extend(actions)

    def runs_of(self, owner: str) -> list[Run]:
        return [r for r in self.runs.values() if r.owner == owner]

    def delete_owner(self, owner: str) -> int:
        ids = [k for k, r in self.runs.items() if r.owner == owner]
        for k in ids:
            del self.runs[k]
            self.logs.pop(k, None)
        return len(ids)


class FakeReplayer:
    """Score = number of "adv" actions * 10; finished once an "adv" was played; "bad" kinds raise."""

    def __init__(self) -> None:
        self.calls: list[tuple[str, int, int, Mapping[str, Any] | None]] = []

    def supported_policy(self) -> int:
        return POLICY

    def supported_policies(self) -> frozenset[int]:
        return frozenset({POLICY})

    def replay(self, mission_id: str, seed: int, actions: Sequence[Action],
               options: Mapping[str, Any] | None = None, policy: int | None = None) -> dict[str, Any]:
        self.calls.append((mission_id, seed, len(actions), options))
        if any(a.get("bad") for a in actions):
            raise ValueError("engine refused the log")
        advs = sum(1 for a in actions if a["k"] == "adv")
        digest = hashlib.sha256(f"{mission_id}/{seed}/{len(actions)}".encode()).hexdigest()
        if not advs:
            return {"digest": digest, "status": "active", "debrief": None}
        return {"digest": digest, "status": "finished", "debrief": {"score": {"total": advs * 10}, "netPnl": "12.50"}}


class FakePrefs:
    def __init__(self) -> None:
        self.rows: dict[str, dict[str, Preference]] = {}

    def all(self, owner: str) -> dict[str, Preference]:
        return dict(self.rows.get(owner, {}))

    def put(self, owner: str, prefs: Sequence[Preference]) -> None:
        self.rows.setdefault(owner, {}).update({p.key: p for p in prefs})

    def delete_owner(self, owner: str) -> None:
        self.rows.pop(owner, None)


class FakeClock:
    def now(self) -> datetime:
        return NOW


class FakeAudit:
    def __init__(self) -> None:
        self.entries: list[ErasureEntry] = []

    def record(self, entry: ErasureEntry) -> None:
        self.entries.append(entry)


class FakeReceipts:
    def __init__(self) -> None:
        self.seen: set[str] = set()

    def claim(self, webhook_id: str, event_type: str, at: Any) -> bool:
        if webhook_id in self.seen:
            return False
        self.seen.add(webhook_id)
        return True


LOG: list[Action] = [{"k": "ans"}, {"k": "order"}, {"k": "adv"}]


def incoming(run_id: str = "r1", actions: list[Action] | None = None, start: int = 0, **kw: Any) -> IncomingRun:
    acts = LOG if actions is None else actions
    fields: dict[str, Any] = {"mission_id": "m01", "seed": 5, "execution_policy": POLICY,
                              "actions": [(start + i, a) for i, a in enumerate(acts)], **kw}
    return IncomingRun(run_id, **fields)


@pytest.fixture
def ports() -> tuple[FakeRuns, FakeReplayer]:
    return FakeRuns(), FakeReplayer()


@pytest.fixture
def svc(ports) -> GameService:
    runs, replayer = ports
    return GameService(runs, replayer, FakeClock(), KINDS)


def test_new_run_is_replayed_and_scored_by_the_server(svc, ports):
    runs, replayer = ports
    [out] = svc.sync("alice", "dev", [incoming()])
    assert (out.error, out.accepted_through, out.status, out.score_total) == (None, 3, "finished", 10)
    stored = runs.runs["r1"]
    assert (stored.owner, stored.device_id, stored.created_at) == ("alice", "dev", NOW)
    assert replayer.calls == [("m01", 5, 3, None)]


def test_resend_is_a_no_op_and_extension_replays_the_full_log(svc, ports):
    runs, replayer = ports
    svc.sync("alice", "dev", [incoming(actions=LOG[:2])])
    [out] = svc.sync("alice", "dev", [incoming(actions=LOG[:2])])
    assert out.accepted_through == 2
    [out] = svc.sync("alice", "dev", [incoming()])
    assert out.accepted_through == 3
    assert [seq for seq, _ in runs.logs["r1"]] == [0, 1, 2]
    assert replayer.calls[-1][2] == 3


def test_rewritten_action_is_a_conflict(svc):
    svc.sync("alice", "dev", [incoming(actions=LOG[:2])])
    [out] = svc.sync("alice", "dev", [incoming(actions=[LOG[0], {"k": "adv"}])])
    assert out.error == "conflict"


def test_gap_keeps_only_the_contiguous_prefix(svc, ports):
    runs, _ = ports
    svc.sync("alice", "dev", [incoming(actions=LOG[:1])])
    [out] = svc.sync("alice", "dev", [incoming(actions=LOG[2:], start=2)])
    assert out.accepted_through == 1
    assert len(runs.logs["r1"]) == 1


def test_mission_seed_or_options_reuse_conflicts(svc):
    svc.sync("alice", "dev", [incoming()])
    assert svc.sync("alice", "dev", [incoming(seed=6)])[0].error == "conflict"
    assert svc.sync("alice", "dev", [incoming(options={"calendar": True})])[0].error == "conflict"


def test_foreign_run_is_not_found_and_unchanged(svc, ports):
    runs, _ = ports
    svc.sync("alice", "dev", [incoming(actions=LOG[:1])])
    [out] = svc.sync("bob", "dev", [incoming()])
    assert out.error == "not_found"
    assert runs.runs["r1"].actions == 1
    with pytest.raises(NotFound):
        svc.run("bob", "r1")


def test_invalid_log_stores_nothing_and_other_runs_proceed(svc, ports):
    runs, _ = ports
    outs = svc.sync("alice", "dev", [incoming("bad1", actions=[{"k": "ans", "bad": True}]),
                                     incoming("bad2", actions=[{"k": "explode"}]), incoming("ok")])
    assert [o.error for o in outs] == ["invalid_log", "invalid_log", None]
    assert set(runs.runs) == {"ok"}
    assert "bad1" not in runs.logs


def test_unsupported_policy_and_limits(svc):
    assert svc.sync("alice", "dev", [incoming(execution_policy=POLICY - 1)])[0].error == "unsupported_policy"
    with pytest.raises(InvalidLog):
        svc.sync("alice", "dev", [incoming(f"r{i}") for i in range(MAX_RUNS_PER_BATCH + 1)])
    big = [{"k": "ans"}] * (MAX_ACTIONS_PER_RUN + 1)
    assert svc.sync("alice", "dev", [incoming(actions=big)])[0].error == "invalid_log"


def test_digest_is_compared_only_for_the_full_log(svc, ports):
    runs, _ = ports
    [out] = svc.sync("alice", "dev", [incoming(client_digest="f" * 64, client_action_count=2)])
    assert out.digest_match is None
    server = runs.runs["r1"].server_digest
    [out] = svc.sync("alice", "dev", [incoming(client_digest=server, client_action_count=3)])
    assert out.digest_match is True
    [out] = svc.sync("alice", "dev", [incoming(client_digest="0" * 64, client_action_count=3)])
    assert out.digest_match is False
    assert runs.runs["r1"].digest_mismatch is True
    assert runs.runs["r1"].server_digest == server  # the server result is kept


def test_options_reach_the_replay(svc, ports):
    _, replayer = ports
    svc.sync("alice", "dev", [incoming(options={"adviceAllowance": 3})])
    assert replayer.calls[-1][3] == {"adviceAllowance": 3}
    assert svc.runs("alice")[0].with_intel is False


def test_progress_and_export(svc):
    svc.sync("alice", "dev", [incoming("r1"), incoming("r2", actions=LOG[:1]),
                              incoming("r3", actions=[*LOG, {"k": "adv"}])])
    [p], _ = svc.progress("alice")
    assert (p.mission_id, p.runs, p.completed, p.best_score, p.best_net_pnl) == ("m01", 3, 2, 20, "12.50")
    export = svc.export("alice")
    assert export["player"] == "alice"
    assert sorted(len(r["actions"]) for r in export["runs"]) == [1, 3, 4]
    assert svc.progress("bob") == ([], [])


def test_erasure_deletes_and_audits(ports):
    runs, replayer = ports
    GameService(runs, replayer, FakeClock(), KINDS).sync("alice", "dev", [incoming()])
    audit = FakeAudit()
    erasure = ErasureService(runs, audit, FakeClock())
    assert erasure.erase("alice", "player") == 1
    assert runs.runs == {}
    [entry] = audit.entries
    assert entry == ErasureEntry(hashlib.sha256(b"alice").hexdigest(), "player", None, 1, NOW)


def test_regression_erasure_also_deletes_preferences(ports):
    # Before: ErasureService (in-game and webhook erasure) left the player's preferences behind.
    runs, replayer = ports
    prefs = FakePrefs()
    prefs.put("alice", [Preference("ui.lang", "pt-BR", 1)])
    prefs.put("bob", [Preference("ui.lang", "en", 1)])
    erasure = ErasureService(runs, FakeAudit(), FakeClock(), prefs)
    erasure.handle_event(FakeReceipts(), "msg_9", "user.deleted", {"user_id": "alice"})
    assert prefs.all("alice") == {}
    assert prefs.all("bob") != {}


def test_webhook_events_are_processed_once(ports):
    runs, replayer = ports
    GameService(runs, replayer, FakeClock(), KINDS).sync("alice", "dev", [incoming()])
    audit, receipts = FakeAudit(), FakeReceipts()
    erasure = ErasureService(runs, audit, FakeClock())
    assert erasure.handle_event(receipts, "msg_1", "user.deleted", {"user_id": "alice"}) is True
    assert erasure.handle_event(receipts, "msg_1", "user.deleted", {"user_id": "alice"}) is False
    assert erasure.handle_event(receipts, "msg_2", "role.granted", {"user_id": "alice"}) is True
    assert erasure.handle_event(receipts, "msg_3", "user.deleted", {}) is True  # nothing to erase
    assert [(e.source, e.reference) for e in audit.entries] == [("webhook", "msg_1")]


def test_import_validation_enforces_its_size_limit():
    class Validator:
        def validate(self, csv: str, meta: Mapping[str, Any]) -> dict[str, Any]:
            return {"ok": True, "rows": csv.count("\n")}

    imports = ImportService(Validator(), max_csv_bytes=10)
    assert imports.validate("a\nb\n", {}) == {"ok": True, "rows": 2}
    with pytest.raises(TooLarge):
        imports.validate("é" * 6, {})  # 12 bytes in UTF-8
