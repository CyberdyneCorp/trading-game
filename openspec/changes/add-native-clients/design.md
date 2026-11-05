# Design: add-native-clients

## Context

In progress: `apps/desktop` has its foundation (CMake + presets, pinned Raven, MVVM layers, engine worker, menu/briefing/minimal terminal/debrief, generated catalogs, headless and windowed tests; see `apps/desktop/README.md` and the ticked tasks); there is no `apps/ios` or `bindings/swift` yet. The engine is consumed today through (1) the C++20 static library `tradelab_engine` (CMake alias `tradelab::engine`, public headers in `engine/include/tradelab/`, e.g. `engine.hpp`), and (2) the stable C ABI `engine/include/tradelab/c_api.h` (implemented in `engine/src/c_api.cpp`, compiled into `tradelab_engine`): JSON-returning `tl_*` functions over an opaque `tl_run` handle, built into the WASM module (`engine/src/wasm_main.cpp`) and into the shared library target `tradelab_capi` (`libtradelab`) loaded by `bindings/python` for server replay.

The SvelteKit client (`apps/web`) is the reference for every screen, behavior and text. Its pure helpers (`apps/web/src/lib/game/*.ts`: sizing, stops, optionsMath, format, hotkeys, advisor, points, campaign, curriculum, journal, ...) and its storage model (IndexedDB `tradelab` v3: `runs`, `actions`, `meta`, `packs`, `checkpoints`; a checkpoint every 20 actions) are what the desktop client mirrors.

## Desktop

### Decision: Raven

The earlier draft chose SDL3 + Dear ImGui and rejected Raven as young and Vulkan-only. The product owner chose [Raven](https://github.com/oscar-soirey/Raven) (Apache-2.0): it gives retained widgets described in XML (with live reload), docking, a `Table` widget, custom-painted widgets, themes, fragment-shader effects, screen-reader support (NSAccessibility, AT-SPI), locales, and input-injection and frame-capture hooks for tests, all in a C11 library with no dependency besides `libx11-dev` on Linux. The costs (pre-1.0 API, Vulkan at run time, one maintainer) are handled below rather than avoided.

- **Pin:** commit `0fc738631c33844d6da732210267d21b5223f960`, never a branch or tag. Raven's README itself says the repository moves daily and the API changes.
- **How it is fetched:** CMake `FetchContent` with `GIT_REPOSITORY https://github.com/oscar-soirey/Raven.git` and `GIT_TAG <full sha>` (git checks the sha, so the content is what was reviewed). FetchContent is workable: Raven's CMake detects when it is not the top-level project and then builds no examples, tests or tools; we also set `RAVEN_BUILD_WEB=OFF` (it drives an installed Chromium browser), `RAVEN_BUILD_SHARED=OFF`, `RAVEN_WITH_SVG=ON`, `RAVEN_WITH_STB_IMAGE=ON`. Raven fetches the Vulkan headers itself at configure time (`cmake/RavenVulkanHeaders.cmake`, tag `v1.3.290`); `apps/desktop/cmake/Dependencies.cmake` declares `raven_vulkan_headers` first with that tag's commit sha, so the first declaration (ours, sha-pinned) wins. Offline and CI builds pass `FETCHCONTENT_SOURCE_DIR_RAVEN` / `FETCHCONTENT_SOURCE_DIR_RAVEN_VULKAN_HEADERS` (or reuse a cached `_deps`). A vendored copy is not used: it would add ~10 MB of churn per bump and hide the exact upstream commit. If a fix is needed before upstream merges it, it lives as a reviewed patch in `apps/desktop/third_party/raven-patches/` applied by `PATCH_COMMAND`, and is offered upstream.
- **Mirror:** a read-only mirror of the pinned commit under the organization is kept as a second `GIT_REPOSITORY` (selected by `TRADELAB_RAVEN_MIRROR=ON`) so a force-push or deleted repository upstream cannot break a release build.
- **Bumping:** one variable (`TRADELAB_RAVEN_SHA`) in `Dependencies.cmake`; `apps/desktop/scripts/check_raven_pin.py` fails CI if the sha in CMake, `apps/desktop/README.md` and this design disagree. A bump is its own PR that runs the full windowed suite and attaches captures.

### Platforms and GPU

- **macOS** arm64, latest two major versions, Cocoa. Universal (`arm64;x86_64`) only if cheap: CI builds it and runs the goldens on the x86_64 slice under Rosetta; it ships universal only when that passes and a universal MoltenVK is bundled, else arm64-only.
- **Linux** x86-64 glibc, X11 and Wayland (Raven picks Wayland when a compositor is available and X11 otherwise; Wayland libraries are opened at run time). AppImage built in the oldest supported baseline container.
- **Windows** out of scope for this change (Raven supports Win32; a later change adds MSVC CI, signing and an installer).
- **Vulkan at run time.** Raven links no Vulkan library; it opens `libvulkan.so.1` (Linux, package `libvulkan1` plus a driver such as `mesa-vulkan-drivers`) or `libvulkan.1.dylib` / `libMoltenVK.dylib` (macOS). The macOS app ships MoltenVK in `Contents/Frameworks/` (pinned release from the LunarG SDK, universal, hash recorded) and preloads it by absolute path before `Rinit`, so Raven's bare-name `dlopen("libMoltenVK.dylib")` resolves to the bundled copy without Homebrew; task 1.4 verifies this and falls back to a Raven patch adding the bundle's Frameworks folder to the search list.
- **No Vulkan driver.** When `Rinit` fails with `R_ERROR_BACKEND` (library missing, no device, instance creation failed), no Raven window can exist, so the app shows a native alert before exiting with code 3: `CFUserNotificationDisplayAlert` on macOS (C, no Objective-C needed); on Linux the first of `zenity --error`, `kdialog --error` found on `PATH`, and always stderr. Text (PT-BR, or en by system language): "O TradeLab precisa de um driver Vulkan para desenhar a janela e nenhum foi encontrado." followed by the platform hint (Linux: "Instale `libvulkan1` e o driver Vulkan da sua placa de vídeo (por exemplo `mesa-vulkan-drivers`) e abra o TradeLab de novo."; macOS: "A instalação parece incompleta: baixe o TradeLab de novo.") and Raven's error detail. The texts are desktop-only catalog keys (`desktop.vulkanMissing.*`).

### Architecture (MVVM in C++20)

```
apps/desktop/
  CMakeLists.txt, CMakePresets.json, cmake/Dependencies.cmake
  src/model/        engine adapter (EngineHost, worker thread), storage/ (SQLite repositories, action log, recorder), packs/ (library, catalog, downloads, http), updater, i18n catalog
  src/viewmodel/    plain C++ structs + commands per screen (menu, briefing, terminal panels, debrief, curriculum, journal, lessons, catalog, csv import, settings)
  src/view/         Raven glue: XML loading, widget binding, custom painters (chart, payoff, pattern sheet), CRT effect, input map
  src/platform/     system language, native alert, MoltenVK preload (user-data dir and single-instance lock: model/storage/paths)
  resources/        views/*.xml, theme/tradelab.theme.xml, shaders/crt.frag (+ .spv), i18n/strings.csv (generated), fonts/ (OFL), audio/
  tests/            viewmodel/, model/, headless/, windowed/, fixtures/
  scripts/          gen_i18n.mjs, check_i18n.mjs, check_raven_pin.py, package_macos.sh, package_appimage.sh
  third_party/      sqlite/ (amalgamation), miniaudio/, raven-patches/
```

- **Library split enforces the layers.** `tradelab_desktop_model` and `tradelab_desktop_viewmodel` are static libraries that do not link Raven, so including a Raven header there fails to compile; `tradelab_desktop_view` is the only target that links `raven::raven` (plus `src/platform`). The view layer reaches Raven through a thin C++ adapter (`view/raven/`: RAII `Rid` handles, `std::function` callbacks, XML load/find-by-name, `Rtask_post` wrapper) so API churn in a Raven bump is absorbed in one folder.
- **Views** are Raven XML documents, one per screen and per terminal panel, with widget names and action names that match viewmodel fields and commands (shared constants in `viewmodel/widget_names.hpp`). Every resource is embedded in the binary at configure time (`cmake/EmbedResources.cmake`) and the app calls `Rxml_set_auto_load(R_FALSE)` and `Rxml_set_live_reload(R_FALSE)`, so no stray `*.csv`/`*.xml` on `PATH` or in the current folder is picked up; a debug option that loads `resources/` from disk with live reload is a later addition.
- **Viewmodels** hold plain data (strings already formatted for the locale, enabled flags, visible rows) and expose commands; they read immutable engine snapshots and never call Raven or the engine directly. They are unit-tested without a window.
- **Web helpers** are ported to C++ in `viewmodel/` (or `model/` for storage-facing ones) one file per web file, keeping names, so a web change can be traced. Parity is checked with fixtures: a script runs the web helper over a fixed input grid (Node, through the web toolchain) and writes JSON under `apps/desktop/tests/fixtures/web/`; the C++ tests replay them; CI regenerates and fails on drift.

### Engine linkage

- `apps/desktop/CMakeLists.txt` does `add_subdirectory(../../engine engine)` with `TRADELAB_BUILD_TESTS=OFF` and links `tradelab::engine`: no WASM, no IPC, no shared library. The engine keeps its own `PRIVATE` determinism flags (`-ffp-contract=off -fno-fast-math -fno-rounding-math`); the desktop project SHALL NOT set global floating-point flags (`CMAKE_CXX_FLAGS`, `add_compile_options`) that would reach the engine, and a CMake check fails the configure if `-ffast-math`/`-Ofast` appear in the global flags.
- The adapter calls the engine's C ABI in-process (`tl_*` from `c_api.h`, statically linked from `tradelab::engine`): it is the exact surface the web and Python use, so the desktop reads the same state/debrief JSON shapes (IDL-generated enums in `idl_types.hpp`) and parity fixtures compare like for like. The C++ API (`engine.hpp`, `pack.hpp`, `ed25519.hpp`) is used where the C ABI has no equivalent (Ed25519 verification for updates). JSON is parsed with a small reader in `model/json` (no third-party parser: the C ABI emits plain, compact JSON); numbers keep their source text and stay decimal strings until formatted.
- Golden parity: the desktop test binary replays the shared golden fixtures (task 1.0) through the engine as linked into the desktop build, on every target.

### Threading

- **UI thread** = the thread that called `Rinit`; it owns every widget and runs `while (Rdraw_frame()) {}` (or `Rwait_frame` when idle).
- **Simulation worker** (`model/EngineHost`): one `std::thread` that owns every `tl_run*` and every engine call (the C ABI's blob buffer and returned strings are not shared across threads). Commands from viewmodels go into a mutex-guarded queue (place, cancel, move, advance n, plan, answer, checkpoint, restore). For each command the worker appends the action to SQLite in its own transaction, then applies it, and only then publishes it as accepted (commit-before-engine, as the web does).
- **Snapshots**: after each command (and after each step of a batch advance, coalesced to at most one per frame) the worker builds an immutable `std::shared_ptr<const Snapshot>` (parsed state, events since the last snapshot, progress `k/n`) and hands it to the UI thread with `Rtask_post`; the UI keeps the latest and viewmodels re-derive from it. The UI never waits on the worker; a batch advance keeps rendering and shows progress. Auto-advance timers live in the viewmodel and enqueue the same advance command as a manual advance.
- Raven's own job system (`Rtask_run_async`) is used only for UI-side work that does not touch the engine (decoding audio, hashing a downloaded file).

### Retro look

- **Theme:** `resources/theme/tradelab.theme.xml` in Raven's theme format (`docs/THEMES.md`) with the `terminal-ui` tokens (background `#0B1010`, panel `#121C1B`, accent `#39FF88`, text `#E3F5EA`, attention `#FFC857`, info `#4FD8FF`), square corners, no glass, Raven's springs disabled except short fades. A test parses the theme and checks WCAG 2.2 AA contrast for every text/background pair it declares.
- **Fonts:** the fonts the web names, bundled because the desktop cannot rely on them being installed: IBM Plex Mono (numbers, tables, body) and VT323 (titles only), both SIL OFL 1.1 with license files in `resources/fonts/` and named in Settings (the desktop has no separate About screen yet). Titles are drawn at integer multiples of VT323's pixel grid (one font pixel = 0.08 em, so 12.5 px; titles at 25, 37.5 and 50 px) so pixels stay square; the window scale (Retina, fractional Linux scales) is respected by Raven's pixel snapping.
- **CRT post-process:** `resources/shaders/crt.frag` written against Raven's effect interface (`raven_shader.h`, `raven_effect.glsl`), compiled to SPIR-V by `scripts/build_shaders.sh` (glslangValidator) and checked in, so a build needs no shader compiler; a test fails when the recorded hash of `crt.frag` no longer matches the source. It runs on a full-window overlay `Border` with `EffectBackdrop` (reads what is drawn behind it) and implements scanlines, bloom and noise as separately toggled parameters (plus the web overlay's vignette), no flicker: nothing depends on time, the grain is a hash of the pixel. Toggles live in Settings (the web's `settings.crt` master toggle plus three sub-toggles); everything is off when the OS reduced-motion setting is on (`Raccessibility_get_reduce_motion`, which also calls `Rset_animations_enabled(R_FALSE)`). On the null backend the overlay draws nothing.

### Chart

A custom-painted Raven widget (`Rcustom_set_paint_callback`, `raven_draw.h`) placed in the chart panel. The viewmodel computes geometry (price/time scales, candle bodies and wicks, volume, entry/stop/target/liquidation lines, option strikes, indicator panes, warming-up states, synthetic/branch marks, regime bands in the debrief) as plain structs, ported from the web's `chartScale.ts`, `chartPanes.ts`, `chartDrag.ts` and `chartA11y.ts`; the painter only issues `Rdraw_rect`/`Rdraw_line`/`Rdraw_text`, pixel-snapped. Line drags (stop/target) and per-candle keyboard focus go through the same viewmodel commands as the web. The accessible equivalent is a Raven `Table` of closed bars (paged, never a future bar) exposed to the screen reader. Budget: 5,000 bars at 60 fps on the reference machines, measured from `RFrameStats`.

### Localization

- `apps/desktop/scripts/gen_i18n.mjs` loads `apps/web/src/lib/game/i18n.svelte.ts` through the web's TypeScript toolchain (no regex parsing of TypeScript) (through the compiler API: data literals only, top-level `texts['id'] = …` assignments included) and writes `resources/i18n/strings.csv` in Raven's locale CSV format (`key,pt-BR,en`), with the mission texts of `missions.ts` as `mission.<id>.<field>`, merged with the desktop-only keys in `resources/i18n/desktop.json` (Vulkan message, updater, About; every key in both languages). `{name}` placeholders are kept and filled by the C++ formatter in `model/i18n`, not by Raven, so viewmodel tests format text without a window. The same script exports the pinned publisher keys from `apps/web/src/lib/game/packs.ts` to `resources/packs/pinned_keys.json`.
- `scripts/check_i18n.mjs` (CI) regenerates into a temp dir and fails when the committed CSV differs, when a desktop-only key duplicates a web key, or when a placeholder set differs between languages.
- PT-BR is the default, en second; the system language picks en only when it is English. Numbers follow the web's `format.ts` (decimal comma, `.` grouping in PT-BR, no `-0,00`) and input parsing accepts either decimal mark and, for credit amounts, the locale's thousands separator — ported with the web's test vectors.

### Storage

- SQLite amalgamation vendored in `third_party/sqlite/` (3.54.1; version and SHA3 recorded in `third_party/README.md`), compiled with `SQLITE_DQS=0`, `SQLITE_THREADSAFE=1`, `SQLITE_DEFAULT_FOREIGN_KEYS=1`, `SQLITE_OMIT_LOAD_EXTENSION`. One connection per thread (worker writes, UI reads), WAL, `synchronous=FULL` (an acknowledged command survives power loss), `PRAGMA user_version` migrations; a database from a newer build is refused, never rewritten.
- Schema mirrors the web DB, one `user_version` step per web DB version: v1 `runs` (header: mission, seed, engine version, execution policy, status, action count, digest, options, journal, questions, campaign/assisted links, equity), `actions` (append-only, `(run_id, seq)` primary key, the web's `Action` JSON so a log means the same on both clients and syncs unchanged later), `meta` (progress v2 with the campaign run and history as the web's JSON, `setting:<key>` rows for settings/keymap/layout/audio, device seed, cached catalog); v2 `packs` (digest, origin, size, summary) and `runs.pack_digest`; v3 `checkpoints` (latest per run, every 20 actions, tagged with the engine version that wrote it); v4 `downloads` (resumable pack downloads: url, size, bytes committed).
- Location: macOS `~/Library/Application Support/TradeLab/`, Linux `$XDG_DATA_HOME/tradelab/` (an absolute `XDG_DATA_HOME` only; default `~/.local/share/tradelab/`), `TRADELAB_DATA_DIR` overriding both; packs in `packs/<digest>.tlpack`. A lock file (`flock`) keeps a second instance from opening the same database: it shows "TradeLab is already open" and exits. A folder or database that cannot be opened leaves the session playable but unsaved.
- **Packs:** catalog (the web's static manifest format, schemaVersion 1, cached in `meta` for offline reading) and downloads over HTTP(S) through `model/packs/http` (libcurl opened at run time with `dlopen`: the system `/usr/lib/libcurl.4.dylib` on macOS, `libcurl.so.4` on Linux; when it is absent the catalog reads "unavailable" while play stays offline). 64 KB ranges appended (fsync) to a `.part` file with the offset then committed to SQLite (resumable; bytes past the committed offset are cut on resume), then size + SHA-256 against the catalog, then `tl_pack_validate` with the pinned keys (+ device key), then an atomic write (temp file, fsync, rename) into `packs/` followed by the index row. CSV import uses `tl_csv_validate`/`tl_pack_build_csv` with a per-device seed stored in `meta`. On start each indexed pack is checked against its file (missing or truncated = shown missing, never played). The engine's run-less calls share one buffer, so pack work runs on the simulation worker (`EngineHost::post_storage_job`).
- **Crash recovery (OFF02):** resume = restore the latest checkpoint (same engine version, restored digest equal to the recorded one) and replay the actions after it, else replay the whole log. Because an action is committed before the engine sees it and acknowledged only after that, a kill at any point leaves either the action committed (replayed once) or absent (never shown as accepted), and no fill is duplicated. A fault-injection hook (`TRADELAB_FAULT=<point>:<n>`, installed only by the test child process) kills the process with SIGKILL before the commit, inside the open transaction, after the commit and after the engine applied the action; the crash test restarts and compares the resumed state digest and fill count with the expected ones, then finishes the session and compares with an uninterrupted run.
- **Engine pinning:** the desktop ships one engine, statically linked. An unfinished run records the engine version and execution policy it started on; on another engine (after an update) it resumes by replaying its whole log on the current one (its checkpoint is ignored) and shows the web's `engine.pinMissing` notice; a different execution policy or a replay digest different from the recorded one is reported like the web does.

### Audio

miniaudio (vendored, public domain / MIT-0) plays the web's tracks (`apps/web/static/audio/*.mp3`, copied into the bundle) with the web's mood rules, hysteresis, crossfades, stings and persisted mute/volumes (`M`), ported from `audioRules.ts`.

### Sign-in and sync (deferred)

Blocked on Cyberdyne Auth (`add-cyberdyne-identity-and-backend`): the first release is guest-only and makes no network call besides the catalog, pack downloads and the update check. When Auth exists: loopback redirect on `127.0.0.1` with an ephemeral port, system browser opened with `open`/`xdg-open`, PKCE, refresh token in Keychain (macOS) or Secret Service via libsecret opened at run time (Linux), sync client over `model/http` uploading the existing action log. The storage schema already keys actions by `(run_id, seq)` so no migration is needed then.

### Packaging and updates

- **macOS:** `TradeLab.app` (CMake `MACOSX_BUNDLE`), MoltenVK and `MoltenVK_icd.json` in `Contents/Frameworks/` / `Contents/Resources/vulkan/icd.d/`, hardened runtime. Signing with the Developer ID and notarization are done by a human with the Apple Developer account (task 1.21); CI produces an unsigned bundle.
- **Linux:** AppImage (`appimagetool`, pinned), bundling everything except the Vulkan loader/driver, X11/Wayland and libcurl (taken from the host); desktop file and icons inside.
- **Updater:** the menu checks an HTTPS appcast (`{version, platform, url, size, sha256, notes}` + detached Ed25519 signature over its canonical bytes, release public key compiled in). Verification uses the engine's vendored Monocypher through `tradelab/ed25519.hpp` (no new crypto). The download goes to a staging folder, is checked for size, SHA-256 and signature, and is applied on the next restart (macOS: replace the `.app` by rename; Linux: replace the AppImage file by rename); any failed check deletes it and the current version keeps running. Updates are offered only in the menu, never mid-mission. An unfinished run resumes after an update by replaying its action log on the new engine; if the new engine refuses the log, the run shows the web's old-engine notice. The release signing key is held offline by a human.

### Testing

- **Viewmodel/model unit tests** (doctest, like the engine): every ported helper with the web's vectors and the parity fixtures, formatting and comma input, sizing, auto-advance, audio rules, storage (in-memory and on-disk SQLite), migrations, pack install/verify (corrupt, truncated, wrong key, wrong hash), updater verification (tampered bytes, wrong key, wrong size).
- **Golden parity:** the shared fixtures replayed inside the desktop build on Linux and macOS (and the x86_64 slice if universal).
- **Headless view tests:** `Rinit_ex` with `headless = R_TRUE` and `R_BACKEND_NULL` (Raven built with `RAVEN_PLATFORM=null`, `RAVEN_BACKEND_VULKAN=OFF` in the `linux-headless` preset): every XML view loads, every named widget and action exists and is bound, and scripted input (`Rwindow_inject_key`, `Rwindow_inject_mouse_*`, `Rwindow_inject_text`, `Rwindow_inject_mouse_wheel`) drives viewmodel state. Runs without a GPU on any CI machine.
- **Windowed scripted playthroughs** (the desktop counterpart of the web's Playwright suites): the real binary under Xvfb with Mesa lavapipe (`VK_ICD_FILENAMES` pointing at the lvp ICD) in an `ubuntu:24.04` container, and natively on macOS with MoltenVK. `Rset_animations_enabled(R_FALSE)` and `Rset_animation_time_step` make frames deterministic; scripts drive full missions to the debrief, keyboard-only play, the Vulkan-missing path, pack install, crash recovery and update refusal, and `Rwindow_capture` saves frames as CI artifacts and checks a small set of per-platform reference images with a tolerance. A Wayland smoke test runs under headless Weston.
- **Complexity:** cognitive complexity per C++ function at most 15, measured in CI with clang-tidy `readability-function-cognitive-complexity`.

### Risks

- **Pre-1.0 API churn** → pinned sha, Raven confined to `view/raven/`, bumps as dedicated PRs with the full windowed suite; patches carried in-tree and upstreamed.
- **Single maintainer / repository availability** → organization mirror of the pinned commit; Apache-2.0 allows a fork if upstream stops.
- **MoltenVK** (Vulkan-on-Metal translation gaps, effect shader differences) → CRT effect optional and off by default when it fails to compile; macOS windowed suite with captures; bundled pinned MoltenVK version.
- **Missing Vulkan on Linux desktops / VMs** → clear native message (above); lavapipe documented as a software fallback.
- **Wayland variance** (compositors, fractional scale) → X11 fallback through XWayland; Weston smoke test.
- **CI GPUs** → lavapipe on Linux; if macOS runners cannot create a Metal device, windowed macOS runs move to a self-hosted Apple Silicon machine and the hosted job keeps headless + goldens.

## iOS / iPadOS

- **Stack:** Swift 6, SwiftUI, Swift C++ interop (`-cxx-interoperability-mode=default`) against the engine C++ API; Swift Package wrapping the engine as an XCFramework built by CMake. Alternative if interop proves brittle: the stable C ABI (`tradelab/c_api.h`) through a module map, the surface the WASM and Python bindings already use.
- **Chart:** SwiftUI `Canvas` (or Metal if 5,000-bar budget fails), same visual rules.
- **Architecture:** MVVM with `@Observable` view models; engine actor (`actor EngineHost`) owning the run state.
- **Storage:** GRDB (SQLite) for runs/progress; `URLSession` background configuration for packs.
- **Auth:** `ASWebAuthenticationSession` + PKCE; Keychain `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`.
- **Golden tests:** XCTest target replays the engine's golden scripted runs (to be extracted from `engine/tests` / `bindings/python/tests` into shared fixtures) and compares digests on simulator and on a physical device in CI (device farm).

## Risks (iOS)
- Swift C++ interop edge cases with templates → expose a narrow, non-templated facade header for Swift (or fall back to the C ABI).
- App Store review on "trading" category → clear "educational simulation, no real money" metadata.
