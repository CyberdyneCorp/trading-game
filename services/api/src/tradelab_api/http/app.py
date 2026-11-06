"""FastAPI application factory (adapters wired here; routes stay thin).

No `from __future__ import annotations` here: FastAPI must resolve the dependency aliases
defined inside create_app at runtime.
"""


import json
from collections.abc import AsyncIterator, Callable, Iterator
from contextlib import asynccontextmanager
from datetime import UTC, datetime
from typing import Annotated, Any

from fastapi import Depends, FastAPI, Header, HTTPException, Request
from fastapi.responses import JSONResponse
from sqlalchemy.orm import Session, sessionmaker

from ..adapters.db import (
    SqlIdempotencyStore,
    SqlPreferenceStore,
    SqlRunRepository,
    make_engine,
    request_hash,
    session_factory,
)
from ..application.catalog import CatalogService
from ..application.game import MAX_RUNS_PER_BATCH, GameService, preference_view, run_view
from ..config import Settings
from ..domain.errors import DomainError, Forbidden, InvalidLog, LimitExceeded, NotFound, Unauthenticated
from ..domain.models import Achievement, IncomingRun, MissionProgress, Preference, SyncOutcome
from ..ports import ObjectStore, Replayer, TokenVerifier
from .auth import claims_dependency, player_dependency
from .body_limit import BodyLimit
from .catalog_routes import add_catalog_routes
from .schemas import PreferencesIn, RunIn, SyncBatchIn, SyncOutcomeOut

_STATUS = {NotFound: 404, InvalidLog: 413, LimitExceeded: 413, Unauthenticated: 401, Forbidden: 403}
IdempotencyKey = Annotated[str | None, Header()]


class SystemClock:
    def now(self) -> datetime:
        return datetime.now(UTC)


def create_app(settings: Settings, verifier: TokenVerifier, replayer: Replayer, action_kinds: frozenset[str],
               clock: Any | None = None, packs: ObjectStore | None = None) -> FastAPI:
    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncIterator[None]:
        yield
        getattr(replayer, "close", lambda: None)()  # older-policy replay worker pools

    app = FastAPI(title="TradeLab API", version="0.1.0", docs_url="/api/docs", openapi_url="/api/openapi.json",
                  lifespan=lifespan)
    factory: sessionmaker[Session] = session_factory(make_engine(settings.database_url))
    clock = clock or SystemClock()
    claims = claims_dependency(verifier)

    app.add_middleware(BodyLimit, max_bytes=settings.max_body_bytes)

    def session() -> Iterator[Session]:
        with factory() as s:
            yield s

    def service(s: Annotated[Session, Depends(session)]) -> GameService:
        return GameService(SqlRunRepository(s), replayer, clock, action_kinds, SqlPreferenceStore(s))

    Player = Annotated[str, Depends(player_dependency(claims))]
    Service = Annotated[GameService, Depends(service)]
    Db = Annotated[Session, Depends(session)]

    @app.exception_handler(DomainError)
    async def domain_error(_: Request, exc: DomainError) -> JSONResponse:
        return JSONResponse({"detail": exc.code}, status_code=_STATUS.get(type(exc), 422))

    @app.get("/api/v1/health")
    def health() -> dict[str, Any]:
        return {"status": "ok", "executionPolicy": replayer.supported_policy(),
                "supportedPolicies": sorted(replayer.supported_policies())}

    @app.get("/api/v1/missions")
    def missions() -> Any:
        return getattr(replayer, "missions", list)()

    @app.post("/api/v1/sync/batches", response_model=list[SyncOutcomeOut])
    async def sync(request: Request, body: SyncBatchIn, owner: Player, svc: Service, s: Db,
                   idempotency_key: IdempotencyKey = None) -> Any:
        if len(body.runs) > MAX_RUNS_PER_BATCH:
            raise HTTPException(413, f"at most {MAX_RUNS_PER_BATCH} runs per batch")
        incoming = [_incoming(r) for r in body.runs]
        return await idempotent(request, s, owner, idempotency_key,
                                lambda: [_outcome_view(o) for o in svc.sync(owner, body.deviceId, incoming)])

    @app.get("/api/v1/runs")
    def runs(owner: Player, svc: Service) -> list[dict[str, Any]]:
        return [run_view(r) for r in svc.runs(owner)]

    @app.get("/api/v1/runs/{run_id}")
    def run_detail(run_id: str, owner: Player, svc: Service) -> dict[str, Any]:
        r = svc.run(owner, run_id)
        return {**run_view(r), "report": r.report}

    @app.get("/api/v1/progress")
    def progress(owner: Player, svc: Service) -> dict[str, Any]:
        missions, earned = svc.progress(owner)
        return {"missions": [_mission_view(p) for p in missions], "achievements": [_achievement_view(a) for a in earned]}

    @app.get("/api/v1/preferences")
    def preferences(owner: Player, svc: Service) -> dict[str, Any]:
        return {"preferences": preference_view(svc.preferences(owner))}

    @app.patch("/api/v1/preferences")
    async def set_preferences(request: Request, body: PreferencesIn, owner: Player, svc: Service, s: Db,
                              idempotency_key: IdempotencyKey = None) -> Any:
        incoming = [Preference(k, v.value, v.updatedAt) for k, v in body.preferences.items()]
        return await idempotent(request, s, owner, idempotency_key,
                                lambda: {"preferences": preference_view(svc.set_preferences(owner, incoming))})

    @app.get("/api/v1/me/export")
    def export(owner: Player, svc: Service) -> Any:
        return svc.export(owner)

    @app.delete("/api/v1/me/data")
    async def erase(request: Request, owner: Player, svc: Service, s: Db, idempotency_key: IdempotencyKey = None) -> Any:
        return await idempotent(request, s, owner, idempotency_key, lambda: {"deletedRuns": svc.erase(owner)})

    add_catalog_routes(app, CatalogService(packs), claims)
    return app


async def idempotent(request: Request, s: Session, owner: str, key: str | None, run: Callable[[], Any]) -> Any:
    """Same key + same body => stored response; same key + other body => 409 (spec game-api)."""
    if not key:
        raise HTTPException(400, "Idempotency-Key header required")
    store = SqlIdempotencyStore(s)
    digest = request_hash(await request.body())
    seen = store.get(owner, key)
    if seen is not None:
        if seen[0] != digest:
            raise HTTPException(409, "Idempotency-Key reused with a different payload")
        return seen[1]
    response = run()
    store.put(owner, key, digest, response)
    s.commit()
    return response


def _incoming(r: RunIn) -> IncomingRun:
    return IncomingRun(r.runId, r.missionId, r.seed, r.executionPolicy, [(a.seq, a.action) for a in r.actions],
                       r.clientDigest, r.clientActionCount, _options(r), _campaign(r))


def _outcome_view(o: SyncOutcome) -> dict[str, Any]:
    return SyncOutcomeOut(runId=o.run_id, acceptedThrough=o.accepted_through, status=o.status,
                          serverDigest=o.server_digest, digestMatch=o.digest_match, scoreTotal=o.score_total,
                          error=o.error).model_dump()


def _mission_view(p: MissionProgress) -> dict[str, Any]:
    return {"missionId": p.mission_id, "runs": p.runs, "completed": p.completed, "bestScore": p.best_score,
            "bestNetPnl": p.best_net_pnl, "lessonPoints": p.lesson_points, "lessonTotal": p.lesson_total}


def _achievement_view(a: Achievement) -> dict[str, Any]:
    return {"id": a.id, "kind": a.kind, "missionId": a.mission_id, "campaignId": a.campaign_id, "runId": a.run_id,
            "earnedAt": a.earned_at.isoformat()}


def _options(r: RunIn) -> dict[str, Any] | None:
    """Start options without their defaults, so a default run stores None however the client sent it."""
    return (r.options.model_dump(exclude_defaults=True) or None) if r.options else None


def _campaign(r: RunIn) -> dict[str, Any] | None:
    return r.campaign.model_dump() if r.campaign else None


def json_dumps(value: Any) -> str:
    return json.dumps(value, separators=(",", ":"))
