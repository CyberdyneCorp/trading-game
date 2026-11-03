"""FastAPI application factory (adapters wired here; routes stay thin).

No `from __future__ import annotations` here: FastAPI must resolve the dependency aliases
defined inside create_app at runtime.
"""


import json
from collections.abc import Callable, Iterator
from datetime import UTC, datetime
from typing import Annotated, Any

from fastapi import Depends, FastAPI, Header, HTTPException, Request
from fastapi.responses import JSONResponse
from sqlalchemy.orm import Session, sessionmaker

from ..adapters.db import SqlIdempotencyStore, SqlRunRepository, make_engine, request_hash, session_factory
from ..adapters.oidc import InvalidToken
from ..application.game import MAX_RUNS_PER_BATCH, GameService, run_view
from ..config import Settings
from ..domain.errors import DomainError, InvalidLog, NotFound
from ..domain.models import IncomingRun
from ..ports import Replayer, TokenVerifier
from .schemas import SyncBatchIn, SyncOutcomeOut


class SystemClock:
    def now(self) -> datetime:
        return datetime.now(UTC)


def create_app(settings: Settings, verifier: TokenVerifier, replayer: Replayer, action_kinds: frozenset[str],
               clock: Any | None = None) -> FastAPI:
    app = FastAPI(title="TradeLab API", version="0.1.0", docs_url="/api/docs", openapi_url="/api/openapi.json")
    factory: sessionmaker[Session] = session_factory(make_engine(settings.database_url))
    clock = clock or SystemClock()

    @app.middleware("http")
    async def limit_body(request: Request, call_next: Callable[..., Any]) -> Any:
        length = request.headers.get("content-length")
        if length and int(length) > settings.max_body_bytes:
            return JSONResponse({"detail": "request too large"}, status_code=413)
        return await call_next(request)

    def session() -> Iterator[Session]:
        with factory() as s:
            yield s

    def player(authorization: Annotated[str | None, Header()] = None) -> str:
        if not authorization or not authorization.lower().startswith("bearer "):
            raise HTTPException(401, "missing bearer token")
        try:
            return str(verifier.verify(authorization[7:])["sub"])
        except InvalidToken as exc:
            raise HTTPException(401, "invalid token") from exc

    def service(s: Annotated[Session, Depends(session)]) -> GameService:
        return GameService(SqlRunRepository(s), replayer, clock, action_kinds)

    Player = Annotated[str, Depends(player)]
    Service = Annotated[GameService, Depends(service)]
    Db = Annotated[Session, Depends(session)]

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

    @app.exception_handler(DomainError)
    async def domain_error(_: Request, exc: DomainError) -> JSONResponse:
        status = {NotFound: 404, InvalidLog: 413}.get(type(exc), 422)
        return JSONResponse({"detail": exc.code}, status_code=status)

    @app.get("/api/v1/health")
    def health() -> dict[str, Any]:
        return {"status": "ok", "executionPolicy": replayer.supported_policy()}

    @app.get("/api/v1/missions")
    def missions() -> Any:
        return getattr(replayer, "missions", lambda: [])()

    @app.post("/api/v1/sync/batches", response_model=list[SyncOutcomeOut])
    async def sync(request: Request, body: SyncBatchIn, owner: Player, svc: Service, s: Db,
                   idempotency_key: Annotated[str | None, Header()] = None) -> Any:
        if len(body.runs) > MAX_RUNS_PER_BATCH:
            raise HTTPException(413, f"at most {MAX_RUNS_PER_BATCH} runs per batch")
        incoming = [
            IncomingRun(r.runId, r.missionId, r.seed, r.executionPolicy, [(a.seq, a.action) for a in r.actions],
                        r.clientDigest, r.clientActionCount)
            for r in body.runs
        ]

        def run() -> Any:
            out = svc.sync(owner, body.deviceId, incoming)
            return [SyncOutcomeOut(runId=o.run_id, acceptedThrough=o.accepted_through, status=o.status,
                                   serverDigest=o.server_digest, digestMatch=o.digest_match, scoreTotal=o.score_total,
                                   error=o.error).model_dump() for o in out]

        return await idempotent(request, s, owner, idempotency_key, run)

    @app.get("/api/v1/runs")
    def runs(owner: Player, svc: Service) -> list[dict[str, Any]]:
        return [run_view(r) for r in svc.runs(owner)]

    @app.get("/api/v1/runs/{run_id}")
    def run_detail(run_id: str, owner: Player, svc: Service) -> dict[str, Any]:
        r = svc.run(owner, run_id)
        return {**run_view(r), "report": r.report}

    @app.get("/api/v1/progress")
    def progress(owner: Player, svc: Service) -> list[dict[str, Any]]:
        return [{"missionId": p.mission_id, "runs": p.runs, "completed": p.completed, "bestScore": p.best_score,
                 "bestNetPnl": p.best_net_pnl} for p in svc.progress(owner)]

    @app.get("/api/v1/me/export")
    def export(owner: Player, svc: Service) -> Any:
        return svc.export(owner)

    @app.delete("/api/v1/me/data")
    async def erase(request: Request, owner: Player, svc: Service, s: Db,
                    idempotency_key: Annotated[str | None, Header()] = None) -> Any:
        return await idempotent(request, s, owner, idempotency_key, lambda: {"deletedRuns": svc.erase(owner)})

    return app


def json_dumps(value: Any) -> str:
    return json.dumps(value, separators=(",", ":"))
