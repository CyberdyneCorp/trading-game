from __future__ import annotations

from typing import Any

from pydantic import BaseModel, ConfigDict, Field, field_validator

from ..domain import preferences as prefs


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


class CampaignLinkIn(BaseModel):
    """The campaign run a run record plays and its mission index in the curriculum order."""

    model_config = ConfigDict(extra="forbid")

    id: str = Field(min_length=1, max_length=80)
    index: int = Field(ge=0, le=63)


class RunIn(BaseModel):
    runId: str = Field(min_length=8, max_length=80)
    missionId: str = Field(min_length=1, max_length=64)
    seed: int = Field(ge=0, le=2**32 - 1)
    executionPolicy: int = Field(ge=1)
    actions: list[ActionIn] = Field(default_factory=list, max_length=20_000)
    clientDigest: str | None = Field(default=None, max_length=64)
    clientActionCount: int | None = Field(default=None, ge=0)
    options: RunOptionsIn | None = None
    campaign: CampaignLinkIn | None = None


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


class PreferenceIn(BaseModel):
    model_config = ConfigDict(extra="forbid")

    value: Any
    updatedAt: int = Field(ge=0, le=2**53)  # ms since the epoch, when the player changed it

    @field_validator("value")
    @classmethod
    def small(cls, value: Any) -> Any:
        if len(prefs.canonical(value).encode()) > prefs.MAX_VALUE_BYTES:
            raise ValueError(f"at most {prefs.MAX_VALUE_BYTES} bytes per value")
        return value


class PreferencesIn(BaseModel):
    model_config = ConfigDict(extra="forbid")

    preferences: dict[str, PreferenceIn] = Field(max_length=prefs.MAX_KEYS)

    @field_validator("preferences")
    @classmethod
    def keys(cls, value: dict[str, PreferenceIn]) -> dict[str, PreferenceIn]:
        bad = [k for k in value if not prefs.KEY_RE.fullmatch(k)]
        if bad:
            raise ValueError(f"invalid preference key {bad[0]!r}")
        return value
