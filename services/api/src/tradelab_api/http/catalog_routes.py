"""Catalog and pack delivery routes (spec game-api "Pack delivery").

`GET /api/v1/scenarios` is the web client's catalog manifest (schema 1); `GET /api/v1/packs/{digest}`
serves a pack by content digest: immutable, strong ETag (the digest), `Content-Length`, single byte
ranges (206) for resumable downloads, 416 past the end, 304 on a matching If-None-Match.
"""

from collections.abc import Callable
from typing import Annotated, Any

from fastapi import Depends, FastAPI, Request, Response
from fastapi.responses import JSONResponse, StreamingResponse

from ..application.catalog import CatalogService
from ..domain.catalog import PackEntry
from ..ports import ObjectInfo
from .ranges import Unsatisfiable, byte_range, matches, range_applies

FREE_CACHE = "public, max-age=31536000, immutable"
ENTITLED_CACHE = "private, no-store"


def add_catalog_routes(app: FastAPI, catalog: CatalogService, claims_dep: Callable[..., Any]) -> None:
    Claims = Annotated[dict[str, Any] | None, Depends(claims_dep)]

    @app.get("/api/v1/scenarios")
    def scenarios(claims: Claims) -> JSONResponse:
        # Personal (entitled packs) and never cached, like the static manifest it replaces.
        return JSONResponse(catalog.listing(claims), headers={"Cache-Control": "no-store", "Vary": "Authorization"})

    @app.get("/api/v1/scenarios/{pack_id}/manifest")
    def scenario_manifest(pack_id: str, claims: Claims) -> JSONResponse:
        return JSONResponse(catalog.describe(pack_id, claims), headers={"Cache-Control": "no-store"})

    @app.api_route("/api/v1/packs/{digest}", methods=["GET", "HEAD"])
    def pack(digest: str, request: Request, claims: Claims) -> Response:
        entry, info = catalog.open(digest, claims)
        return _pack_response(catalog, entry, info, request)


def _pack_response(catalog: CatalogService, entry: PackEntry, info: ObjectInfo, request: Request) -> Response:
    etag = f'"{entry.digest}"'
    headers = {"ETag": etag, "Accept-Ranges": "bytes", "Cache-Control": FREE_CACHE if entry.free else ENTITLED_CACHE}
    if matches(request.headers.get("if-none-match"), etag):
        return Response(status_code=304, headers=headers)
    span: tuple[int, int] | None = None
    if range_applies(request.headers.get("if-range"), etag):
        try:
            span = byte_range(request.headers.get("range"), info.size)
        except Unsatisfiable:
            return Response(status_code=416, headers={**headers, "Content-Range": f"bytes */{info.size}"})
    start, end = span if span is not None else (0, info.size - 1)
    headers["Content-Length"] = str(end - start + 1)
    status = 200
    if span is not None:
        status = 206
        headers["Content-Range"] = f"bytes {start}-{end}/{info.size}"
    if request.method == "HEAD":
        return Response(status_code=status, headers=headers, media_type="application/octet-stream")
    body = catalog.read(entry, start, end - start + 1)
    return StreamingResponse(body, status_code=status, headers=headers, media_type="application/octet-stream")
