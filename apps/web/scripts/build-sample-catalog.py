#!/usr/bin/env python3
"""Builds the sample pack catalog served as static files by the web app (web-client tasks 4.4/5.3).

  cd bindings/python && PYTHONPATH=. uv run --python 3.12 python ../../apps/web/scripts/build-sample-catalog.py \
      --seed-file ~/.tradelab/catalog-publisher-dev.seed

Writes apps/web/static/catalog/catalog.json and packs/<digest>.tlpack. The markets are synthetic
(seeded random walks). Each one goes through the packer (tools/packer) the way a publisher pack
does: the engine masks the window into a pack source (`tl_pack_source_csv`), the manifest records
the market as synthetic with its license, the data files are zstd-compressed and the engine signs
the pack under a `publisher-` key and checks it for publishing (LIC01), so every client verifies
it with the same code. A sample with a story (sample-c-shock) adds the packer's events.json
encoding (a liquidity-outflow headline, then a liquidity shock) and pack schema v2 mission texts
and objectives, so the briefing and ticket shock warnings appear in real content. The signing seed is the development catalog key: it is created on first
use, stays outside the repository, and only its public key is pinned in the web app
(`DEV_CATALOG_KEY` in src/lib/game/packs.ts). The output is byte-for-byte reproducible for a given
seed and `zstandard` version. Needs the engine shared library (cmake --build
engine/build/native-release) and the bindings/python dev group (zstandard).
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import random
import sys
import tarfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "bindings" / "python"))
sys.path.insert(0, str(ROOT / "tools" / "packer"))

from tradelab_engine import pack_source_csv, pack_validate, pack_write, signing_key  # noqa: E402
from packer import SOURCE_ORDER, ZSTD_LEVEL, compress, encode_events, load_seed, needs_v2, stored_archive  # noqa: E402

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
    # Optional schema-v2 content: a story (packer events.json), the pack's own mission texts
    # (mission.json) and objectives (missionRules.objectives).
    story: dict[str, Any] = field(default_factory=dict)
    texts: list[dict[str, str]] = field(default_factory=list)
    objectives: list[dict[str, Any]] = field(default_factory=list)


SAMPLES = [
    Sample(
        "sample-a-1h", 101, 4800, 3600, 41_250.0, 0.0, 0.006, 5,
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
    Sample(
        "sample-c-shock", 303, 400, 3600, 2_640.0, 0.0, 0.005, 5,
        {"pt-BR": "Mercado C · choque de liquidez", "en": "Market C · liquidity shock"},
        {
            "pt-BR": "Mercado sintético em barras de 1 hora. Uma formadora de mercado sai do livro duas vezes: o spread abre e a profundidade some por algumas barras.",
            "en": "Synthetic market in one-hour bars. A market maker leaves the book twice: the spread widens and depth vanishes for a few bars.",
        },
        story={
            # Headline at the close of a bar; the shock covers the bars after it (warm-up is 60 bars).
            "news": [
                {"step": 74, "kind": "liquidity_outflow", "tone": -1, "credibility": 90, "expectation": 0, "actual": -1},
                {"step": 230, "kind": "liquidity_outflow", "tone": -1, "credibility": 80, "expectation": 0, "actual": -2},
            ],
            "liquidityShocks": [
                {"start": 75, "bars": 6, "spreadPct": 400, "capacityPct": 25},
                {"start": 231, "bars": 10, "spreadPct": 600, "capacityPct": 15},
            ],
        },
        texts=[
            {
                "locale": "pt-BR", "title": "Treino · choque de liquidez",
                "briefing": "Uma formadora de mercado vai sair do livro. Quando a manchete sair, o spread e a derrapagem ficam várias vezes maiores e só uma fração do volume pode ser negociada. Confira o custo no boleto antes de entrar ou sair, e prefira esperar o livro voltar.",
            },
            {
                "locale": "en", "title": "Practice · liquidity shock",
                "briefing": "A market maker is about to leave the book. Once the headline is out, spread and slippage are several times larger and only a fraction of the volume can trade. Check the cost on the ticket before you enter or exit, and prefer to wait for the book to come back.",
            },
        ],
        objectives=[
            {"code": "plan_every_entry", "valueBp": 0},
            {"code": "risk_per_idea", "valueBp": 100},
            {"code": "no_liquidation_wait_ok", "valueBp": 0},
        ],
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


def pack_source(s: Sample) -> tuple[dict[str, object], list[tuple[str, bytes]]]:
    """The engine's masked pack source for the sample, recorded as a synthetic market: (manifest, files)."""
    meta = {
        "asset": s.title["en"], "pair": f"{s.slug.upper()}/USD", "venue": "TradeLab sample",
        "volumeUnit": "base", "intervalSeconds": s.interval, "timestampUnit": "s", "leverageCap": s.leverage,
    }
    result, archive = pack_source_csv(csv_text(s), meta)
    if not result["ok"]:
        raise SystemExit(f"{s.slug}: {result}")
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        raw = {m.name: tar.extractfile(m).read() for m in tar.getmembers()}
    manifest = json.loads(raw.pop("manifest.json"))
    manifest["mode"] = "synthetic"  # a seeded random walk: no real market behind it
    manifest["provenance"] = {"source": "sample-random-walk", "sample": s.slug, "seed": str(s.seed), "tool": "build-sample-catalog.py"}
    manifest["license"] = {
        "name": "tradelab-synthetic", "attribution": "TradeLab sample catalog (synthetic random walk)",
        "transformation": True, "commercial": True, "offlineRedistribution": True,
    }
    manifest["distribution"] = {"downloadable": True}
    add_v2_content(s, manifest, raw)
    files = [(n, compress(raw[n], ZSTD_LEVEL) if n.endswith(".bin") else raw[n]) for n in SOURCE_ORDER if n in raw]
    return manifest, files


def add_v2_content(s: Sample, manifest: dict[str, Any], raw: dict[str, bytes]) -> None:
    """The sample's story (events.bin), texts (mission.json) and objectives; schema v2 when it has any."""
    if s.story:
        raw["events.bin"] = encode_events(s.story)
        manifest["data"]["events"] = len(s.story.get("news", []))
        manifest["data"]["liquidityShocks"] = len(s.story.get("liquidityShocks", []))
    if s.texts:
        raw["mission.json"] = json.dumps({"texts": s.texts}, ensure_ascii=False).encode()
    if s.objectives:
        manifest["missionRules"]["objectives"] = s.objectives
    if needs_v2(manifest, list(raw)):
        manifest["schemaVersion"] = 2


def build(s: Sample, seed_hex: str, key: dict[str, str]) -> dict[str, object]:
    manifest, files = pack_source(s)
    result, data = pack_write(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", stored_archive(files), seed_hex, "publisher")
    if not result["ok"]:
        raise SystemExit(f"{s.slug}: {result}")
    check = pack_validate(data, [{"id": key["id"], "key": key["publicKey"]}], publish=True)
    if not check["ok"]:
        raise SystemExit(f"{s.slug}: {check}")
    digest = hashlib.sha256(data).hexdigest()
    (OUT / "packs").mkdir(parents=True, exist_ok=True)
    (OUT / "packs" / f"{digest}.tlpack").write_bytes(data)
    info = check["pack"]
    return {
        "id": s.slug, "file": f"packs/{digest}.tlpack", "digest": digest, "size": len(data),
        "scenarioId": info["scenarioId"], "keyId": key["id"], "intervalSeconds": s.interval,
        "bars": info["totalBars"] - info["warmupBars"], "leverageCap": info["leverageCap"], "mode": info["mode"],
        "title": s.title, "summary": s.summary,
    }


def kept_entries(only: list[str]) -> dict[str, dict[str, object]]:
    """With --only, the current catalog entries of every other sample (a published pack keeps its
    bytes, and so its digest, until it is rebuilt on purpose)."""
    if not only:
        return {}
    unknown = set(only) - {s.slug for s in SAMPLES}
    if unknown:
        raise SystemExit(f"unknown sample: {', '.join(sorted(unknown))}")
    current = json.loads((OUT / "catalog.json").read_text())["packs"]
    return {e["id"]: e for e in current if e["id"] not in only}


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--seed-file", type=Path, required=True, help="development catalog signing seed (kept out of the repo)")
    p.add_argument("--only", action="append", default=[], metavar="SLUG",
                   help="rebuild only this sample (repeatable); the other entries and their files are kept as they are")
    args = p.parse_args(argv)
    args.seed_file.parent.mkdir(parents=True, exist_ok=True)
    seed_hex = load_seed(args.seed_file)
    key = signing_key(seed_hex, "publisher")
    kept = kept_entries(args.only)
    for old in (OUT / "packs").glob("*.tlpack"):
        if old.name not in {Path(e["file"]).name for e in kept.values()}:
            old.unlink()
    packs = [kept[s.slug] if s.slug in kept else build(s, seed_hex, key) for s in SAMPLES]
    catalog = {"schemaVersion": CATALOG_SCHEMA, "packs": packs}
    (OUT / "catalog.json").write_text(json.dumps(catalog, indent=1, ensure_ascii=False) + "\n")
    print(json.dumps({"keyId": key["id"], "publicKey": key["publicKey"], "packs": [x["file"] for x in packs]}, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
