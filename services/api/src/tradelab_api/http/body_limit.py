"""Request body size limit (spec game-api "Limits").

Checks the declared Content-Length up front and also counts the bytes actually received, so a
chunked body or a body longer than its header cannot bypass the limit. The body is buffered
(at most max_bytes) and replayed to the app. A malformed
Content-Length is a 400.
"""

from __future__ import annotations

import json
from typing import Any

Scope = dict[str, Any]
Message = dict[str, Any]


async def _reply(send: Any, status: int, detail: str) -> None:
    body = json.dumps({"detail": detail}).encode()
    await send({"type": "http.response.start", "status": status,
                "headers": [(b"content-type", b"application/json"), (b"content-length", str(len(body)).encode())]})
    await send({"type": "http.response.body", "body": body})


def _declared_length(scope: Scope) -> int | None:
    for name, value in scope.get("headers", []):
        if name == b"content-length":
            text = value.decode("latin-1").strip()
            if not text.isdigit():
                raise ValueError(text)
            return int(text)
    return None


class BodyLimit:
    def __init__(self, app: Any, max_bytes: int) -> None:
        self.app = app
        self.max_bytes = max_bytes

    async def __call__(self, scope: Scope, receive: Any, send: Any) -> None:
        if scope["type"] != "http":
            await self.app(scope, receive, send)
            return
        try:
            declared = _declared_length(scope)
        except ValueError:
            await _reply(send, 400, "invalid content-length")
            return
        if declared is not None and declared > self.max_bytes:
            await _reply(send, 413, "request too large")
            return
        await self._counted(scope, receive, send)

    async def _counted(self, scope: Scope, receive: Any, send: Any) -> None:
        # Buffer the body (bounded by max_bytes) before the app sees it: raising mid-read would
        # surface as FastAPI's generic body-parsing 400 instead of a 413.
        chunks: list[bytes] = []
        seen = 0
        more = True
        while more:
            message = await receive()
            if message["type"] != "http.request":
                return  # client disconnected
            chunk = message.get("body", b"")
            seen += len(chunk)
            if seen > self.max_bytes:
                await _reply(send, 413, "request too large")
                return
            chunks.append(chunk)
            more = message.get("more_body", False)
        body = b"".join(chunks)
        delivered = False

        async def replay() -> Message:
            nonlocal delivered
            if delivered:
                return await receive()
            delivered = True
            return {"type": "http.request", "body": body, "more_body": False}

        await self.app(scope, replay, send)
