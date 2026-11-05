#!/usr/bin/env python3
"""TradeLab packer (design D5): builds and checks scenario packs through the engine C ABI, so it
reports exactly what every client reports.

Device imports (private, masked, device-signed):
  packer.py key --seed-file dev.seed
  packer.py import-csv data.csv --pair BTC/USDT --interval 3600 --unit ms --seed-file dev.seed -o out.tlpack

Publishing (a pack source is a directory with manifest.json and the uncompressed data files):
  packer.py generate m09-event-straddle --seed 7 -o src/                      # synthetic, from a mission
  packer.py mask window.csv --pair BTC/USDT --license CC-BY-4.0 --attribution "..." -o src/
  packer.py mask window.csv --branch-at 90 --branch-mission m08-regime-switch --branch-seed 4 -o src/
  packer.py build src/ --seed-file publisher.seed -o pack.tlpack              # zstd level 19, signed
  packer.py sign pack.tlpack --seed-file release.seed -o release.tlpack       # publisher re-sign
  packer.py inspect pack.tlpack [--key <id>=<64 hex>]
  packer.py validate pack.tlpack --key <id>=<64 hex> [--publish]

Needs the engine shared library (cmake --build engine/build/native-release --target tradelab_capi)
and, for `build` and `inspect`, the `zstandard` package (the bindings/python dev group has it).
Market4Me pulls (`m4m-pull`) wait for the service client and the legal review (tasks 1b.x).
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import struct
import sys
import tarfile
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bindings" / "python"))

from tradelab_engine import (  # noqa: E402
    build_csv_pack,
    device_key,
    pack_resign,
    pack_source_csv,
    pack_source_mission,
    pack_validate,
    pack_write,
    signing_key,
)

ZSTD_LEVEL = 19  # authoring time: smallest frames; clients only decode
TABLE_HEADER = struct.Struct("<4sIII")  # magic, rows, columns, 0


def load_seed(path: Path) -> str:
    """The signing seed (64 hex) in `path`, created with os.urandom when missing."""
    if not path.exists():
        path.write_text(os.urandom(32).hex() + "\n")
        path.chmod(0o600)
    return path.read_text().strip()


def emit(result: dict[str, Any]) -> None:
    print(json.dumps(result, indent=2, ensure_ascii=False))


def zstd():
    try:
        import zstandard
    except ImportError as exc:  # pragma: no cover - environment problem, not a code path
        raise SystemExit("packer: `build` and `inspect` need the zstandard package (uv sync in bindings/python)") from exc
    return zstandard


# ---------------------------------------------------------------- device imports

def cmd_key(args: argparse.Namespace) -> int:
    seed = load_seed(args.seed_file)
    emit(device_key(seed) if args.prefix == "device" else signing_key(seed, args.prefix))
    return 0


def csv_meta(args: argparse.Namespace) -> dict[str, Any]:
    meta: dict[str, Any] = {
        "asset": args.asset, "pair": args.pair, "venue": args.venue, "volumeUnit": args.volume,
        "intervalSeconds": args.interval, "timestampUnit": args.unit, "leverageCap": args.leverage,
    }
    if getattr(args, "warmup", 0):
        meta["warmupBars"] = args.warmup
    return meta


def cmd_import_csv(args: argparse.Namespace) -> int:
    result, data = build_csv_pack(args.csv.read_text(), csv_meta(args), load_seed(args.seed_file))
    emit(result)
    if not result["ok"]:
        return 1
    args.output.write_bytes(data)
    return 0


# ---------------------------------------------------------------- pack sources

def write_source(archive: bytes, out_dir: Path, patch: dict[str, Any] | None = None) -> list[str]:
    """Unpacks a source archive into `out_dir` (manifest.json pretty-printed for editing)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    names = []
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        for member in tar.getmembers():
            data = tar.extractfile(member).read()
            if member.name == "manifest.json":
                manifest = json.loads(data)
                for key, value in (patch or {}).items():
                    manifest[key] = value
                data = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode()
            (out_dir / member.name).write_bytes(data)
            names.append(member.name)
    return names


def cmd_generate(args: argparse.Namespace) -> int:
    result, archive = pack_source_mission(args.mission, args.seed)
    if result["ok"]:
        result["written"] = write_source(archive, args.output)
    emit(result)
    return 0 if result["ok"] else 1


def license_patch(args: argparse.Namespace) -> dict[str, Any]:
    """The rights the publisher records (spec "Licensing record"); publishing checks them (LIC01)."""
    provenance: dict[str, Any] = {"source": args.source or "csv-import", "tool": "tools/packer mask"}
    if args.as_of:
        provenance["asOf"] = args.as_of
    if args.csv_sha256:
        provenance["csvSha256"] = args.csv_sha256
    return {
        "license": {
            "name": args.license, "attribution": args.attribution, "transformation": args.transformation,
            "commercial": args.commercial, "offlineRedistribution": args.offline,
        },
        "distribution": {"downloadable": args.downloadable},
        "provenance": provenance,
    }


def cmd_mask(args: argparse.Namespace) -> int:
    text = args.csv.read_text()
    meta = csv_meta(args)
    if args.branch_at is not None:
        meta.update({"branchStep": args.branch_at, "branchMission": args.branch_mission, "branchSeed": args.branch_seed})
    args.csv_sha256 = hashlib.sha256(text.encode()).hexdigest()
    result, archive = pack_source_csv(text, meta)
    if result["ok"]:
        result["written"] = write_source(archive, args.output, license_patch(args))
    emit(result)
    return 0 if result["ok"] else 1


# ---------------------------------------------------------------- build, sign, inspect

def stored_archive(files: list[tuple[str, bytes]]) -> bytes:
    """A deterministic ustar archive (flat names, zero mtime/uid/gid) of the stored files."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for name, data in files:
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o644
            tar.addfile(info, io.BytesIO(data))
    return buf.getvalue()


def compress(data: bytes, level: int) -> bytes:
    """A zstd frame that declares its content size (the engine decodes into exactly that size)."""
    return zstd().ZstdCompressor(level=level, write_content_size=True, write_checksum=True).compress(data)


SOURCE_ORDER = ("bars.bin", "funding.bin", "events.bin", "options.bin", "reveal.json")


def cmd_build(args: argparse.Namespace) -> int:
    src: Path = args.source
    manifest = (src / "manifest.json").read_text()
    files = []
    raw_total = stored_total = 0
    for name in SOURCE_ORDER:
        path = src / name
        if not path.exists():
            continue
        raw = path.read_bytes()
        stored = compress(raw, args.level) if name.endswith(".bin") else raw
        raw_total += len(raw)
        stored_total += len(stored)
        files.append((name, stored))
    seed = load_seed(args.seed_file)
    result, pack = pack_write(manifest, stored_archive(files), seed, args.prefix)
    if result["ok"]:
        key = signing_key(seed, args.prefix)
        result["validation"] = pack_validate(pack, [{"id": key["id"], "key": key["publicKey"]}], publish=args.publish)
        result["compression"] = {"raw": raw_total, "stored": stored_total, "level": args.level}
        if result["validation"]["ok"]:
            args.output.write_bytes(pack)
    emit(result)
    return 0 if result["ok"] and result["validation"]["ok"] else 1


def cmd_sign(args: argparse.Namespace) -> int:
    seed = load_seed(args.seed_file)
    result, pack = pack_resign(args.pack.read_bytes(), seed, args.prefix)
    if result["ok"]:
        args.output.write_bytes(pack)
        key = signing_key(seed, args.prefix)
        result["publicKey"] = key["publicKey"]
    emit(result)
    return 0 if result["ok"] else 1


def tables(raw: bytes) -> list[dict[str, Any]]:
    """The columnar tables of a decompressed .bin file (header magic, rows, columns)."""
    out, pos = [], 0
    while pos + TABLE_HEADER.size <= len(raw):
        magic, rows, columns, _ = TABLE_HEADER.unpack_from(raw, pos)
        out.append({"magic": magic.decode("ascii", "replace"), "rows": rows, "columns": columns})
        pos += TABLE_HEADER.size + 8 * rows * columns
    return out


def describe_file(name: str, stored: bytes, listed: dict[str, Any] | None) -> dict[str, Any]:
    entry: dict[str, Any] = {"name": name, "stored": len(stored), "listed": listed is not None}
    if listed is not None:
        entry["hashOk"] = hashlib.sha256(stored).hexdigest() == listed.get("sha256")
        entry["decompressed"] = listed.get("decompressedSize")
    if name.endswith(".bin") and listed is not None:
        limit = int(listed.get("decompressedSize", 0))
        raw = zstd().ZstdDecompressor().decompress(stored, max_output_size=limit)
        entry["ratio"] = round(len(raw) / max(1, len(stored)), 2)
        entry["tables"] = tables(raw)
    return entry


def cmd_inspect(args: argparse.Namespace) -> int:
    data = args.pack.read_bytes()
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        members = {m.name: tar.extractfile(m).read() for m in tar.getmembers()}
    manifest = json.loads(members.get("manifest.json", b"{}"))
    listed = {f["name"]: f for f in manifest.get("files", [])}
    summary = {key: manifest.get(key) for key in ("scenarioId", "packVersion", "mode", "private", "signingKeyId",
                                                    "executionPolicyVersion", "scoringVersion", "timeframe", "data",
                                                    "license", "distribution", "provenance")}
    result: dict[str, Any] = {
        "digest": hashlib.sha256(data).hexdigest(), "size": len(data), "manifest": summary,
        "options": "options" in manifest.get("instruments", {}),
        "files": [describe_file(name, blob, listed.get(name)) for name, blob in members.items()
                  if name not in ("manifest.json", "manifest.sig")],
    }
    if args.key:
        result["validation"] = pack_validate(data, args.key, publish=args.publish)
    emit(result)
    return 0 if result.get("validation", {"ok": True})["ok"] else 1


def parse_key(text: str) -> dict[str, str]:
    key_id, _, hex_key = text.partition("=")
    if not key_id or len(hex_key) != 64:
        raise argparse.ArgumentTypeError("expected <id>=<64 hex>")
    return {"id": key_id, "key": hex_key}


def cmd_validate(args: argparse.Namespace) -> int:
    result = pack_validate(args.pack.read_bytes(), args.key, publish=args.publish)
    emit(result)
    return 0 if result["ok"] else 1


# ---------------------------------------------------------------- CLI

def add_csv_args(p: argparse.ArgumentParser) -> None:
    p.add_argument("csv", type=Path)
    p.add_argument("--asset", default="")
    p.add_argument("--pair", default="")
    p.add_argument("--venue", default="")
    p.add_argument("--interval", type=int, default=3600, help="bar interval in seconds")
    p.add_argument("--unit", choices=["s", "ms"], default="s", help="unit of numeric timestamps")
    p.add_argument("--volume", choices=["base", "quote"], default="base")
    p.add_argument("--leverage", type=int, default=5)
    p.add_argument("--warmup", type=int, default=0, help="bars before the first decision (0 = a quarter, 20..60)")


def add_mask_args(p: argparse.ArgumentParser) -> None:
    p.add_argument("--license", default="unlicensed")
    p.add_argument("--attribution", default="")
    p.add_argument("--no-transformation", dest="transformation", action="store_false", help="masking not licensed")
    p.add_argument("--commercial", action="store_true")
    p.add_argument("--offline", action="store_true", help="offline redistribution licensed")
    p.add_argument("--downloadable", action="store_true")
    p.add_argument("--source", default="", help="provenance source (e.g. the export id)")
    p.add_argument("--as-of", default="", help="pinned as_of of the source extract")
    p.add_argument("--branch-at", type=int, help="branch step: later bars come from --branch-mission (synthetic)")
    p.add_argument("--branch-mission", default="")
    p.add_argument("--branch-seed", type=int, default=1)


def add_device_commands(sub: Any) -> None:
    key = sub.add_parser("key", help="print the public key of a signing seed (created if missing)")
    key.add_argument("--seed-file", type=Path, required=True)
    key.add_argument("--prefix", default="device", help="key id prefix: device, publisher, ...")
    key.set_defaults(func=cmd_key)

    imp = sub.add_parser("import-csv", help="CSV -> private, masked, signed pack")
    add_csv_args(imp)
    imp.add_argument("--seed-file", type=Path, required=True)
    imp.add_argument("-o", "--output", type=Path, required=True)
    imp.set_defaults(func=cmd_import_csv)


def add_source_commands(sub: Any) -> None:
    gen = sub.add_parser("generate", help="built-in mission + seed -> synthetic pack source directory")
    gen.add_argument("mission")
    gen.add_argument("--seed", type=int, required=True)
    gen.add_argument("-o", "--output", type=Path, required=True)
    gen.set_defaults(func=cmd_generate)

    mask = sub.add_parser("mask", help="external CSV window -> masked historical (or branch) pack source")
    add_csv_args(mask)
    add_mask_args(mask)
    mask.add_argument("-o", "--output", type=Path, required=True)
    mask.set_defaults(func=cmd_mask)


def add_pack_commands(sub: Any) -> None:
    build = sub.add_parser("build", help="pack source directory -> zstd-compressed, signed pack")
    build.add_argument("source", type=Path)
    build.add_argument("--seed-file", type=Path, required=True)
    build.add_argument("--prefix", default="publisher")
    build.add_argument("--level", type=int, default=ZSTD_LEVEL)
    build.add_argument("--publish", action="store_true", help="also check publishing rights (LIC01)")
    build.add_argument("-o", "--output", type=Path, required=True)
    build.set_defaults(func=cmd_build)

    sign = sub.add_parser("sign", help="sign a built pack again with a publisher key")
    sign.add_argument("pack", type=Path)
    sign.add_argument("--seed-file", type=Path, required=True)
    sign.add_argument("--prefix", default="publisher")
    sign.add_argument("-o", "--output", type=Path, required=True)
    sign.set_defaults(func=cmd_sign)

    for name, func, text in (("inspect", cmd_inspect, "describe a pack: manifest, files, tables, compression"),
                             ("validate", cmd_validate, "verify a pack against trusted keys")):
        p = sub.add_parser(name, help=text)
        p.add_argument("pack", type=Path)
        p.add_argument("--key", type=parse_key, action="append", default=[], help="<id>=<64 hex>, repeatable")
        p.add_argument("--publish", action="store_true", help="also check publishing rights (LIC01)")
        p.set_defaults(func=func)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="packer", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    add_device_commands(sub)
    add_source_commands(sub)
    add_pack_commands(sub)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
