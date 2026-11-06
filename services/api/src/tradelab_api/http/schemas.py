from __future__ import annotations

from typing import Any

from pydantic import BaseModel, ConfigDict, Field


class ActionIn(BaseModel):
    seq: int = Field(ge=0)
    action: dict[str, Any]


class RunOptionsIn(BaseModel):
    """A run record's start options (engine policy v11/v12). Ranges are what a client can start:
    at most 10 advice points, a fast wire of 0-2 bars; the engine refuses anything else it cannot run."""

    model_config = ConfigDict(extra="forbid")

    adviceAllowance: int = Field(default=0, ge=0, le=10)
    newsLead: int = Field(default=0, ge=0, le=2)
    calendar: bool = False
    startingBalance: str | None = Field(default=None, max_length=40, pattern=r"^[0-9]+(\.[0-9]+)?$")
    leverageCap: int = Field(default=0, ge=0, le=1000)


class RunIn(BaseModel):
    runId: str = Field(min_length=8, max_length=80)
    missionId: str = Field(min_length=1, max_length=64)
    seed: int = Field(ge=0, le=2**32 - 1)
    executionPolicy: int = Field(ge=1)
    actions: list[ActionIn] = Field(default_factory=list, max_length=20_000)
    clientDigest: str | None = Field(default=None, max_length=64)
    clientActionCount: int | None = Field(default=None, ge=0)
    options: RunOptionsIn | None = None


class SyncBatchIn(BaseModel):
    deviceId: str = Field(min_length=1, max_length=80)
    runs: list[RunIn] = Field(max_length=500)  # the use case enforces the real limit (413)


class SyncOutcomeOut(BaseModel):
    runId: str
    acceptedThrough: int
    status: str
    serverDigest: str
    digestMatch: bool | None
    scoreTotal: int | None
    error: str | None


# ---- responses (the OpenAPI contract; tests/openapi.json is its checked-in snapshot) -----------

class HealthOut(BaseModel):
    status: str
    executionPolicy: int


class _RunFields(BaseModel):
    runId: str
    missionId: str
    seed: int
    executionPolicy: int
    deviceId: str
    createdAt: str
    status: str
    serverDigest: str
    digestMismatch: bool
    scoreTotal: int | None
    options: dict[str, Any] | None
    withIntel: bool


class RunOut(_RunFields):
    actions: int  # committed actions


class RunDetailOut(RunOut):
    report: dict[str, Any] | None


class ExportRunOut(_RunFields):
    actions: list[dict[str, Any]]  # the full action log
    report: dict[str, Any] | None


class ExportOut(BaseModel):
    player: str
    runs: list[ExportRunOut]


class ProgressOut(BaseModel):
    missionId: str
    runs: int
    completed: int
    bestScore: int | None
    bestNetPnl: str | None


class ErasedOut(BaseModel):
    deletedRuns: int


class CsvValidateIn(BaseModel):
    """A CSV file's text and the import metadata the client's wizard collected (asset, pair, venue,
    volumeUnit, intervalSeconds, timestampUnit, ...): the inputs of the engine's `tl_csv_validate`."""

    model_config = ConfigDict(extra="forbid")

    csv: str
    meta: dict[str, Any]


class CsvIssueOut(BaseModel):
    line: int
    code: str


class CsvReportOut(BaseModel):
    """The engine's CSV report, unchanged: fields the engine did not send stay absent."""

    model_config = ConfigDict(extra="allow")

    ok: bool
    metaError: str | None = None
    rows: int | None = None
    errors: list[CsvIssueOut] | None = None
    errorCount: int | None = None
    gaps: list[dict[str, Any]] | None = None
    gapCount: int | None = None
    hasFunding: bool | None = None
    firstTime: int | None = None
    lastTime: int | None = None
