"""Prints the API's OpenAPI document in its canonical form (the checked-in `services/api/openapi.json`).

    uv run python -m tradelab_api.openapi_snapshot > openapi.json

`tests/test_openapi_contract.py` fails while the snapshot and the code disagree, so every contract
change shows up in review as a diff of that file.
"""

from __future__ import annotations

import json
from typing import Any

from .config import Settings
from .http.app import create_app


class _NoTokens:
    def verify(self, token: str) -> dict[str, Any]:
        raise NotImplementedError


class _NoEngine:
    def supported_policy(self) -> int:
        return 0

    def replay(self, *args: Any, **kwargs: Any) -> dict[str, Any]:
        raise NotImplementedError


def document() -> dict[str, Any]:
    settings = Settings(database_url="sqlite+pysqlite:///:memory:", migrate_on_start=False)
    return create_app(settings, _NoTokens(), _NoEngine(), frozenset()).openapi()


def canonical(doc: dict[str, Any]) -> str:
    return json.dumps(doc, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


if __name__ == "__main__":
    print(canonical(document()), end="")
