#!/usr/bin/env python3
"""Generates the PWA icons (pixel-art "TL" on the terminal background). Stdlib only."""
import pathlib
import struct
import zlib

BG = (0x0B, 0x10, 0x10)
FG = (0x39, 0xFF, 0x88)
GLYPHS = [  # 5x7 pixel font, "T" and "L"
    ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
]


def pixels(size: int) -> list[list[tuple[int, int, int]]]:
    img = [[BG] * size for _ in range(size)]
    cell = size // 16  # 2 glyphs of 5 cols + 1 gap = 11 cols, 7 rows, centered on a 16-grid
    ox = (size - 11 * cell) // 2
    oy = (size - 7 * cell) // 2
    for g, glyph in enumerate(GLYPHS):
        for r, row in enumerate(glyph):
            for c, bit in enumerate(row):
                if bit != "1":
                    continue
                x0 = ox + (g * 6 + c) * cell
                y0 = oy + r * cell
                for y in range(y0, y0 + cell):
                    for x in range(x0, x0 + cell):
                        img[y][x] = FG
    return img


def png(size: int) -> bytes:
    raw = b"".join(b"\x00" + bytes(v for px in row for v in px) for row in pixels(size))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


if __name__ == "__main__":
    out = pathlib.Path(__file__).resolve().parent.parent / "static" / "icons"
    out.mkdir(parents=True, exist_ok=True)
    for s in (192, 512):
        (out / f"icon-{s}.png").write_bytes(png(s))
    print("icons written to", out)
