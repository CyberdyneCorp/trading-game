#!/usr/bin/env python3
"""Builds the sample pack catalog served as static files by the web app (web-client tasks 4.4/5.3).

  python3 apps/web/scripts/build-sample-catalog.py --seed-file ~/.tradelab/catalog-dev.seed

Writes apps/web/static/catalog/catalog.json and packs/<digest>.tlpack. The markets are synthetic
(seeded random walks), built and signed through the engine C ABI (`tl_pack_build_csv`), so every
client verifies them with the same code. The signing seed is the development catalog key: it is
created on first use, stays outside the repository, and only its public key is pinned in the web
app (`PINNED_PUBLISHER_KEYS` in src/lib/game/packs.ts). The output is byte-for-byte reproducible
for a given seed. Needs the engine shared library (cmake --build engine/build/native-release).

Until the publisher signing step exists in the packer (add-scenarios-and-gameplay 5.1), the sample
packs use the CSV import path: they are private packs signed with a `device-` key id.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import random
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "bindings" / "python"))

from tradelab_engine import build_csv_pack, device_key, pack_validate  # noqa: E402

OUT = ROOT / "apps" / "web" / "static" / "catalog"
CATALOG_SCHEMA = 1


@dataclass(frozen=True)
class Sample:
    slug: str
    seed: int
    rows: int
    interval: int
    start_price: float
    drift: float  # per bar
    vol: float  # per bar
    leverage: int
    title: dict[str, str]
    summary: dict[str, str]


SAMPLES = [
    Sample(
        "sample-a-1h", 101, 2400, 3600, 41_250.0, 0.0, 0.006, 5,
        {"pt-BR": "Mercado A · 1h", "en": "Market A · 1h"},
        {
            "pt-BR": "Mercado sintético mascarado, em barras de 1 hora. Pratique plano, stop e tamanho antes de cada entrada.",
            "en": "Masked synthetic market in one-hour bars. Practice the plan, the stop and the size before every entry.",
        },
    ),
    Sample(
        "sample-b-4h", 202, 600, 14400, 1_830.0, 0.0008, 0.012, 3,
        {"pt-BR": "Mercado B · 4h", "en": "Market B · 4h"},
        {
            "pt-BR": "Mercado sintético mascarado, em barras de 4 horas, com alavancagem até 3×.",
            "en": "Masked synthetic market in four-hour bars, with leverage up to 3×.",
        },
    ),
]


def csv_text(s: Sample) -> str:
    """A seeded random walk with regime-like volatility clusters (stdlib `random` is stable)."""
    rng = random.Random(s.seed)
    lines = ["timestamp,open,high,low,close,volume"]
    close = s.start_price
    t0 = 1_600_000_000
    for i in range(s.rows):
        cluster = 1.0 + 0.8 * math.sin(i / 97.0) ** 2
        ret = s.drift + s.vol * cluster * (rng.random() + rng.random() + rng.random() - 1.5) * 2.0
        open_ = close
        close = max(open_ * (1.0 + ret), 0.01)
        wick = abs(ret) * 0.5 + s.vol * rng.random() * 0.6
        high = max(open_, close) * (1.0 + wick * rng.random())
        low = min(open_, close) * (1.0 - wick * rng.random())
        volume = 100.0 + 400.0 * rng.random() * cluster
        lines.append(f"{t0 + i * s.interval},{open_:.2f},{high:.2f},{low:.2f},{close:.2f},{volume:.1f}")
    return "\n".join(lines) + "\n"


def load_seed(path: Path) -> str:
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(os.urandom(32).hex() + "\n")
        path.chmod(0o600)
    return path.read_text().strip()


def build(s: Sample, seed_hex: str, key: dict[str, str]) -> dict[str, object]:
    meta = {
        "asset": s.title["en"], "pair": f"{s.slug.upper()}/USD", "venue": "TradeLab sample",
        "volumeUnit": "base", "intervalSeconds": s.interval, "timestampUnit": "s", "leverageCap": s.leverage,
    }
    result, data = build_csv_pack(csv_text(s), meta, seed_hex)
    if not result["ok"]:
        raise SystemExit(f"{s.slug}: {result}")
    check = pack_validate(data, [{"id": key["id"], "key": key["publicKey"]}])
    if not check["ok"]:
        raise SystemExit(f"{s.slug}: {check}")
    digest = hashlib.sha256(data).hexdigest()
    (OUT / "packs").mkdir(parents=True, exist_ok=True)
    (OUT / "packs" / f"{digest}.tlpack").write_bytes(data)
    info = check["pack"]
    return {
        "id": s.slug, "file": f"packs/{digest}.tlpack", "digest": digest, "size": len(data),
        "scenarioId": info["scenarioId"], "keyId": key["id"], "intervalSeconds": s.interval,
        "bars": info["totalBars"] - info["warmupBars"], "leverageCap": info["leverageCap"],
        "title": s.title, "summary": s.summary,
    }


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--seed-file", type=Path, required=True, help="development catalog signing seed (kept out of the repo)")
    args = p.parse_args(argv)
    seed_hex = load_seed(args.seed_file)
    key = device_key(seed_hex)
    for old in (OUT / "packs").glob("*.tlpack"):
        old.unlink()
    packs = [build(s, seed_hex, key) for s in SAMPLES]
    catalog = {"schemaVersion": CATALOG_SCHEMA, "packs": packs}
    (OUT / "catalog.json").write_text(json.dumps(catalog, indent=1, ensure_ascii=False) + "\n")
    print(json.dumps({"keyId": key["id"], "publicKey": key["publicKey"], "packs": [x["file"] for x in packs]}, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
