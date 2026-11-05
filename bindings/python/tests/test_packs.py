"""Scenario packs and CSV import through the Python binding (spec scenario-packs).

The pack built by the engine is also checked with independent implementations where available:
the stdlib tar reader, `cryptography` for the Ed25519 signature (design D1: the server verifies
with `cryptography`) and `zstandard` for the data frames.
"""

import importlib.util
import io
import json
import sys
import tarfile
from pathlib import Path

import pytest

from tradelab_engine import EngineError, Run, build_csv_pack, csv_validate, device_key, pack_validate

SEED = "11" * 32
META = {"asset": "BTC", "pair": "BTC/USDT", "venue": "test", "volumeUnit": "base", "intervalSeconds": 3600,
        "timestampUnit": "s", "leverageCap": 4}


def make_csv(rows: int = 120, start: int = 1_700_000_000, step: int = 3600) -> str:
    lines = ["timestamp,open,high,low,close,volume"]
    close = 30_000.0
    for i in range(rows):
        o = close
        close = round(o + (((i * 7919) % 401) - 200) * 0.5, 2)
        lines.append(f"{start + i * step},{o:.2f},{max(o, close) + 12:.2f},{min(o, close) - 9:.2f},{close:.2f},{100 + i % 37}.5")
    return "\n".join(lines) + "\n"


def trusted() -> list[dict[str, str]]:
    k = device_key(SEED)
    return [{"id": k["id"], "key": k["publicKey"]}]


def test_csv_report_and_data01():
    assert csv_validate(make_csv(), META)["ok"]
    ms = make_csv(start=1_700_000_000_000, step=3_600_000)
    report = csv_validate(ms, META)
    assert not report["ok"]
    assert report["errors"] == [{"line": 2, "code": "timestamp_unit"}]
    assert csv_validate(ms, {**META, "timestampUnit": "ms"})["ok"]


def test_build_validate_and_play_a_pack():
    result, pack = build_csv_pack(make_csv(), META, SEED)
    assert result["ok"], result
    assert result["size"] == len(pack)
    check = pack_validate(pack, trusted())
    assert check["ok"]
    assert check["pack"]["leverageCap"] == 4
    assert check["pack"]["private"] is True
    assert pack_validate(pack, trusted(), publish=True)["reason"] == "private_pack"
    assert pack_validate(pack, [])["reason"] == "untrusted_key"

    with Run.from_pack(pack, trusted(), 3) as run:
        run.place_order(1, 0, 0, "1", None, False)
        run.advance(5)
        cp = run.checkpoint()
        digest = run.state()["digest"]
        with Run.restore_pack(pack, trusted(), 3, cp) as restored:
            assert restored.state()["digest"] == digest
        state = run.advance(1000)
        assert state["status"] == "finished"
        debrief = run.debrief()
    assert debrief["syntheticMarket"] is False
    assert debrief["reveal"]["pair"] == "BTC/USDT"


def test_tampered_pack_is_refused_dat02():
    _, pack = build_csv_pack(make_csv(), META, SEED)
    with tarfile.open(fileobj=io.BytesIO(pack)) as tar:
        offset = tar.getmember("bars.bin").offset_data
    tampered = bytearray(pack)
    tampered[offset + 40] ^= 1
    assert pack_validate(bytes(tampered), trusted())["reason"] == "hash_mismatch"
    with pytest.raises(EngineError, match="hash_mismatch"):
        Run.from_pack(bytes(tampered), trusted(), 1)


def test_pack_is_readable_by_independent_tools():
    _, pack = build_csv_pack(make_csv(), META, SEED)
    with tarfile.open(fileobj=io.BytesIO(pack)) as tar:
        names = tar.getnames()
        manifest = tar.extractfile("manifest.json").read()
        sig = tar.extractfile("manifest.sig").read()
        bars = tar.extractfile("bars.bin").read()
    assert names == ["manifest.json", "manifest.sig", "bars.bin", "reveal.json"]
    doc = json.loads(manifest)
    # Stored canonically (RFC 8785 under the integer-only subset): sorted keys, no whitespace.
    assert manifest == json.dumps(doc, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
    assert doc["fundingModel"]["source"] == "none"
    if importlib.util.find_spec("cryptography"):
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

        Ed25519PublicKey.from_public_bytes(bytes.fromhex(trusted()[0]["key"])).verify(sig, manifest)
    if importlib.util.find_spec("zstandard"):
        import zstandard

        raw = zstandard.ZstdDecompressor().decompress(bars)
        assert raw[:4] == b"TLB1" and len(raw) == 16 + 40 * 120


def golden_csv() -> str:
    """Same text as golden_csv() in engine/tests/test_pack.cpp (prices in integer tenths)."""
    def tenths(v: int) -> str:
        return f"{v // 10}.{v % 10}"

    lines = ["timestamp,open,high,low,close,volume,funding"]
    for i in range(150):
        o = 1000 + (i * 37) % 50
        c = o + (i * 13) % 21 - 10
        funding = "0.0001" if i % 8 == 0 else ""
        lines.append(f"{1_700_000_000 + i * 3600},{tenths(o)},{tenths(max(o, c) + 5)},{tenths(min(o, c) - 5)},{tenths(c)},"
                     f"{1000 + i},{funding}")
    return "\n".join(lines) + "\n"


def test_cross_binding_golden_pack_run_matches_native_cpp():
    """Same calls as the C++ test 'Golden: scripted run on an imported CSV pack' => identical digests."""
    seed = "7" * 64
    meta = {"asset": "X", "pair": "X/USD", "venue": "v", "volumeUnit": "base", "intervalSeconds": 3600,
            "timestampUnit": "s", "leverageCap": 5}
    result, pack = build_csv_pack(golden_csv(), meta, seed)
    assert result["ok"], result
    k = device_key(seed)
    with Run.from_pack(pack, [{"id": k["id"], "key": k["publicKey"]}], 2026) as run:
        run.set_leverage(1, 3)
        run.place_order(2, 0, 0, "10", None, False)
        run.advance(20)
        run.place_order(3, 0, 1, "25", None, False)
        run.advance(30)
        run.close_position(4)
        state = run.advance(1000)
    assert state["digest"] == "b8a96857388428be9fbd73c417462c96e5a589fc54ed698e3dc5c18e35e87c1c"
    assert state["eventsDigest"] == "e3e42dedf85efb5b7fa7b55835e1998d82b3c552100f3c017214b6b585d382da"


def test_packer_cli_round_trip(tmp_path: Path, capsys: pytest.CaptureFixture[str]):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "packer"))
    import packer

    csv = tmp_path / "btc.csv"
    csv.write_text(make_csv())
    seed = tmp_path / "dev.seed"
    out = tmp_path / "btc.tlpack"
    assert packer.main(["import-csv", str(csv), "--pair", "BTC/USDT", "--seed-file", str(seed), "-o", str(out)]) == 0
    capsys.readouterr()
    assert packer.main(["key", "--seed-file", str(seed)]) == 0
    key = json.loads(capsys.readouterr().out)
    assert packer.main(["validate", str(out), "--key", f"{key['id']}={key['publicKey']}"]) == 0
    assert packer.main(["validate", str(out), "--key", f"{key['id']}={key['publicKey']}", "--publish"]) == 1
