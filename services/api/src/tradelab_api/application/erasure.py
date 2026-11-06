"""Use cases: erase a player's data (in-game request or CyberdyneAuth `user.deleted`) and log it.

Every erasure is written to the audit log with the SHA-256 of the `sub` (never the `sub` itself),
its source and, for webhooks, the event id. Webhook events are processed once per `webhook-id`
(CyberdyneAuth delivers at least once); erasing an already-erased player is a no-op that is still
logged with 0 runs, so every verified request leaves a trace.
"""

from __future__ import annotations

import hashlib
import logging
from collections.abc import Mapping
from typing import Any

from ..domain.models import ErasureEntry
from ..ports import Clock, ErasureLog, PreferenceStore, RunRepository, WebhookReceipts

USER_DELETED = "user.deleted"

log = logging.getLogger("tradelab.erasure")


def subject_digest(sub: str) -> str:
    return hashlib.sha256(sub.encode()).hexdigest()


class ErasureService:
    def __init__(self, runs: RunRepository, audit: ErasureLog, clock: Clock,
                 prefs: PreferenceStore | None = None) -> None:
        self._runs = runs
        self._prefs = prefs
        self._audit = audit
        self._clock = clock

    def erase(self, sub: str, source: str, reference: str | None = None) -> int:
        if self._prefs is not None:
            self._prefs.delete_owner(sub)
        deleted = self._runs.delete_owner(sub)
        entry = ErasureEntry(subject_digest(sub), source, reference, deleted, self._clock.now())
        self._audit.record(entry)
        log.info("erased player %s: %d runs (source=%s, ref=%s)", entry.subject_sha256[:12], deleted, source,
                 reference)
        return deleted

    def handle_event(self, receipts: WebhookReceipts, webhook_id: str, event_type: str,
                     data: Mapping[str, Any]) -> bool:
        """Applies a verified CyberdyneAuth event once; False when it was already processed.
        Unknown event types are acknowledged and ignored (they are additive upstream)."""
        if not receipts.claim(webhook_id, event_type, self._clock.now()):
            return False
        user_id = data.get("user_id")
        if event_type == USER_DELETED and isinstance(user_id, str) and user_id:
            self.erase(user_id, "webhook", webhook_id)
        return True
