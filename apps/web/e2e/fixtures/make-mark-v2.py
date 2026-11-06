#!/usr/bin/env python3
"""Builds e2e/fixtures/mark-v2.tlpack: a schema-2 pack with a mark-price series.

  cd bindings/python && PYTHONPATH=. uv run --python 3.12 python ../../apps/web/e2e/fixtures/make-mark-v2.py

`packer generate m07-trend-trailing --seed 7`, then the schema-2 content a publisher adds by hand:
a mark-price series (mark.bin) 0.25% below the last price (the gap is what the tests look for:
orders are placed and filled at the last price, margin and liquidation use the mark), the pack's
own texts (mission.json), objectives and margin tiers. Built with `packer build --prefix device`
and signed with the device key of FIXTURE_DEVICE_SEED (e2e/helpers.ts), like
branch.tlpack. Desktop tests load the same file. Needs the engine shared library and zstandard.
"""

from __future__ import annotations

import json
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "bindings" / "python"))
sys.path.insert(0, str(ROOT / "tools" / "packer"))

import packer  # noqa: E402

DEVICE_SEED = "0e2e" * 16
OUT = Path(__file__).resolve().parent / "mark-v2.tlpack"
TICK = 1_000_000  # 0.01 in 1e-8

TEXTS = [
    {"locale": "pt-BR", "title": "Treino v2 · preço de marcação",
     "briefing": "A margem deste mercado segue o preço de marcação, um pouco abaixo do último preço. Faixas próprias limitam a alavancagem acima de 20.000."},
    {"locale": "en", "title": "Practice v2 · mark price",
     "briefing": "Margin in this market follows the mark price, a little below the last price. Its own tiers cap leverage above 20,000."},
]
TIERS = [
    {"notionalFloor": "20000.00000000", "maintenanceMargin": "0.0200000000", "maxLeverage": 4},
    {"notionalFloor": "60000.00000000", "maintenanceMargin": "0.0500000000", "maxLeverage": 2},
]
OBJECTIVES = [
    {"code": "plan_every_entry", "valueBp": 0},
    {"code": "liq_distance", "valueBp": 1500},
    {"code": "risk_per_idea", "valueBp": 75},
    {"code": "unfilled_limit_ok", "valueBp": 0},
]


def tick_down(v: int) -> int:
    return (v // TICK) * TICK


def mark_table(bars: bytes) -> bytes:
    """TLM1 from the TLB1 bars: open and close 0.25% lower, half the wicks, on the tick grid."""
    magic, rows, cols, _ = struct.unpack_from("<4sIII", bars, 0)
    assert magic == b"TLB1", magic
    vals = struct.unpack_from(f"<{rows * cols}q", bars, 16)
    o, h, lo, c = (vals[i * rows:(i + 1) * rows] for i in range(4))
    mo = [tick_down(v - v // 400) for v in o]
    mc = [tick_down(v - v // 400) for v in c]
    mh = [max(a, b) + tick_down((hh - max(oo, cc)) // 2) for a, b, hh, oo, cc in zip(mo, mc, h, o, c)]
    ml = [min(a, b) - tick_down((min(oo, cc) - ll) // 2) for a, b, ll, oo, cc in zip(mo, mc, lo, o, c)]
    return packer.table(b"TLM1", [mo, mh, ml, mc])


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        src, seed = Path(tmp) / "src", Path(tmp) / "device.seed"
        seed.write_text(DEVICE_SEED + "\n")
        if packer.main(["generate", "m07-trend-trailing", "--seed", "7", "-o", str(src)]) != 0:
            return 1
        (src / "mark.bin").write_bytes(mark_table((src / "bars.bin").read_bytes()))
        (src / "mission.json").write_text(json.dumps({"texts": TEXTS}, ensure_ascii=False))
        manifest = json.loads((src / "manifest.json").read_text())
        manifest["scenarioId"] = "e2e-mark-v2"
        manifest["data"]["markBars"] = True
        manifest["marginModel"]["tiers"] = TIERS
        manifest["missionRules"]["objectives"] = OBJECTIVES
        (src / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
        return packer.main(["build", str(src), "--seed-file", str(seed), "--prefix", "device", "-o", str(OUT)])


if __name__ == "__main__":
    raise SystemExit(main())
