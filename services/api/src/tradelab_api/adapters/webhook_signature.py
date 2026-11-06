"""CyberdyneAuth webhook verification: the Standard Webhooks scheme (CyberdyneAuth docs/webhooks.md).

signature = base64(HMAC-SHA256(base64decode(secret without "whsec_"), f"{id}.{timestamp}.{raw body}")),
sent as space-separated `v1,<base64>` entries in `webhook-signature` (two during a secret rotation).
A timestamp more than `tolerance` seconds from our clock is refused, so a captured delivery cannot
be replayed later; replays inside the window are caught by the receipt of its `webhook-id`.
"""

from __future__ import annotations

import base64
import binascii
import hashlib
import hmac
import json
import time
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from typing import Any


class InvalidWebhook(Exception):
    """Unsigned, wrongly signed or stale delivery (401)."""


class MalformedWebhook(InvalidWebhook):
    """Correctly signed delivery whose body is not an event (400)."""


@dataclass(frozen=True)
class WebhookEvent:
    webhook_id: str
    type: str
    data: Mapping[str, Any]


def sign(secret: str, webhook_id: str, timestamp: int, body: bytes) -> str:
    """The `v1,<base64>` signature of a delivery (also used by tests to forge valid ones)."""
    key = _key(secret)
    mac = hmac.new(key, f"{webhook_id}.{timestamp}.".encode() + body, hashlib.sha256).digest()
    return "v1," + base64.b64encode(mac).decode()


def _key(secret: str) -> bytes:
    try:
        return base64.b64decode(secret.removeprefix("whsec_"), validate=True)
    except (binascii.Error, ValueError) as exc:
        raise InvalidWebhook("malformed signing secret") from exc


class WebhookVerifier:
    def __init__(self, secret: str, tolerance: int = 300, clock: Callable[[], float] = time.time) -> None:
        self._secret = secret
        self._tolerance = tolerance
        self._clock = clock

    def verify(self, body: bytes, headers: Mapping[str, str]) -> WebhookEvent:
        webhook_id = headers.get("webhook-id", "")
        timestamp = self._timestamp(headers.get("webhook-timestamp", ""))
        if not webhook_id or len(webhook_id) > 128:
            raise InvalidWebhook("missing webhook-id")
        expected = sign(self._secret, webhook_id, timestamp, body).encode()
        offered = headers.get("webhook-signature", "").split(" ")
        if not any(hmac.compare_digest(expected, s.encode()) for s in offered if s.startswith("v1,")):
            raise InvalidWebhook("bad signature")
        return _event(webhook_id, body)

    def _timestamp(self, raw: str) -> int:
        if not raw.isdigit():
            raise InvalidWebhook("missing webhook-timestamp")
        ts = int(raw)
        if abs(self._clock() - ts) > self._tolerance:
            raise InvalidWebhook("timestamp outside the tolerance")
        return ts


def _event(webhook_id: str, body: bytes) -> WebhookEvent:
    try:
        payload = json.loads(body)
    except ValueError as exc:
        raise MalformedWebhook("body is not JSON") from exc
    if not isinstance(payload, dict) or not isinstance(payload.get("type"), str):
        raise MalformedWebhook("body has no event type")
    data = payload.get("data")
    return WebhookEvent(webhook_id, payload["type"], data if isinstance(data, dict) else {})
