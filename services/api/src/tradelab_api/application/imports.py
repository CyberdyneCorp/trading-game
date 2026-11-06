"""Use case: validate a private CSV import (spec game-api `POST /scenario-imports/validate`).

The engine's report is returned unchanged, so the reasons are exactly the ones every client's
import wizard shows. Nothing is stored or logged: the file stays private to its owner.
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from ..domain.errors import TooLarge
from ..ports import CsvValidator


class ImportService:
    def __init__(self, validator: CsvValidator, max_csv_bytes: int) -> None:
        self._validator = validator
        self._max = max_csv_bytes

    def validate(self, csv: str, meta: Mapping[str, Any]) -> dict[str, Any]:
        if len(csv.encode("utf-8")) > self._max:
            raise TooLarge(f"CSV larger than {self._max} bytes")
        return self._validator.validate(csv, meta)
