# tools/packer

Authoring CLI for TradeLab scenario packs (design D5 of `openspec/changes/add-scenarios-and-gameplay`). Every check runs in the engine through its C ABI, so the packer reports exactly what the web, desktop and server clients report. Format reference: `engine/README.md` "Scenario packs and CSV import".

## Setup

```bash
(cd engine && cmake --preset native-release && cmake --build build/native-release --target tradelab_capi)
cd bindings/python && uv sync --python 3.12      # the dev group brings zstandard (build, inspect)
alias packer='PYTHONPATH=. uv run --python 3.12 python ../../tools/packer/packer.py'
```

Signing seeds are 32 random bytes in hex in a file (`--seed-file`); a missing file is created with mode 0600. Keep publisher seeds out of the repository. `packer key --seed-file x.seed --prefix publisher` prints the key id and public key to pin in clients.

## Commands

| Command | What it does |
|---|---|
| `key` | public key of a seed (`--prefix device` by default) |
| `import-csv` | CSV → private, masked pack signed with the device key (the client's import wizard does the same) |
| `generate <mission> --seed N -o DIR` | a built-in mission generated for a seed → synthetic pack source (bars, funding, events, options) |
| `mask <csv> -o DIR` | an external window → masked historical pack source, with the rights you record (`--license`, `--attribution`, `--offline`, `--commercial`, `--downloadable`, `--no-transformation`, `--source`, `--as-of`); `--branch-at N --branch-mission M --branch-seed S` makes it a branch pack whose bars after N come from that mission (marked synthetic) |
| `build DIR --seed-file F -o PACK` | compresses each `.bin` with zstd (level 19, content size and checksum in the frame), has the engine sign the manifest under `<prefix>-<16 hex>` (`--prefix publisher` by default) and validates the result; `--publish` also checks the license record (LIC01) and that generated bars have their generator's calibration report in `docs/calibration/` |
| `sign PACK --seed-file F -o OUT` | signs a built pack again with another key (same files; private packs are refused) |
| `inspect PACK [--key id=hex]` | manifest summary, each file's size, hash check, compression ratio and column tables; validation when keys are given |
| `validate PACK --key id=hex [--publish]` | the engine's install (or publish) verdict |

A pack source directory holds `manifest.json` (pretty-printed; edit the license, provenance or rules before `build`; `files` and `signingKeyId` are filled in when the pack is written) and the uncompressed data files. Builds are deterministic for a given source, zstd level, `zstandard` version and key.

```bash
packer generate m09-event-straddle --seed 7 -o /tmp/m09
packer build /tmp/m09 --seed-file publisher.seed --publish -o m09-s7.tlpack
packer inspect m09-s7.tlpack
```

Not built: `m4m-pull` (Market4Me bulk exports) waits for the `cyb_tradelab_packer` service client, its allowlisting and the legal review of vendor-derived data (tasks 1b.1–1b.5). Until then publishable packs are synthetic; masked historical windows are for development (`license_incomplete` blocks publishing without recorded rights).

## Calibration report

```bash
cd bindings/python && PYTHONPATH=. uv run --python 3.12 python ../../tools/calibration_report.py --seeds 100 \
  -o ../../docs/calibration/generator-v5.md [--reference authorized.csv]
```

Writes the generator's calibration report: per mission and pooled return distribution, autocorrelation of returns and absolute returns, and drawdowns, against the stylized facts and, with `--reference`, an authorized hourly series (a CSV with a `close` column). Commit a new report with every generator version.
