#!/usr/bin/env python3
"""TradeLab packer (design D5, minimal): builds and checks scenario packs through the engine C ABI,
so it reports exactly what every client reports.

  packer.py key --seed-file dev.seed
  packer.py import-csv data.csv --pair BTC/USDT --interval 3600 --unit ms --seed-file dev.seed -o out.tlpack
  packer.py validate out.tlpack --key device-0123456789abcdef=<64 hex> [--publish]

Needs the engine shared library (cmake --build engine/build/native-release --target tradelab_capi).
Market4Me pulls (`m4m-pull`), masking of external windows, `generate`, `sign` with publisher keys
and `inspect` are not built yet (add-scenarios-and-gameplay tasks 1b.x, 5.1).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bindings" / "python"))

from tradelab_engine import build_csv_pack, device_key, pack_validate  # noqa: E402


def load_seed(path: Path) -> str:
    """The signing seed (64 hex) in `path`, created with os.urandom when missing."""
    if not path.exists():
        path.write_text(os.urandom(32).hex() + "\n")
        path.chmod(0o600)
    return path.read_text().strip()


def cmd_key(args: argparse.Namespace) -> int:
    print(json.dumps(device_key(load_seed(args.seed_file))))
    return 0


def cmd_import_csv(args: argparse.Namespace) -> int:
    meta = {
        "asset": args.asset, "pair": args.pair, "venue": args.venue, "volumeUnit": args.volume,
        "intervalSeconds": args.interval, "timestampUnit": args.unit, "leverageCap": args.leverage,
    }
    result, data = build_csv_pack(args.csv.read_text(), meta, load_seed(args.seed_file))
    print(json.dumps(result, indent=2))
    if not result["ok"]:
        return 1
    args.output.write_bytes(data)
    return 0


def parse_key(text: str) -> dict[str, str]:
    key_id, _, hex_key = text.partition("=")
    if not key_id or len(hex_key) != 64:
        raise argparse.ArgumentTypeError("expected <id>=<64 hex>")
    return {"id": key_id, "key": hex_key}


def cmd_validate(args: argparse.Namespace) -> int:
    result = pack_validate(args.pack.read_bytes(), args.key, publish=args.publish)
    print(json.dumps(result, indent=2))
    return 0 if result["ok"] else 1


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="packer", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)

    key = sub.add_parser("key", help="print the public key of a signing seed (created if missing)")
    key.add_argument("--seed-file", type=Path, required=True)
    key.set_defaults(func=cmd_key)

    imp = sub.add_parser("import-csv", help="CSV -> private, masked, signed pack")
    imp.add_argument("csv", type=Path)
    imp.add_argument("--asset", default="")
    imp.add_argument("--pair", default="")
    imp.add_argument("--venue", default="")
    imp.add_argument("--interval", type=int, default=3600, help="bar interval in seconds")
    imp.add_argument("--unit", choices=["s", "ms"], default="s", help="unit of numeric timestamps")
    imp.add_argument("--volume", choices=["base", "quote"], default="base")
    imp.add_argument("--leverage", type=int, default=5)
    imp.add_argument("--seed-file", type=Path, required=True)
    imp.add_argument("-o", "--output", type=Path, required=True)
    imp.set_defaults(func=cmd_import_csv)

    val = sub.add_parser("validate", help="verify a pack against trusted keys")
    val.add_argument("pack", type=Path)
    val.add_argument("--key", type=parse_key, action="append", default=[], help="<id>=<64 hex>, repeatable")
    val.add_argument("--publish", action="store_true", help="also check publishing rights (LIC01)")
    val.set_defaults(func=cmd_validate)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
