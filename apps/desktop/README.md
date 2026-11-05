# TradeLab desktop (Linux + macOS)

C++20 client on [Raven](https://github.com/oscar-soirey/Raven) (C11 GUI library, Apache-2.0) that links the engine (`tradelab::engine`) directly. The web client (`apps/web`) is the reference for every screen, behaviour and text. Plan and requirements: `openspec/changes/add-native-clients` (`desktop-client` spec, design "Desktop", tasks section 1).

**Status: shell screens and retro look.** Menu (resume line for a run left from the terminal, campaign panel, the 12 missions with their Completed mark, settings), briefing (mission rules, permissions, limits, margin tiers, mark-price note, costs, campaign locks), the full debrief (score dimensions, findings, next lesson, per-trade R table with the stop/liquidation advice, P&L split by Greek, spread/slippage, safe leverage, regime and release reveal tables, campaign outcome and report, decisions, advisor reads, timeline), campaign runs, curriculum map, journal, the Academy lesson runner and settings (language, shortcuts, CRT effects, text scale, colorblind palette), a first terminal (candle chart, Space / Shift+Space, market buy/sell with comma input, the account line), in PT-BR (default) and en, with the TradeLab theme, bundled fonts and a CRT post-process. Progress and settings live in memory until the SQLite store (task 1.9) backs their repositories. Not yet: the full terminal, storage and resume across launches, packs, audio, packaging, updates (see the open tasks). Guest-only and offline: sign-in and sync wait for Cyberdyne Auth. Windows is out of scope for this change.

## Requirements

| | Linux (x86-64, glibc) | macOS (arm64) |
|---|---|---|
| Build | CMake ≥ 3.28, Ninja, Clang 18 (`CC=clang CXX=clang++`: the engine's `-Wpedantic -Werror` build rejects `__int128` under GCC, as in `engine.yml`), git, `libx11-dev` | Xcode command-line tools, CMake ≥ 3.28, Ninja |
| Run | `libvulkan1` and a Vulkan driver (`mesa-vulkan-drivers`; lavapipe is a software fallback); X11 or Wayland | MoltenVK: `brew install molten-vk` (the app bundle will ship its own, task 1.4/1.18) |
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
src/viewmodel/    tradelab_desktop_viewmodel: AppViewModel (screen flow, commands, texts per named widget),
                  format (port of the web's format.ts), chart_geometry, vulkan_message, widget_names
                  shell screens: shell.cpp (run life as progress sees it, campaign, lessons, settings commands),
                  screens/ (one ScreenModel per screen: texts, screen-reader names, toned lines, tables, flags),
                  ports of the web helpers (curriculum, campaign + campaignReport, journal, shortcuts as keymap,
                  analysis/objectives/debrief texts as report_texts, the Academy lesson runner)
src/view/         tradelab_desktop_view (the only target linking Raven): App wiring, AppView (XML screens in a
                  Switcher, actions, keyboard hook), chart painter; raven/ adapter (Rtask_post, actions, find, text);
                  screens/ (ShellView: applies ScreenModels and binds the shell's callbacks; RetroLook: theme,
                  fonts, text scale, CRT overlay)
src/platform/     system language, native error alert
src/main.cpp      tradelab executable
resources/        views/*.xml (one per screen), i18n/strings.csv (generated), i18n/desktop.json (desktop-only
                  texts), packs/pinned_keys.json and web/content.json (generated), theme/*.theme.xml,
                  fonts/ (OFL), shaders/crt.frag + crt.spv
scripts/          gen_i18n.mjs, check_i18n.mjs, i18n_lib.mjs, screen_sources.mjs, check_raven_pin.py,
                  build_shaders.sh, check_complexity.sh
tests/            unit (model + viewmodel), headless, windowed, cmake rules, scripts; support/ (driver, PNG, UI queue)
```

`tradelab_desktop_model` and `tradelab_desktop_viewmodel` do not link Raven, so a Raven include there does not compile (checked by a test). Threading: the UI thread (the one that called `Rinit`) owns every widget; `EngineHost` owns the engine on one worker thread; commands go through a FIFO queue and each result comes back as an immutable `Snapshot` posted with `Rtask_post`, coalesced to at most one hand-off per frame (acknowledgements are merged, never lost). The viewmodel sends one command at a time and never blocks.

Views are embedded in the binary (`cmake/EmbedResources.cmake`); widget and action names are shared constants (`src/viewmodel/widget_names.hpp`) checked against the XML by the headless test.

## Texts (PT-BR first, en second)

Never retyped: `scripts/gen_i18n.mjs` reads `apps/web/src/lib/game/i18n.svelte.ts` (`pt`, `en`), `missions.ts` (mission texts → `mission.<id>.<field>`) and `packs.ts` (pinned publisher keys) with the web's own TypeScript compiler API, adds `resources/i18n/desktop.json`, and writes `resources/i18n/strings.csv` (Raven's locale CSV, `key,pt-BR,en`) and `resources/packs/pinned_keys.json`.

The shell screens' other web sources go through `scripts/screen_sources.mjs`: texts from `objectives.ts` (`objective.<code>`), `analysis.ts` (`finding.<code>[.safe|.one]`, `finding.liquidatedOnly`, `nextLesson.<code>`), `news.ts` (`newsKind.<kind>`, `newsLevel.<0..4>`) and `academy.ts` (`academyLesson.<id>.…`), and the screen rules as data in `resources/web/content.json` (curriculum `STAGES`, campaign constants, `REPORT_MIN_TRADES`, `AFTER_RELEASE_BARS`, `SHORTCUT_ACTIONS` / `DEFAULT_KEYMAP`, and academy `LESSONS`, read by transpiling the module since it is built with helpers). `check_i18n.mjs` fails when any of them drifts.

```bash
(cd apps/web && npm ci)                      # the generator uses apps/web/node_modules/typescript
node apps/desktop/scripts/gen_i18n.mjs       # after any web i18n / missions / packs / desktop.json change
node apps/desktop/scripts/check_i18n.mjs     # CI: drift, duplicate keys, placeholder mismatch
node --test apps/desktop/tests/scripts/check_i18n.test.mjs apps/desktop/tests/scripts/screen_sources.test.mjs
```

Numbers follow the web's `format.ts` (ported with its test vectors): decimal comma and `.` grouping in PT-BR, never `-0,00`; typed numbers accept either decimal mark (`1,5` = 1.5).

## Retro look

- **Theme:** `resources/theme/tradelab.theme.xml` (the web's `app.css` tokens: window `#0B1010`, panel `#121C1B`, accent `#39FF88`, text `#E3F5EA`, attention `#FFC857`, info `#4FD8FF`; square corners, solid, short transitions) with the label classes `muted up down attention info accent title`; `tradelab-cb.theme.xml` is the colorblind palette (blue/orange for up/down, the web's `data-palette='cb'`). A unit test checks WCAG 2.2 AA (4.5:1) for every text colour on every background the theme declares.
- **Fonts** (bundled, embedded in the binary): IBM Plex Mono for text, tables and numbers, VT323 for titles only; both SIL Open Font License 1.1 (`resources/fonts/OFL-*.txt`; named in Settings). VT323 draws one font pixel per 0.08 em (its cap height is 7 pixels of 80/1000 em), so 12.5 px is one screen pixel per font pixel: titles use 25, 37.5 and 50 px.
- **CRT:** `resources/shaders/crt.frag` (a Raven effect, `raven_shader.h`) runs on a full-window overlay `Border` that reads what is behind it: scanlines, bloom, a fixed grain and a vignette; nothing depends on time, so it never flickers. Settings has the master toggle (the web's `settings.crt`) and one per effect; all are off when the system asks for reduced motion. Without a usable shader (null backend, a driver refusing it) the overlay stays collapsed. `crt.spv` is checked in, so builds need no shader compiler: after editing the shader run `scripts/build_shaders.sh <Raven checkout>` (glslangValidator); a `script` test fails when `crt.spv.sha256` no longer matches `crt.frag`.
- **Accessibility:** every control has a screen-reader name (mission Start buttons say which mission), tables are Raven `Table`s, the focus ring is always shown, and every shell screen is usable from the keyboard (Tab / Enter / arrows); text scale 100–200% in Settings (`Raccessibility_set_text_scale`).

## Test

```bash
ctest --preset macos-release           # unit, headless, cmake rules, scripts (not windowed)
ctest --test-dir build/macos-release -L windowed --output-on-failure   # needs a GPU (MoltenVK) and a display
```

| Label | What |
|---|---|
| `unit` | `tradelab_desktop_unit_tests` (doctest): json, i18n, format (web vectors), chart geometry, AppViewModel with a fake engine and with the real EngineHost, the shell's web ports (curriculum, campaign + report, journal, shortcuts, lesson runner, finding/objective texts, release rows, CRT toggles), every shell screen from real engine snapshots, theme contrast, EngineHost (50-step batch = 50 single steps by digest, sink only on the UI thread, FIFO under concurrent enqueue, coalescing), Vulkan message |
| `headless` | `tradelab_desktop_headless_tests`: Raven null platform + null backend; every view loads, every named widget/action exists and is bound, injected clicks/keys/text play mission 01 from the menu to the debrief; keyboard-only menu → briefing → terminal; every shown control of every shell screen has a screen-reader name; curriculum map → Academy lesson → stage passed; journal; settings (palette, CRT, text scale, a remapped shortcut) |
| `windowed` | `tradelab_desktop_windowed_tests`: the Vulkan renderer driven by `Rwindow_inject_*`; `Rwindow_capture` frames saved as PNG in `$TRADELAB_CAPTURE_DIR` (default `build/<preset>/captures`); m01 to the debrief and a 1024×768 terminal; a campaign mission from the menu through its briefing to its debrief and on to the next; curriculum map, an Academy lesson (chart pixels), journal; CRT on/off and the colorblind palette |
| `novulkan` | Linux: the app with no usable Vulkan driver must exit 3 with the message (PT-BR and en) |
| `cmake` | configure with `-ffast-math` fails naming the flag; a viewmodel file including a Raven header fails to build while the same file builds in the view layer |
| `script` | `check_raven_pin.py` and its tests; the checked-in `crt.spv` matches `crt.frag` and is SPIR-V |

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
