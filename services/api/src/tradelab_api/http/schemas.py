from __future__ import annotations

from typing import Any

from pydantic import BaseModel, Field


class ActionIn(BaseModel):
    seq: int = Field(ge=0)
    action: dict[str, Any]


class RunIn(BaseModel):
    runId: str = Field(min_length=8, max_length=80)
    missionId: str = Field(min_length=1, max_length=64)
    seed: int = Field(ge=0, le=2**32 - 1)
    executionPolicy: int = Field(ge=1)
    actions: list[ActionIn] = Field(default_factory=list, max_length=20_000)
    clientDigest: str | None = Field(default=None, max_length=64)
    clientActionCount: int | None = Field(default=None, ge=0)


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
