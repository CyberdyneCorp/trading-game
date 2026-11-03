"""Use cases: sync runs (idempotent, server-side replay), list runs, progress, export, erase.

The server never trusts client balances or scores: every sync replays the full action log through
the same deterministic engine and stores its own digest, status and score (spec game-api).
"""

from __future__ import annotations

import json
from collections.abc import Sequence
from typing import Any

from ..domain.errors import Conflict, DomainError, InvalidLog, NotFound, UnsupportedPolicy
from ..domain.models import Action, IncomingRun, MissionProgress, Run, SyncOutcome
from ..ports import Clock, Replayer, RunRepository

MAX_RUNS_PER_BATCH = 50
MAX_ACTIONS_PER_RUN = 20_000


def canonical(action: Action) -> str:
    return json.dumps(action, sort_keys=True, separators=(",", ":"))


class GameService:
    def __init__(self, runs: RunRepository, replayer: Replayer, clock: Clock, action_kinds: frozenset[str]) -> None:
        self._runs = runs
        self._replayer = replayer
        self._clock = clock
        self._kinds = action_kinds

    # ---- sync -----------------------------------------------------------------------------

    def sync(self, owner: str, device_id: str, batch: Sequence[IncomingRun]) -> list[SyncOutcome]:
        if len(batch) > MAX_RUNS_PER_BATCH:
            raise InvalidLog(f"at most {MAX_RUNS_PER_BATCH} runs per batch")
        return [self._sync_one(owner, device_id, inc) for inc in batch]

    def _sync_one(self, owner: str, device_id: str, inc: IncomingRun) -> SyncOutcome:
        try:
            return self._sync_run(owner, device_id, inc)
        except DomainError as exc:
            return SyncOutcome(inc.run_id, 0, "", "", None, None, error=exc.code)

    def _sync_run(self, owner: str, device_id: str, inc: IncomingRun) -> SyncOutcome:
        if inc.execution_policy != self._replayer.supported_policy():
            raise UnsupportedPolicy()
        run = self._owned_or_new(owner, device_id, inc)
        stored = self._runs.actions(run.run_id)
        accepted = self._new_prefix(stored, inc.actions)
        full = [a for _, a in stored] + [a for _, a in accepted]
        if len(full) > MAX_ACTIONS_PER_RUN:
            raise InvalidLog("too many actions")
        report = self._replay(run, full)  # validate before storing anything
        if self._runs.get(run.run_id) is None:
            self._runs.add(run)
        self._runs.add_actions(run.run_id, accepted)
        self._apply_report(run, len(full), report)
        match = self._compare(run, inc)
        self._runs.save(run)
        return SyncOutcome(run.run_id, run.actions, run.status, run.server_digest, match, run.score_total)

    def _owned_or_new(self, owner: str, device_id: str, inc: IncomingRun) -> Run:
        run = self._runs.get(inc.run_id)
        if run is None:
            return Run(inc.run_id, owner, inc.mission_id, inc.seed, inc.execution_policy, device_id, self._clock.now())
        if run.owner != owner:
            raise NotFound()  # never reveal that someone else's run exists
        if (run.mission_id, run.seed) != (inc.mission_id, inc.seed):
            raise Conflict("run id reused for another mission or seed")
        return run

    def _new_prefix(self, stored: list[tuple[int, Action]], incoming: list[tuple[int, Action]]) -> list[tuple[int, Action]]:
        """Actions that extend the stored log without gaps; re-sent actions must be identical."""
        known = {seq: canonical(a) for seq, a in stored}
        accepted: list[tuple[int, Action]] = []
        for seq, action in sorted(incoming, key=lambda x: x[0]):
            if not isinstance(action, dict) or action.get("k") not in self._kinds:
                raise InvalidLog(f"bad action at seq {seq}")
            if seq in known:
                if known[seq] != canonical(action):
                    raise Conflict(f"action {seq} differs from the stored one")
                continue
            if seq != len(stored) + len(accepted):
                break  # a gap: keep only the contiguous prefix; the client re-sends the rest
            accepted.append((seq, action))
        return accepted

    def _replay(self, run: Run, actions: list[Action]) -> dict[str, Any]:
        try:
            return self._replayer.replay(run.mission_id, run.seed, actions)
        except ValueError as exc:
            raise InvalidLog(str(exc)) from exc

    @staticmethod
    def _apply_report(run: Run, count: int, report: dict[str, Any]) -> None:
        run.actions = count
        run.server_digest = report["digest"]
        run.status = report["status"]
        debrief = report.get("debrief")
        if debrief is not None:
            run.report = debrief
            run.score_total = debrief["score"]["total"]

    @staticmethod
    def _compare(run: Run, inc: IncomingRun) -> bool | None:
        """Digest check only when the client's digest covers exactly the server's full log."""
        if not inc.client_digest or inc.client_action_count != run.actions:
            return None
        run.client_digest = inc.client_digest
        run.digest_mismatch = inc.client_digest != run.server_digest
        return not run.digest_mismatch

    # ---- queries --------------------------------------------------------------------------

    def runs(self, owner: str) -> list[Run]:
        return sorted(self._runs.runs_of(owner), key=lambda r: r.created_at, reverse=True)

    def run(self, owner: str, run_id: str) -> Run:
        run = self._runs.get(run_id)
        if run is None or run.owner != owner:
            raise NotFound()
        return run

    def progress(self, owner: str) -> list[MissionProgress]:
        by_mission: dict[str, MissionProgress] = {}
        for run in self._runs.runs_of(owner):
            p = by_mission.setdefault(run.mission_id, MissionProgress(run.mission_id))
            p.runs += 1
            if run.status == "active":
                continue
            p.completed += 1
            if run.score_total is not None and (p.best_score is None or run.score_total > p.best_score):
                p.best_score = run.score_total
                p.best_net_pnl = (run.report or {}).get("netPnl")
        return sorted(by_mission.values(), key=lambda p: p.mission_id)

    def export(self, owner: str) -> dict[str, Any]:
        runs = []
        for run in self.runs(owner):
            runs.append({**_run_view(run), "actions": [a for _, a in self._runs.actions(run.run_id)], "report": run.report})
        return {"player": owner, "runs": runs}

    def erase(self, owner: str) -> int:
        return self._runs.delete_owner(owner)


def _run_view(run: Run) -> dict[str, Any]:
    return {
        "runId": run.run_id,
        "missionId": run.mission_id,
        "seed": run.seed,
        "executionPolicy": run.execution_policy,
        "deviceId": run.device_id,
        "createdAt": run.created_at.isoformat(),
        "actions": run.actions,
        "status": run.status,
        "serverDigest": run.server_digest,
        "digestMismatch": run.digest_mismatch,
        "scoreTotal": run.score_total,
    }


run_view = _run_view
