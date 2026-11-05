# TradeLab desktop (Linux + macOS)

C++20 client on [Raven](https://github.com/oscar-soirey/Raven) (C11 GUI library, Apache-2.0) that links the engine (`tradelab::engine`) directly. The web client (`apps/web`) is the reference for every screen, behaviour and text. Plan and requirements: `openspec/changes/add-native-clients` (`desktop-client` spec, design "Desktop", tasks section 1).

**Status: foundation.** Mission menu (12 missions), briefing, a first terminal (candle chart, Space / Shift+Space to advance 1 / 5 bars, market buy/sell with comma input, the account line) and a first debrief, in PT-BR (default) and en. Runs are saved on the device (SQLite, crash-safe, resumable) and packs install through the engine (CSV import, catalog downloads) at the model level (see [Storage and packs](#storage-and-packs)). Not yet: the full terminal, the screens for resume / catalog / CSV import, audio, theme/CRT, packaging, updates (see the open tasks). Guest-only and offline: sign-in and sync wait for Cyberdyne Auth. Windows is out of scope for this change.

## Requirements

| | Linux (x86-64, glibc) | macOS (arm64) |
|---|---|---|
| Build | CMake ≥ 3.28, Ninja, Clang 18 (`CC=clang CXX=clang++`: the engine's `-Wpedantic -Werror` build rejects `__int128` under GCC, as in `engine.yml`), git, `libx11-dev` | Xcode command-line tools, CMake ≥ 3.28, Ninja |
| Run | `libvulkan1` and a Vulkan driver (`mesa-vulkan-drivers`; lavapipe is a software fallback); X11 or Wayland; optional `libcurl4` for the pack catalog | MoltenVK: `brew install molten-vk` (the app bundle will ship its own, task 1.4/1.18); the system libcurl |
| Tests | for windowed tests: `xvfb` | — |

Raven, the Vulkan headers and doctest are fetched at configure time (network once; then the build folder's `_deps` is reused). Raven loads the Vulkan library at run time; nothing Vulkan is linked.

## Build and run

```bash
cd apps/desktop
cmake --preset macos-release      # or linux-release
cmake --build build/macos-release -j 8
./build/macos-release/tradelab    # --seed N: fixed market (like the web's ?seed=)
```

Presets: `macos-release` (Cocoa + Vulkan via MoltenVK, arm64), `linux-release` (X11 + Wayland, Vulkan), `linux-headless` (Raven `RAVEN_PLATFORM=null`, `RAVEN_BACKEND_VULKAN=OFF`: builds and tests on machines without X11 or a GPU; the app binary there only prints the Vulkan message).

Offline / cached builds: `-DFETCHCONTENT_SOURCE_DIR_RAVEN=<clone at the pinned sha>`, `-DFETCHCONTENT_SOURCE_DIR_RAVEN_VULKAN_HEADERS=<Vulkan-Headers checkout>`, `-DFETCHCONTENT_SOURCE_DIR_DOCTEST=<doctest 2.4.11>`. `-DTRADELAB_RAVEN_MIRROR=ON` fetches Raven from the organization mirror (`TRADELAB_RAVEN_MIRROR_URL`, default `CyberdyneCorp/raven-mirror`; the mirror repository has to be created by an org admin before it is used).

The configure step fails if global C/C++ flags contain `-ffast-math`, `-Ofast`, `-funsafe-math-optimizations` or `-ffp-contract=fast`: they would reach the engine and break its determinism. Engine golden digests are unchanged by this app (`tradelab_engine_tests` runs in `engine/`).

Without a usable Vulkan driver the app prints (and, with `zenity`/`kdialog` on Linux or natively on macOS, shows) "O TradeLab precisa de um driver Vulkan…" with the platform's fix and Raven's detail, and exits with status 3. `TRADELAB_NO_DIALOG=1` keeps it to stderr. The language follows `LC_ALL` / `LC_MESSAGES` / `LANG` (macOS: then the preferred languages); en only when English, PT-BR otherwise.

## Raven pin

Raven is pre-1.0 and changes daily, so it is pinned to one full commit, `0fc738631c33844d6da732210267d21b5223f960` (`TRADELAB_RAVEN_SHA` in `cmake/Dependencies.cmake`), never a branch or tag.
The Vulkan headers are declared first, pinned by the commit of tag `v1.3.290`:
`b379292b2ab6df5771ba9870d53cf8b2c9295daf` (the pin check only reads lines that name the GUI library).

Bumping Raven: change `TRADELAB_RAVEN_SHA`, this README and `openspec/changes/add-native-clients/design.md` (and any other OpenSpec file naming the sha) in a PR of its own, run the full windowed suite on Linux and macOS and attach the captures. `python3 scripts/check_raven_pin.py` (CI) fails when the sha differs between them. Raven calls live only in `src/view/` (thin adapter in `src/view/raven/`), so API churn stays there; patches that cannot wait for upstream go to `third_party/raven-patches/` (none yet).

## Layout (MVVM)

```
CMakeLists.txt, CMakePresets.json
cmake/            Dependencies.cmake (pins), FloatFlagsGuard.cmake, EmbedResources.cmake
src/model/        tradelab_desktop_model: engine adapter (engine_run: C ABI tl_*), EngineHost (worker thread,
                  command queue, coalesced snapshots), json reader, i18n catalog + {name} formatter, embedded resources
  storage/        SQLite layer, schema + migrations, RunStore, logged actions, RunRecorder (commit, checkpoint,
                  resume), progress v2, user-data dir + instance lock, fault hook (crash test)
  packs/          PackLibrary (verify + install, CSV import), catalog, resumable downloads, libcurl client (dlopen)
third_party/      sqlite/ (amalgamation 3.54.1; version and hashes in third_party/README.md)
src/viewmodel/    tradelab_desktop_viewmodel: AppViewModel (screen flow, commands, texts per named widget),
                  format (port of the web's format.ts), chart_geometry, vulkan_message, widget_names
src/view/         tradelab_desktop_view (the only target linking Raven): App wiring, AppView (XML screens in a
                  Switcher, actions, keyboard hook), chart painter; raven/ adapter (Rtask_post, actions, find, text)
src/platform/     system language, native error alert
src/main.cpp      tradelab executable
resources/        views/*.xml (one per screen), i18n/strings.csv (generated), i18n/desktop.json (desktop-only
                  texts), packs/pinned_keys.json (generated)
scripts/          gen_i18n.mjs, check_i18n.mjs, i18n_lib.mjs, check_raven_pin.py
tests/            unit (model + viewmodel), headless, windowed, cmake rules, scripts; support/ (driver, PNG, UI queue)
```

`tradelab_desktop_model` and `tradelab_desktop_viewmodel` do not link Raven, so a Raven include there does not compile (checked by a test). Threading: the UI thread (the one that called `Rinit`) owns every widget; `EngineHost` owns the engine on one worker thread; commands go through a FIFO queue and each result comes back as an immutable `Snapshot` posted with `Rtask_post`, coalesced to at most one hand-off per frame (acknowledgements are merged, never lost). The viewmodel sends one command at a time and never blocks.

Views are embedded in the binary (`cmake/EmbedResources.cmake`); widget and action names are shared constants (`src/viewmodel/widget_names.hpp`) checked against the XML by the headless test.

## Storage and packs

Everything saved lives in one folder: macOS `~/Library/Application Support/TradeLab/`, Linux `$XDG_DATA_HOME/tradelab/` (default `~/.local/share/tradelab/`); `TRADELAB_DATA_DIR` overrides both. Inside: `tradelab.db` (SQLite, WAL, `synchronous=FULL`) and `packs/<digest>.tlpack`. A `flock` on `tradelab.lock` keeps a second instance out ("TradeLab is already open"); a folder that cannot be opened leaves the session playable but unsaved.

- **Schema** (`src/model/storage/schema.cpp`, `PRAGMA user_version`, one step per web DB version): v1 `runs`, `actions`, `meta`; v2 `packs` + `runs.pack_digest`; v3 `checkpoints`; v4 `downloads`. Released steps are never edited; a database from a newer build is refused, not rewritten.
- **Action log, commit-before-engine** (`EngineHost` + `storage::RunRecorder`): every command that changes a run is a `storage::Action`, the web's `Action` JSON (`{"k":"order",...}`, `{"k":"adv","n":5}`, ...). It is committed in its own transaction, then applied with `apply_action` (live and on replay, so client ids and digests match), and acknowledged only after the run header (digest, status, equity) and, every 20 actions, a checkpoint are written. New commands follow the same path. A failed write never stops play: the snapshot's `durable` turns false.
- **Resume** (`EngineCommands::resume`): the active run is rebuilt from its latest checkpoint (written by this engine version, restored digest checked) plus the tail of the log, else from the whole log. Notices in `Snapshot::resume_notice`: `engine_changed` (the run started on another engine version: full replay on this one, the web's `engine.pinMissing`), `policy_changed`, `digest_mismatch`, `pack_missing` (`pack.missing`), `damaged`.
- **Packs** (`src/model/packs/`, used on the worker through `EngineHost::post_storage_job` because the engine's run-less calls share a buffer): install = size limit, `tl_pack_validate` against the pinned publisher keys (`resources/packs/pinned_keys.json`, generated from the web's `packs.ts`) plus this device's key, SHA-256 equal to the engine's digest, atomic write (temp, fsync, rename), index row. CSV import = `tl_csv_validate` / `tl_pack_build_csv` signed with the device seed kept in `meta`, then the same install. `PackLibrary::missing()` is the start-up file check.
- **Catalog and downloads**: the web's static manifest (`apps/web/static/catalog/catalog.json` format, cached for offline reading); 64 KB `Range` requests appended (fsync) to `packs/<digest>.part`, the offset committed to SQLite after each chunk, so a dropped connection or a quit resumes from the committed offset; then size + SHA-256 against the catalog and the install above. HTTP goes through libcurl opened at run time (`/usr/lib/libcurl.4.dylib` on macOS, `libcurl.so.4` on Linux): when it is missing the catalog is unavailable and everything else works offline.

## Texts (PT-BR first, en second)

Never retyped: `scripts/gen_i18n.mjs` reads `apps/web/src/lib/game/i18n.svelte.ts` (`pt`, `en`), `missions.ts` (mission texts → `mission.<id>.<field>`) and `packs.ts` (pinned publisher keys) with the web's own TypeScript compiler API, adds `resources/i18n/desktop.json`, and writes `resources/i18n/strings.csv` (Raven's locale CSV, `key,pt-BR,en`) and `resources/packs/pinned_keys.json`.

```bash
(cd apps/web && npm ci)                      # the generator uses apps/web/node_modules/typescript
node apps/desktop/scripts/gen_i18n.mjs       # after any web i18n / missions / packs / desktop.json change
node apps/desktop/scripts/check_i18n.mjs     # CI: drift, duplicate keys, placeholder mismatch
node --test apps/desktop/tests/scripts/check_i18n.test.mjs
```

Numbers follow the web's `format.ts` (ported with its test vectors): decimal comma and `.` grouping in PT-BR, never `-0,00`; typed numbers accept either decimal mark (`1,5` = 1.5).

## Test

```bash
ctest --preset macos-release           # unit, headless, cmake rules, scripts (not windowed)
ctest --test-dir build/macos-release -L windowed --output-on-failure   # needs a GPU (MoltenVK) and a display
```

| Label | What |
|---|---|
| `unit` | `tradelab_desktop_unit_tests` (doctest): json, i18n, format (web vectors), chart geometry, AppViewModel with a fake engine and with the real EngineHost, EngineHost (50-step batch = 50 single steps by digest, sink only on the UI thread, FIFO under concurrent enqueue, coalescing), Vulkan message; storage (migrations from an empty and a v1 database, newer schema refused, log/checkpoints/progress/settings/packs, paths with and without `XDG_DATA_HOME`, lock contention, resume from checkpoint + tail, foreign or damaged checkpoint = full replay, engine/policy/digest notices, host commits before acknowledging); **OFF02**: `tradelab_desktop_crash_child` plays 30 commands through EngineHost + SQLite and is SIGKILLed at `before-commit`, `mid-commit`, `after-commit` and `after-apply` (`TRADELAB_FAULT=<point>:<n>`), the restart must resume with the expected digest and fill count and finish like an uninterrupted run; packs (sample catalog packs with the pinned key, tampered/truncated/foreign-key refused with the engine reason, CSV import, missing files, catalog parsing, downloads interrupted at 60% and resumed after a restart, libcurl against a loopback server); scripted: import a CSV, play it, quit, resume, finish |
| `headless` | `tradelab_desktop_headless_tests`: Raven null platform + null backend; every view loads, every named widget/action exists and is bound, injected clicks/keys/text play mission 01 from the menu to the debrief |
| `windowed` | `tradelab_desktop_windowed_tests`: the Vulkan renderer driven by `Rwindow_inject_*`; `Rwindow_capture` frames saved as PNG in `$TRADELAB_CAPTURE_DIR` (default `build/<preset>/captures`); m01 to the debrief and a 1024×768 terminal |
| `novulkan` | Linux: the app with no usable Vulkan driver must exit 3 with the message (PT-BR and en) |
| `cmake` | configure with `-ffast-math` fails naming the flag; a viewmodel file including a Raven header fails to build while the same file builds in the view layer |
| `script` | `check_raven_pin.py` and its tests |

Linux in Docker (what CI does):

```bash
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c '
  apt-get update && apt-get install -y build-essential clang cmake ninja-build git python3 libx11-dev zlib1g-dev \
    libvulkan1 mesa-vulkan-drivers xvfb xauth
  export CC=clang CXX=clang++
  cmake -S apps/desktop -B /tmp/b -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b -j 8
  ctest --test-dir /tmp/b -LE "windowed|novulkan" --output-on-failure
  VK_ICD_FILENAMES=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json) xvfb-run -a -s "-screen 0 1920x1080x24" \
    ctest --test-dir /tmp/b -L windowed --output-on-failure'
```

Cognitive complexity (≤ 15 per function, clang-tidy `readability-function-cognitive-complexity`, sources and tests): `scripts/check_complexity.sh build/<preset>` (`CLANG_TIDY=` picks the binary).

CI: `.github/workflows/desktop.yml` (ubuntu:24.04 container: build, i18n and pin checks, unit + headless + cmake tests, windowed tests under Xvfb with Mesa lavapipe (captures uploaded), the no-Vulkan test with `libvulkan1` removed, clang-tidy complexity; `linux-headless` preset build + tests; macOS: build + unit/headless tests).
