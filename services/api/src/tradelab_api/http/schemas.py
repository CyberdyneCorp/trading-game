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
