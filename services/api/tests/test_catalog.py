"""Catalog, pack metadata and Range/ETag delivery with the entitlement gate (spec game-api "Pack delivery")."""

from __future__ import annotations

import hashlib
import json
import shutil
from pathlib import Path
from typing import Any

import pytest
from conftest import build_client

from tradelab_api.adapters.object_store import FilesystemObjectStore
from tradelab_api.domain.catalog import has_entitlement, parse_catalog
from tradelab_api.http.ranges import Unsatisfiable, byte_range, matches, range_applies

STATIC = Path(__file__).resolve().parents[3] / "apps" / "web" / "static" / "catalog"
SAMPLE = json.loads((STATIC / "catalog.json").read_text())
PREMIUM_BYTES = bytes(range(256)) * 300  # 76,800 bytes: stands in for a signed pack
PREMIUM_DIGEST = hashlib.sha256(PREMIUM_BYTES).hexdigest()
ENTITLEMENT = "cyb_tradelab:pro"


@pytest.fixture
def packs_dir(tmp_path: Path) -> Path:
    shutil.copytree(STATIC / "packs", tmp_path / "packs")
    (tmp_path / "packs" / f"{PREMIUM_DIGEST}.tlpack").write_bytes(PREMIUM_BYTES)
    premium = {**SAMPLE["packs"][1], "id": "premium-c", "digest": PREMIUM_DIGEST, "size": len(PREMIUM_BYTES),
               "file": f"packs/{PREMIUM_DIGEST}.tlpack", "entitlement": ENTITLEMENT}
    escape = {**SAMPLE["packs"][0], "id": "escape", "digest": "f" * 64, "file": "../secret.tlpack"}
    doc = {**SAMPLE, "packs": [*SAMPLE["packs"], premium, escape]}
    (tmp_path / "catalog.json").write_text(json.dumps(doc))
    return tmp_path


@pytest.fixture
def api(idp, replayer, packs_dir):
    return build_client(idp, replayer, packs=FilesystemObjectStore(packs_dir))


def bearer(idp, **claims: Any) -> dict[str, str]:
    return {"Authorization": f"Bearer {idp.token(**claims)}"}


def sample(i: int = 0) -> dict[str, Any]:
    return SAMPLE["packs"][i]


def test_anonymous_catalog_is_the_static_manifest_with_api_pack_urls(api):
    res = api.get("/api/v1/scenarios")
    assert res.status_code == 200
    assert res.headers["cache-control"] == "no-store"
    doc = res.json()
    assert doc["schemaVersion"] == SAMPLE["schemaVersion"]
    # Same entries and fields the web client reads (catalog.ts), only `file` points at the API.
    expected = [{**p, "file": f"packs/{p['digest']}"} for p in SAMPLE["packs"]]
    assert doc["packs"] == expected


def test_entitled_packs_are_listed_only_with_the_entitlement(api, idp):
    ids = lambda res: [p["id"] for p in res.json()["packs"]]
    free = [p["id"] for p in SAMPLE["packs"]]  # the static sample catalog (a, b and the liquidity-shock c)
    assert ids(api.get("/api/v1/scenarios", headers=bearer(idp))) == free
    with_ent = api.get("/api/v1/scenarios", headers=bearer(idp, entitlements=[ENTITLEMENT]))
    assert ids(with_ent) == [*free, "premium-c"]
    assert "entitlement" not in with_ent.json()["packs"][len(free)]
    assert api.get("/api/v1/scenarios", headers={"Authorization": "Bearer nope"}).status_code == 401


def test_free_pack_download_full_range_resume_and_validators(api):
    p = sample()
    data = (STATIC / p["file"]).read_bytes()
    url = f"/api/v1/packs/{p['digest']}"
    full = api.get(url)
    assert full.status_code == 200
    assert full.content == data
    assert full.headers["content-length"] == str(p["size"])
    assert full.headers["etag"] == f'"{p["digest"]}"'
    assert full.headers["accept-ranges"] == "bytes"
    assert "immutable" in full.headers["cache-control"]

    first = api.get(url, headers={"Range": "bytes=0-65535"})
    assert first.status_code == 206
    assert first.headers["content-range"] == f"bytes 0-65535/{p['size']}"
    rest = api.get(url, headers={"Range": "bytes=65536-"})  # the resume after an interruption
    assert rest.status_code == 206
    assert first.content + rest.content == data
    assert hashlib.sha256(first.content + rest.content).hexdigest() == p["digest"]

    assert api.get(url, headers={"Range": "bytes=-10"}).content == data[-10:]
    past = api.get(url, headers={"Range": f"bytes={p['size']}-"})
    assert past.status_code == 416
    assert past.headers["content-range"] == f"bytes */{p['size']}"
    assert api.get(url, headers={"Range": "bytes=0-1,5-6"}).status_code == 200  # multi-range: whole pack
    assert api.get(url, headers={"Range": "bytes=0-9", "If-Range": '"other"'}).status_code == 200
    assert api.get(url, headers={"Range": "bytes=0-9", "If-Range": full.headers["etag"]}).status_code == 206
    assert api.get(url, headers={"If-None-Match": full.headers["etag"]}).status_code == 304
    head = api.head(url, headers={"Range": "bytes=10-19"})
    assert (head.status_code, head.headers["content-length"], head.content) == (206, "10", b"")


def test_entitled_pack_fails_closed(api, idp):
    url = f"/api/v1/packs/{PREMIUM_DIGEST}"
    assert api.get(url).status_code == 401
    assert api.get(url, headers=bearer(idp)).status_code == 403
    assert api.get(url, headers=bearer(idp, entitlements="cyb_tradelab:pro")).status_code == 403  # not a list
    assert api.get(url, headers=bearer(idp, entitlements=["cyb_tradelab:basic"])).status_code == 403
    assert api.get(url, headers=bearer(idp, entitlements=[ENTITLEMENT], aud="other")).status_code == 401
    ok = api.get(url, headers={**bearer(idp, entitlements=[ENTITLEMENT]), "Range": "bytes=100-199"})
    assert ok.status_code == 206
    assert ok.content == PREMIUM_BYTES[100:200]
    assert ok.headers["cache-control"] == "private, no-store"


def test_pack_metadata_unknown_packs_and_path_escapes(api, idp):
    meta = api.get("/api/v1/scenarios/sample-b-4h/manifest").json()
    assert meta == {**sample(1), "file": f"packs/{sample(1)['digest']}"}
    assert api.get("/api/v1/scenarios/premium-c/manifest").status_code == 401
    assert api.get("/api/v1/scenarios/premium-c/manifest", headers=bearer(idp)).status_code == 403
    assert api.get("/api/v1/scenarios/nope/manifest").status_code == 404
    assert api.get("/api/v1/scenarios/escape/manifest").status_code == 404  # `../` entry was dropped
    assert api.get(f"/api/v1/packs/{'0' * 64}").status_code == 404


def test_catalog_changes_are_picked_up_and_missing_store_is_empty(api, packs_dir, idp, replayer):
    doc = json.loads((packs_dir / "catalog.json").read_text())
    doc["packs"] = doc["packs"][:1]
    (packs_dir / "catalog.json").write_text(json.dumps(doc) + " ")  # new size: new version
    assert [p["id"] for p in api.get("/api/v1/scenarios").json()["packs"]] == ["sample-a-1h"]
    empty = build_client(idp, replayer)
    assert empty.get("/api/v1/scenarios").json() == {"schemaVersion": 1, "packs": []}
    assert empty.get(f"/api/v1/packs/{sample()['digest']}").status_code == 404


def test_filesystem_store_refuses_keys_outside_its_root(packs_dir):
    (packs_dir.parent / "outside.bin").write_bytes(b"x")
    store = FilesystemObjectStore(packs_dir)
    for key in ("../outside.bin", "/etc/hosts", "packs\\x", "", "packs"):
        assert store.stat(key) is None
    assert b"".join(store.read("catalog.json", 2, 5)) == (packs_dir / "catalog.json").read_bytes()[2:7]


def test_parse_catalog_skips_malformed_and_duplicate_entries():
    good = sample()
    rows = [good, {**good, "id": "dup"}, {**good, "digest": "XYZ"}, {**good, "entitlement": "bad entitlement!"},
            {**good, "id": "abs", "digest": "a" * 64, "file": "/abs.tlpack"}, "nope"]
    assert [e.id for e in parse_catalog({"schemaVersion": 1, "packs": rows})] == ["sample-a-1h"]
    assert parse_catalog({"schemaVersion": 2, "packs": [good]}) == []
    assert parse_catalog(None) == []


def test_entitlement_matching():
    assert has_entitlement("cyb_tradelab", {"entitlements": ["cyb_tradelab:pro"]})
    assert has_entitlement("cyb_tradelab:pro", {"entitlements": ["cyb_tradelab:pro"]})
    assert not has_entitlement("cyb_tradelab:pro", {"entitlements": ["cyb_tradelab"]})
    assert not has_entitlement("cyb_tradelab", {"entitlements": ["cyb_tradelabx"]})
    assert not has_entitlement("cyb_tradelab", {"entitlements": [{"product": "cyb_tradelab"}]})
    assert not has_entitlement("cyb_tradelab", {})
    assert not has_entitlement("cyb_tradelab", None)


@pytest.mark.parametrize(("header", "size", "expected"), [
    (None, 10, None), ("bytes=0-4", 10, (0, 4)), ("bytes=5-", 10, (5, 9)), ("bytes=-3", 10, (7, 9)),
    ("bytes=-30", 10, (0, 9)), ("bytes=8-100", 10, (8, 9)), ("bytes=5-2", 10, None), ("bytes=-", 10, None),
    ("items=0-1", 10, None), ("bytes=0-1,3-4", 10, None),
])
def test_byte_range(header, size, expected):
    assert byte_range(header, size) == expected


@pytest.mark.parametrize(("header", "size"), [("bytes=10-", 10), ("bytes=-0", 10), ("bytes=0-", 0)])
def test_byte_range_unsatisfiable(header, size):
    with pytest.raises(Unsatisfiable):
        byte_range(header, size)


def test_conditional_headers():
    assert matches('"a", "b"', '"b"') and matches("*", '"b"') and matches('W/"b"', '"b"')
    assert not matches(None, '"b"') and not matches('"c"', '"b"')
    assert range_applies(None, '"b"') and range_applies('"b"', '"b"') and not range_applies('"c"', '"b"')
