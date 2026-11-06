"""FastAPI application factory (adapters wired here; routes stay thin).

No `from __future__ import annotations` here: FastAPI must resolve the dependency aliases
defined inside the route registration functions at runtime.
"""


import json
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Annotated, Any

from fastapi import Depends, FastAPI, Header, HTTPException, Request, Response
from fastapi.responses import JSONResponse
from sqlalchemy.orm import Session, sessionmaker

from ..adapters import migrations
from ..adapters.db import (
    SqlErasureLog,
    SqlIdempotencyStore,
    SqlRunRepository,
    SqlWebhookReceipts,
    make_engine,
    request_hash,
    session_factory,
)
from ..adapters.webhook_signature import InvalidWebhook, MalformedWebhook, WebhookVerifier
from ..application.erasure import ErasureService
from ..application.game import MAX_RUNS_PER_BATCH, GameService, run_view
from ..application.imports import ImportService
from ..config import Settings
from ..domain.access import PermissionResolver, RoleLookup, no_lookup
from ..domain.errors import DomainError, InvalidLog, NotFound, TooLarge
from ..domain.models import IncomingRun
from ..ports import Clock, CsvValidator, Replayer, TokenVerifier
from .auth import Authenticator
from .body_limit import BodyLimit
from .rate_limit import TokenBuckets
from .schemas import (
    CsvReportOut,
    CsvValidateIn,
    ErasedOut,
    ExportOut,
    HealthOut,
    ProgressOut,
    RunDetailOut,
    RunIn,
    RunOut,
    SyncBatchIn,
    SyncOutcomeOut,
)

WEBHOOK_PATH = "/api/v1/webhooks/cyberdyne-auth"


class SystemClock:
    def now(self) -> datetime:
        return datetime.now(UTC)


@dataclass
class _Wiring:
    """What the routes depend on; the methods are FastAPI dependencies or per-request factories."""

    factory: sessionmaker[Session]
    clock: Clock
    replayer: Replayer
    action_kinds: frozenset[str]
    auth: Authenticator
    webhooks: WebhookVerifier | None
    imports: ImportService | None

    def session(self) -> Iterator[Session]:
        with self.factory() as s:
            yield s

    def game(self, s: Session) -> GameService:
        return GameService(SqlRunRepository(s), self.replayer, self.clock, self.action_kinds)

    def erasure(self, s: Session) -> ErasureService:
        return ErasureService(SqlRunRepository(s), SqlErasureLog(s), self.clock)


def create_app(settings: Settings, verifier: TokenVerifier, replayer: Replayer, action_kinds: frozenset[str],
               clock: Any | None = None, *, csv_validator: CsvValidator | None = None,
               role_lookup: RoleLookup = no_lookup, rate_clock: Callable[[], float] | None = None,
               webhook_clock: Callable[[], float] | None = None) -> FastAPI:
    app = FastAPI(title="TradeLab API", version="0.1.0", docs_url="/api/docs", openapi_url="/api/openapi.json")
    engine = make_engine(settings.database_url)
    if settings.migrate_on_start:
        migrations.upgrade(engine)
    limiter = TokenBuckets(settings.rate_limit_per_minute, settings.rate_limit_burst,
                           **({"clock": rate_clock} if rate_clock else {}))
    webhooks = None
    if settings.webhook_secret:
        webhooks = WebhookVerifier(settings.webhook_secret, settings.webhook_tolerance_seconds,
                                   **({"clock": webhook_clock} if webhook_clock else {}))
    wiring = _Wiring(
        factory=session_factory(engine),
        clock=clock or SystemClock(),
        replayer=replayer,
        action_kinds=action_kinds,
        auth=Authenticator(verifier, limiter, PermissionResolver(settings.role_client_ids(), role_lookup)),
        webhooks=webhooks,
        imports=ImportService(csv_validator, settings.max_csv_bytes) if csv_validator else None,
    )
    app.state.engine = engine
    app.state.auth = wiring.auth  # staff routes depend on auth.require(<permission>)
    app.add_middleware(BodyLimit, max_bytes=settings.max_body_bytes)
    app.add_exception_handler(DomainError, _domain_error)  # type: ignore[arg-type]
    _public_routes(app, wiring)
    _run_routes(app, wiring)
    _privacy_routes(app, wiring)
    return app


async def _domain_error(_: Request, exc: DomainError) -> JSONResponse:
    status = {NotFound: 404, InvalidLog: 413, TooLarge: 413}.get(type(exc), 422)
    return JSONResponse({"detail": exc.code}, status_code=status)


async def _idempotent(request: Request, s: Session, owner: str, key: str | None, run: Callable[[], Any]) -> Any:
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


def _public_routes(app: FastAPI, w: _Wiring) -> None:
    @app.get("/api/v1/health", response_model=HealthOut)
    def health() -> dict[str, Any]:
        return {"status": "ok", "executionPolicy": w.replayer.supported_policy()}

    @app.get("/api/v1/missions", response_model=list[dict[str, Any]])
    def missions() -> Any:
        return getattr(w.replayer, "missions", lambda: [])()


def _run_routes(app: FastAPI, w: _Wiring) -> None:
    Player = Annotated[str, Depends(w.auth.player())]
    Db = Annotated[Session, Depends(w.session)]

    @app.post("/api/v1/sync/batches", response_model=list[SyncOutcomeOut])
    async def sync(request: Request, body: SyncBatchIn, owner: Player, s: Db,
                   idempotency_key: Annotated[str | None, Header()] = None) -> Any:
        if len(body.runs) > MAX_RUNS_PER_BATCH:
            raise HTTPException(413, f"at most {MAX_RUNS_PER_BATCH} runs per batch")
        incoming = [
            IncomingRun(r.runId, r.missionId, r.seed, r.executionPolicy, [(a.seq, a.action) for a in r.actions],
                        r.clientDigest, r.clientActionCount, _options(r))
            for r in body.runs
        ]
        return await _idempotent(request, s, owner, idempotency_key,
                                 lambda: _outcomes(w.game(s).sync(owner, body.deviceId, incoming)))

    @app.get("/api/v1/runs", response_model=list[RunOut])
    def runs(owner: Player, s: Db) -> list[dict[str, Any]]:
        return [run_view(r) for r in w.game(s).runs(owner)]

    @app.get("/api/v1/runs/{run_id}", response_model=RunDetailOut)
    def run_detail(run_id: str, owner: Player, s: Db) -> dict[str, Any]:
        r = w.game(s).run(owner, run_id)
        return {**run_view(r), "report": r.report}

    @app.get("/api/v1/progress", response_model=list[ProgressOut])
    def progress(owner: Player, s: Db) -> list[dict[str, Any]]:
        return [{"missionId": p.mission_id, "runs": p.runs, "completed": p.completed, "bestScore": p.best_score,
                 "bestNetPnl": p.best_net_pnl} for p in w.game(s).progress(owner)]

    @app.get("/api/v1/me/export", response_model=ExportOut)
    def export(owner: Player, s: Db) -> Any:
        return w.game(s).export(owner)


def _privacy_routes(app: FastAPI, w: _Wiring) -> None:
    Player = Annotated[str, Depends(w.auth.player())]
    Db = Annotated[Session, Depends(w.session)]

    @app.delete("/api/v1/me/data", response_model=ErasedOut)
    async def erase(request: Request, owner: Player, s: Db,
                    idempotency_key: Annotated[str | None, Header()] = None) -> Any:
        return await _idempotent(request, s, owner, idempotency_key,
                                 lambda: {"deletedRuns": w.erasure(s).erase(owner, "player")})

    @app.post("/api/v1/scenario-imports/validate", response_model=CsvReportOut, response_model_exclude_unset=True)
    def validate_import(body: CsvValidateIn, _: Player) -> Any:
        if w.imports is None:
            raise HTTPException(503, "CSV validation unavailable")
        return w.imports.validate(body.csv, body.meta)

    @app.post(WEBHOOK_PATH, status_code=204, response_class=Response)
    async def cyberdyne_auth_webhook(request: Request, s: Db) -> Response:
        if w.webhooks is None:
            raise HTTPException(503, "webhook secret not configured")
        try:
            event = w.webhooks.verify(await request.body(), request.headers)
        except MalformedWebhook as exc:
            raise HTTPException(400, "malformed event") from exc
        except InvalidWebhook as exc:
            raise HTTPException(401, "invalid signature") from exc
        w.erasure(s).handle_event(SqlWebhookReceipts(s), event.webhook_id, event.type, event.data)
        s.commit()
        return Response(status_code=204)


def _outcomes(out: list[Any]) -> list[dict[str, Any]]:
    return [SyncOutcomeOut(runId=o.run_id, acceptedThrough=o.accepted_through, status=o.status,
                           serverDigest=o.server_digest, digestMatch=o.digest_match, scoreTotal=o.score_total,
                           error=o.error).model_dump() for o in out]


def _options(r: RunIn) -> dict[str, Any] | None:
    """Start options without their defaults, so a default run stores None however the client sent it."""
    return (r.options.model_dump(exclude_defaults=True) or None) if r.options else None


def json_dumps(value: Any) -> str:
    return json.dumps(value, separators=(",", ":"))
