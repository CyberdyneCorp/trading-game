# Vendored third-party code

Unmodified copies, compiled as C99 into `tradelab_vendor` (see `../CMakeLists.txt`) with their own
warnings silenced and the engine's floating-point flags. Neither library uses floating point,
clocks, threads or I/O on the paths the engine calls, so both are deterministic on every target.
Neither is scanned by `tools/check_engine_bans.py` or clang-tidy (they lint `src/` and `include/`).

| Library | Version | Used for | License | Source archive (SHA-256) |
|---|---|---|---|---|
| [Monocypher](https://monocypher.org) | 4.0.2 | Ed25519 (RFC 8032, SHA-512 variant in `optional/monocypher-ed25519.c`): pack signing and verification | CC0-1.0 or BSD-2-Clause (`monocypher/LICENCE.md`) | `monocypher-4.0.2.tar.gz` `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c` |
| [Zstandard](https://github.com/facebook/zstd) | 1.5.7 | Decompressing pack data files | BSD-3-Clause or GPL-2.0 (`zstd/LICENSE`, `zstd/COPYING`); we use BSD | `zstd-1.5.7.tar.gz` `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3` (matches the release's `.sha256`) |

## What is copied

- `monocypher/`: `src/monocypher.{c,h}`, `src/optional/monocypher-ed25519.{c,h}`, `LICENCE.md`.
- `zstd/zstddeclib.c`: upstream `build/single_file_libs/zstddeclib-in.c`, renamed. It is the
  official decoder-only amalgamation input: it sets the decoder options (no legacy formats, no
  assembly, stripped error strings) and `#include`s the decoder sources below. We compile it as
  one translation unit instead of running upstream's `combine.py`, so no generated file is
  committed.
- `zstd/lib/`: `zstd.h`, `zstd_errors.h`, and from `common/` and `decompress/` only the files that
  translation unit includes (no compressor, no dictionary builder, no legacy decoders, no
  `huf_decompress_amd64.S`).

## Why these

Both build unchanged for native targets and Emscripten, are small (the whole engine WASM module,
including both, is about 157 KB gzipped against the 400 KB budget) and are widely reviewed:
Monocypher had a public audit (Cure53, 2020) and the zstd decoder is upstream's own, continuously
fuzzed in OSS-Fuzz. The engine tests check RFC 8032 test vectors and decode a frame made by the
`zstd` CLI; `bindings/python/tests/test_packs.py` verifies engine-made signatures with
`cryptography` and engine-made frames with `zstandard` when those packages are installed.

## Updating

Download the release archive, check its SHA-256 against the publisher's, copy the same file list
over these directories, update the table above, and run the engine tests natively and under WASM.
Do not edit the copies; wrap them in `src/` instead (`src/ed25519.cpp`, `src/zstd_frame.cpp`).
