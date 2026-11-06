# TradeLab desktop (Linux + macOS)

C++20 client on [Raven](https://github.com/oscar-soirey/Raven) (C11 GUI library, Apache-2.0) that links the engine (`tradelab::engine`) directly. The web client (`apps/web`) is the reference for every screen, behaviour and text. Plan and requirements: `openspec/changes/add-native-clients` (`desktop-client` spec, design "Desktop", tasks section 1).

**Status.** In PT-BR (default) and en, with the TradeLab theme, bundled fonts and a CRT post-process:

- **Shell screens:** menu (resume line for a run left from the terminal, or left unfinished by an earlier launch — closed or killed mid-mission: free play, a campaign mission, a pack run or an assisted attempt —, update offer, campaign panel, the 12 missions with their Completed mark, the catalog and CSV-import buttons), the mission's tutorial before its first briefing (Tutorial.svelte; replayable from the briefing), briefing (rules, permissions, limits, margin tiers, mark-price note, costs, campaign locks), the full debrief (score dimensions, findings, next lesson, lesson results with each question's wording, the answer given and the key, per-trade R table with the stop/liquidation advice, P&L split by Greek, spread/slippage, safe leverage, regime and release reveals, campaign outcome and report, decisions, advisor reads, the run's chart with each trade's entry and exit marked and its candle table, timeline), assisted practice in the terminal and the debrief (rewind to an earlier decision or the same mission on another market: a new attempt flagged ASSISTED, without the advisor, never counted for progress, points or the campaign), campaign runs, curriculum map, journal, the Academy lesson runner, the pack catalog (installed / partial / not downloaded, resumable downloads verified then installed, the installed packs to practise or remove), the CSV import wizard (declared data, the engine's report, the gap-fill choice) and settings (language, shortcuts, CRT effects, text scale, colorblind palette).
- **Trading terminal** (the web game screen), in docked panels (move, resize, persisted, Reset layout): chart (candles, volume, SMA20/EMA50/VWAP/BB20, RSI/MACD/ADX with warm-up, no-trading bars, fills and news marks, branch-market marks, the regimes revealed when the run ends, order/stop/target/liquidation lines with drag to move, zoom, crosshair, hover readout, accessible bar table and per-candle keyboard focus), perpetual ticket (market/limit/stop, leverage, risk %/quantity/notional/% equity sizing, stop/target/trailing incl. percent, margin and liquidation estimate with tiers and liquidity shocks, every web warning, the stop-beyond-liquidation confirmation, comma input), orders, rail, option chain and ticket, advisor with points, intel perks, news and event feed, plan/calibration/lesson/help/journal/tutorial dialogs, auto-advance and the web's remappable shortcuts (one key map for the terminal, its button hints and Settings).
- **Sound:** the web's soundtrack (menu, game and tense themes with crossfades, win/loss stings) on vendored miniaudio, with mute (key and button) and volumes in Settings.
- **Storage:** every run is an action log in SQLite, committed before the engine applies it (terminal commands included), checkpointed, crash-safe and resumable from the menu of the next launch; progress (campaign, lessons, tutorials seen, advisor points, journal) and settings (language, display, key map, sound, panel layout) live in the same database; packs install through the engine (CSV import, catalog downloads).
- **Distribution:** the engine's shared golden fixtures replay in the desktop build on both platforms; `TradeLab.app` (MoltenVK bundled) and a Linux AppImage with launch smoke tests; a signed-manifest updater that installs only after the player's click.

Not yet (open tasks): the tutorial cards' art and the pattern sheet animation; the merged client's first Linux CI run (Weston/Wayland smoke, lavapipe against the macOS reference images, TSan). Guest-only and offline: sign-in and sync wait for Cyberdyne Auth. **Windows is out of scope** for this change.

## Requirements

| | Linux (x86-64 or arm64, glibc ≥ Ubuntu 24.04) | macOS 14+ (arm64; universal build) |
|---|---|---|
| Build | CMake ≥ 3.28, Ninja, Clang 18 (`CC=clang CXX=clang++`: the engine's `-Wpedantic -Werror` build rejects `__int128` under GCC, as in `engine.yml`), git, `libx11-dev` | Xcode command-line tools, CMake ≥ 3.28, Ninja |
| Run | `libvulkan1` and a Vulkan driver (`mesa-vulkan-drivers`; lavapipe is a software fallback); X11 or Wayland; optional `libcurl4` (pack catalog, updates); optional PulseAudio, ALSA or JACK (sound) | `TradeLab.app` carries its MoltenVK; a build-folder binary needs `brew install molten-vk`; the system libcurl |
| Tests | windowed tests: `xvfb`; Wayland smoke: `weston`, `libwayland-client0`, `libxkbcommon0`; packaging: `curl`, `file`, `desktop-file-utils` | — |

Raven, the Vulkan headers and doctest are fetched at configure time (network once; then the build folder's `_deps` is reused). Raven loads the Vulkan library at run time; nothing Vulkan is linked.

## Build and run

```bash
cd apps/desktop
cmake --preset macos-release      # or linux-release, macos-universal
cmake --build build/macos-release -j 8
./build/macos-release/tradelab    # --seed N: fixed market (like the web's ?seed=)
```

Presets: `macos-release` (Cocoa + Vulkan via MoltenVK, arm64), `macos-universal` (arm64 + x86_64, the shipped app), `linux-release` (X11 + Wayland, Vulkan), `linux-headless` (Raven `RAVEN_PLATFORM=null`, `RAVEN_BACKEND_VULKAN=OFF`: builds and tests on machines without X11 or a GPU; the app binary there only prints the Vulkan message).

Offline / cached builds: `-DFETCHCONTENT_SOURCE_DIR_RAVEN=<clone at the pinned sha>`, `-DFETCHCONTENT_SOURCE_DIR_RAVEN_VULKAN_HEADERS=<Vulkan-Headers checkout>`, `-DFETCHCONTENT_SOURCE_DIR_DOCTEST=<doctest 2.4.11>`. `-DTRADELAB_RAVEN_MIRROR=ON` fetches Raven from the organization mirror (`TRADELAB_RAVEN_MIRROR_URL`, default `CyberdyneCorp/raven-mirror`; the mirror repository has to be created by an org admin before it is used).

The configure step fails if global C/C++ flags contain `-ffast-math`, `-Ofast`, `-funsafe-math-optimizations` or `-ffp-contract=fast`: they would reach the engine and break its determinism. The app's own code builds with `-ffp-contract=off`, so the ports of the web helpers compute what JavaScript computes (Clang's default fused multiply-add on arm64 once turned a web "no liquidation" into 6e-15; the web parity fixtures caught it). The engine keeps its own flags (`add_subdirectory`, tests off) and its golden digests are unchanged by this app; the desktop golden test (below) proves it in this build.

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
                  command queue, coalesced snapshots, assisted rewind), json reader, i18n catalog + {name}
                  formatter, embedded resources; game_state (typed tl_state), game_command (every terminal
                  command), game_prefs (terminal preferences + the prefs.json reader of the one-time import),
                  progress (shell progress/settings repositories) + progress_json (the web's JSON shapes),
                  content (web rules as data)
  storage/        SQLite layer, schema + migrations, RunStore, logged actions (web Action JSON, incl. every
                  terminal command; rewind prefix), RunRecorder (commit, checkpoint, resume), progress v2,
                  ProfileStore (progress, settings and preferences on the UI thread's connection), user-data
                  dir + instance lock, fault hook (crash test)
  packs/          PackLibrary (verify + install, CSV import), catalog, resumable downloads, libcurl client
                  (dlopen), PackService (the screens' jobs on the worker, answers as PackEvents)
  update/         signed manifest (Ed25519), Updater (check, download, schedule, apply), UpdateService (thread)
third_party/      sqlite/ (amalgamation 3.54.1), miniaudio/ (0.11.22); versions and hashes in third_party/README.md
src/viewmodel/    tradelab_desktop_viewmodel: AppViewModel (screen flow, commands, texts per named widget),
                  format (port of the web's format.ts), chart_geometry, vulkan_message, widget_names;
                  shell.cpp (run life as progress sees it, campaign, lessons, settings), updates.cpp (offer),
                  assisted.cpp (tutorial, assisted practice, debrief chart), packs.cpp + pack_screens (catalog,
                  installed packs, CSV wizard; ports of catalog.ts / packs.ts helpers);
                  screens/ (one ScreenModel per shell screen: texts, screen-reader names, lines, tables, flags,
                  progress values);
                  ports of the web helpers (curriculum, campaign + campaignReport, journal, keymap, report_texts,
                  the Academy lesson runner)
  game/           the terminal: GameViewModel (command pipeline, dialogs, keys, auto-advance), chart_vm (display
                  list + pointer/keyboard), ticket_vm, options_vm, panels_vm, dialogs_vm, progress (points, perks),
                  dock_layout (panels, default arrangement, persisted layout), ports of sizing/stops/optionsMath/
                  shortcuts/hotkeys/chartScale/chartDrag/chartA11y/rejections
  audio/          ports of audioRules.ts (moods, danger hysteresis, stings) and the AudioDirector (playlists,
                  crossfades, stings, mute, volumes) behind a player interface; Soundtrack (per-frame glue)
src/view/         tradelab_desktop_view (the only target linking Raven): App wiring, AppView (XML screens in a
                  Switcher, actions, keyboard hook), chart painter; raven/ adapter (Rtask_post, actions, find,
                  text); screens/ (ShellView, RetroLook: theme, fonts, text scale, CRT overlay); game/ (GameView:
                  glue for views/game.xml, scene painter, widget helpers)
src/platform/     system language, native error alert, bundled MoltenVK preload, executable path, audio_out
                  (the soundtrack's player on miniaudio)
src/main.cpp      tradelab executable (--seed N, --smoke-test REPORT)
resources/        views/*.xml (one per screen), i18n/strings.csv (generated), i18n/desktop.json (desktop-only
                  texts), packs/pinned_keys.json and web/content.json (generated), theme/*.theme.xml,
                  fonts/ (OFL), shaders/crt.frag + crt.spv
packaging/        pins.sh (MoltenVK, appimagetool, runtime: URL + SHA-256), package_macos.sh, package_appimage.sh,
                  smoke_macos.sh, smoke_appimage.sh, smoke_wayland.sh (headless Weston), macos/ (Info.plist.in,
                  entitlements), linux/ (desktop file)
scripts/          gen_i18n.mjs, check_i18n.mjs, i18n_lib.mjs, i18n_game.mjs, screen_sources.mjs,
                  gen_web_fixtures.mjs + web_fixtures_lib.mjs (web parity fixtures), check_raven_pin.py,
                  build_shaders.sh, check_complexity.sh
tests/            unit (model, viewmodel, storage, packs, update), golden, headless, windowed, cmake rules,
                  scripts; fixtures/web (web parity), windowed/reference (reference images); support/ (driver,
                  terminal flows, fakes, PNG writer/reader and reference compare, UI queue, loopback HTTP server)
```

`tradelab_desktop_model` and `tradelab_desktop_viewmodel` do not link Raven, so a Raven include there does not compile (checked by a test). Threading: the UI thread (the one that called `Rinit`) owns every widget; `EngineHost` owns the engine on one worker thread; commands go through a FIFO queue and each result comes back as an immutable `Snapshot` posted with `Rtask_post`, coalesced to at most one hand-off per frame (acknowledgements are merged, never lost). The viewmodel sends one command at a time and never blocks.

The terminal sends one engine command at a time: an entry first opens the plan dialog, and the plan and its order (or a leverage change and its order) run as one sequence; advances pressed meanwhile are queued, and an advance never passes a due calibration question. Rejections quote the web's numbers (margin needed vs available). Each recorded plan also goes to the run's journal (Journal screen). Every run starts with the intel perks the player has unlocked; a campaign mission adds its capital and leverage gate (one `RunOptions`, stored with the run).

The terminal's shortcut map, tutorials seen, the advisor point balance (settled once per run), the finished missions that unlock intel perks, sound (mute, music and effects volumes) and the panel layout are saved in SQLite with the rest of the progress (Storage below; `setting:keymap`, `setting:audio`, `setting:layout`); Settings edits the same key map and sound settings. Without a saved-data folder they last for the session. Every button that has a shortcut names the player's key (Avançar [Space], Padrões [P], the tooltips of the sound, help, journal, options, buy and sell buttons), remapped or not, and a focused control keeps the keys the web's `keepsKey` gives it (a text field all, a combo box all but Space, a slider its arrows, a checkbox or button none).

**Docked panels** (`viewmodel/game/dock_layout`, Raven `DockArea` in `views/game.xml`): chart, account (rail), ticket, options and orders, advisor/news/events. A tab moves its panel (side by side, above, or behind another tab), a border resizes; panels cannot be closed or torn out. Raven splits by ratio, so the default arrangement is computed for the area's size and keeps the web's column widths (rail 290 px, ticket 340 px, feed 300 px, ticket row 300 px) at any window size. The player's own arrangement (Raven's layout text) is stored in SQLite (`setting:layout`) on every change and loaded at the next launch; **Restaurar painéis / Reset layout** (chart toolbar) forgets it and the default follows the window again. A stored text Raven cannot read falls back to the default.

**Sound** (`viewmodel/audio`, `platform/audio_out`): the web's `audioRules.ts` and `AudioDirector` ported: the menu theme outside the terminal, the game theme while trading, the tense theme within 5% of liquidation (calm again only past 8%), on thin option margin or for 3 bars after a margin call or liquidation; 1.2 s crossfades; a win or loss sting when a trade closes (a resumed run's history never plays). The web's MP3s (`apps/web/static/audio`) play through vendored miniaudio (Core Audio; PulseAudio, ALSA or JACK opened at run time); they are looked up in `$TRADELAB_AUDIO_DIR`, then `TradeLab.app/Contents/Resources/audio`, the AppImage's `usr/share/tradelab/audio`, then the source tree (build-folder runs). No device or no files: silent, the game plays on. `TRADELAB_AUDIO=off` disables it, `TRADELAB_AUDIO=null` uses miniaudio's null device (tests: decoded and mixed, not heard). The mute key (M by default), the ♪ button and Settings → Som change one setting.

Views are embedded in the binary (`cmake/EmbedResources.cmake`); widget and action names are shared constants (`src/viewmodel/widget_names.hpp`, `screen_names.hpp`, `game/game_widgets.hpp`) checked against the XML by the headless test.

## Storage and packs

Everything saved lives in one folder: macOS `~/Library/Application Support/TradeLab/`, Linux `$XDG_DATA_HOME/tradelab/` (default `~/.local/share/tradelab/`); `TRADELAB_DATA_DIR` overrides both. Inside: `tradelab.db` (SQLite, WAL, `synchronous=FULL`), `packs/<digest>.tlpack` and `updates/` (staged releases). A `flock` on `tradelab.lock` keeps a second instance out ("TradeLab is already open"); a folder that cannot be opened leaves the session playable but unsaved.

- **Schema** (`src/model/storage/schema.cpp`, `PRAGMA user_version`, one step per web DB version): v1 `runs`, `actions`, `meta`; v2 `packs` + `runs.pack_digest`; v3 `checkpoints`; v4 `downloads`; v5 `runs.engine_abi` (the engine C ABI a run was recorded under, `tl_abi_version`; NULL for older rows, which count as ABI 1). Released steps are never edited; a database from a newer build is refused, not rewritten.
- **Action log, commit-before-engine** (`EngineHost` + `storage::RunRecorder`): every command that changes a run is a `storage::Action`, the web's `Action` JSON (`{"k":"order",...}`, `{"k":"plan",...}`, `{"k":"adv","n":5}`, ...); terminal commands are converted by `storage::actions::from_game`. It is committed in its own transaction, then applied with `apply_action` (live and on replay, so client ids and digests match), and acknowledged only after the run header (digest, status, equity) and, every 20 actions, a checkpoint are written. A failed write never stops play: the snapshot's `durable` turns false.
- **Resume** (`EngineCommands::resume`): the active run is rebuilt from its latest checkpoint (written by this engine version, restored digest checked) plus the tail of the log, else from the whole log. Notices in `Snapshot::resume_notice`: `engine_changed` (the run started on another engine version, e.g. before an update: full replay on this one, the web's `engine.pinMissing`), `policy_changed`, `digest_mismatch`, `pack_missing` (`pack.missing`), `abi_changed` (recorded under another engine C ABI: nothing is replayed, the run is stored as left so later launches do not offer it, and the menu shows the web's `engine.abiMismatch`), `damaged`.
- **Progress and settings** (`storage::ProfileStore`, its own connection on the UI thread: WAL lets it work beside the worker's): `meta` `progress` (the web's Progress v2: tutorials seen, advisor points and settled runs, perks introduced, Academy lessons, the campaign run and its history as the web's CampaignRun JSON, stages assessed), `setting:display` (language, CRT effects, colorblind palette, text scale) `setting:keymap` (the terminal and Settings share it), `setting:audio` (mute, music and effects volumes) and `setting:layout` (the terminal's docked panels, Raven's layout text). The `runs` table is the shell's run list: the shell writes the journal (web PlanEntry[]) and the campaign link (`{"id","index"}`) of a run, and marks a run it leaves without starting another (ending a campaign) as left; digest, status and equity are the worker's. Completed missions are the runs finished outside packs and assisted practice. A `prefs.json` written by an earlier build is merged once (its sound and panel layout included; its finished missions kept in `meta` `legacyFinished`) and renamed `prefs.json.imported`. A failed read gives defaults; a failed write keeps the values for the session.
- **Resume from the menu**: the next launch offers the active run when it has actions and is still in play: free play, a pack run (titled by the pack's label), an assisted attempt (ASSISTED tag), or a campaign mission while its campaign still waits on it (the campaign's Continue resumes it too). Starting another run instead leaves it; a left campaign mission carries its last equity into the campaign, as on the web.
- **Assisted practice** (`EngineHost::rewind`, web `assisted.ts`): the worker keeps the run's logged actions; a rewind starts a new stored run on the same market and start options without the advisor (`advice_allowance` 0; for a pack, the same pack), replays the log's prefix up to the chosen decision (`storage::rewind_prefix`: lesson answers kept) and stores the attempt's `assisted` link (`{"from","kind":"rewind","step"}`; another market: `{"from","kind":"seed"}` on a new seed). The original attempt keeps its record and log. Assisted attempts never settle advisor points, finish a mission or move the campaign, and they are not offered inside a campaign mission in progress.
- **Packs** (`src/model/packs/`, used on the worker through `EngineHost::post_storage_job` because the engine's run-less calls share a buffer): install = size limit, `tl_pack_validate` against the pinned publisher keys (`resources/packs/pinned_keys.json`, generated from the web's `packs.ts`) plus this device's key, SHA-256 equal to the engine's digest, atomic write (temp, fsync, rename), index row. CSV import = `tl_csv_validate` / `tl_pack_build_csv` signed with the device seed kept in `meta`, then the same install. `PackLibrary::missing()` is the start-up file check. A schema-2 pack's own mission texts come back with the install (`PackCheck::texts`) and with every run on it (`Snapshot::pack_texts`), so the briefing and the terminal name the run with the pack's title and briefing (`AppViewModel::run_text`). With a mark series the ticket, chart drags, calibration question and option chain use the last close (`GameState::last_price`), not the account mark. `tests/viewmodel/test_pack_runs.cpp` plays pack runs through the shell on real snapshots: the web's `mark-v2.tlpack` fixture (texts, tiers, mark note, order estimate) and the catalog's liquidity-shock pack to the debrief (ticket warning on bars 15–20 and 171–180).
- **Catalog and downloads**: the web's static manifest (`apps/web/static/catalog/catalog.json` format, cached for offline reading), read from `TRADELAB_CATALOG_URL` (CMake cache value; default the web's static catalog in this repository, `raw.githubusercontent.com/CyberdyneCorp/trading-game/main/apps/web/static/catalog/catalog.json`; `$TRADELAB_CATALOG_URL` overrides it at run time). 64 KB `Range` requests appended (fsync) to `packs/<digest>.part`, the offset committed to SQLite after each chunk, so a dropped connection, a pause or a quit resumes from the committed offset; then size + SHA-256 against the catalog and the install above. The catalog screen (`packs::PackService`) runs these jobs on the simulation worker and shows each committed chunk; leaving the screen pauses a running download (a run started meanwhile would otherwise wait for it). No answer from the server = offline, an HTTP error or a bad manifest = unavailable (with a retry); either way the last catalog seen stays listed with downloads disabled. HTTP goes through libcurl opened at run time (`/usr/lib/libcurl.4.dylib` on macOS, `libcurl.so.4` on Linux): when it is missing the catalog is unavailable and everything else works offline.
- **CSV import screen**: the file is read from the path typed (4 MB at most, refused before reading), the timestamp unit and interval are guessed from its first rows (packs.ts `guessMeta`), the declared data go to `tl_csv_validate`, whose report (errors with their lines, gaps) is shown as the web does; with gaps the player chooses the gap fill (join, or no-trading bars that indicators skip or carry). The pack is built, signed with the device key and installed like any pack, then practised from the catalog screen's installed packs.

## Packages

```bash
packaging/package_macos.sh build/macos-universal dist     # dist/TradeLab.app + dist/TradeLab-macos.zip
packaging/smoke_macos.sh dist/TradeLab.app                # launched like Finder does (LaunchServices `open`)
packaging/package_appimage.sh build/linux-release dist    # dist/TradeLab-<arch>.AppImage (in ubuntu:24.04)
xvfb-run -a packaging/smoke_appimage.sh dist/TradeLab-x86_64.AppImage
```

- **Audio:** both packages carry the web's soundtrack (`Contents/Resources/audio`, `usr/share/tradelab/audio`).
- **macOS `.app`:** `Contents/MacOS/tradelab`, `Contents/Frameworks/libMoltenVK.dylib` (MoltenVK v1.4.2 universal, downloaded by URL and refused unless its SHA-256 matches `packaging/pins.sh`; install name `@rpath/libMoltenVK.dylib`), `Contents/Resources/vulkan/icd.d/MoltenVK_icd.json`, `TradeLab.icns` (from the web's icon), `Info.plist` (version from `CMakeLists.txt`, macOS 14+). At start the app loads the bundled MoltenVK by absolute path (`platform::preload_bundled_vulkan`), so Raven's `dlopen("libMoltenVK.dylib")` resolves to that image (dyld matches the leaf name of a loaded image) and no Homebrew MoltenVK is needed. The smoke test opens the app through LaunchServices (no terminal, cwd `/`), and checks that the menu showed with every named widget and that Vulkan came from the bundle. Signing: ad-hoc by default; with `CODESIGN_IDENTITY` (a Developer ID) everything is signed with the hardened runtime and `packaging/macos/entitlements.plist` (no exceptions). Developer ID signing and notarization are human tasks (tasks 1.21): an ad-hoc app downloaded from the internet is stopped by Gatekeeper.
- **Universal:** `macos-universal` builds arm64 + x86_64. CI runs the goldens on the x86_64 slice under Rosetta and ships the universal app only when they pass. Outcome so far: both slices build; arm64 goldens pass locally; the x86_64 run happens in CI (the development Mac has no Rosetta).
- **Linux AppImage:** built in the `ubuntu:24.04` baseline (glibc 2.39) with the pinned `appimagetool` 1.9.1 and type 2 runtime 20251108 (passed with `--runtime-file`; both SHA-256-checked), `tradelab.desktop` and the icon. Nothing is bundled: `libX11`, `libvulkan.so.1` and the GPU driver come from the system like any Vulkan application. The smoke test runs it (extract-and-run, no FUSE needed) under Xvfb + lavapipe.

## Updates

The release pipeline publishes `desktop-manifest.json` and `desktop-manifest.json.sig` (Ed25519 over the exact bytes, 128 hex digits) next to the packages:

```json
{"format":1,"product":"tradelab-desktop","releases":[
  {"version":"0.2.0","platform":"linux-x86_64","url":"https://…/TradeLab-x86_64.AppImage",
   "size":3365384,"sha256":"<64 hex>","notes":{"pt-BR":"…","en":"…"}}]}
```

`platform` is `macos` (a zip of `TradeLab.app`), `linux-x86_64` or `linux-aarch64` (the AppImage). An installed release build (inside `TradeLab.app`, or an AppImage: `$APPIMAGE`) checks once at start, off the UI thread (`UpdateService`). The manifest is trusted only when the signature verifies with the engine's `ed25519_verify` against the release public key compiled in (`-DTRADELAB_UPDATE_PUBLIC_KEY=<64 hex>`; empty, the default, turns updates off; `-DTRADELAB_UPDATE_MANIFEST_URL` or `$TRADELAB_UPDATE_MANIFEST_URL` points elsewhere). URLs must be https (plain http only to the loopback interface, for tests). A newer release for this platform is offered **in the menu only**; nothing is downloaded until the player clicks "Baixar e instalar ao reabrir". Then the payload is fetched to `updates/`, checked for size and SHA-256, and scheduled (`updates/pending.json`). At the next start, before anything else, the staged file is checked again and moved over the installed copy by rename (an AppImage file, or the `.app` bundle unpacked with `ditto` and swapped), then the new executable is started. Tampered, foreign-key, wrong-size, wrong-hash or truncated inputs are refused and the installed version keeps running; a refused pending install is dropped, not retried at every start. A run left unfinished resumes on the new engine through its action log (the `engine_changed` notice above). Custody of the release key and the signing of manifests are human tasks (tasks 1.21).

## Texts (PT-BR first, en second)

Never retyped: `scripts/gen_i18n.mjs` reads `apps/web/src/lib/game/i18n.svelte.ts` (`pt`, `en`), `missions.ts` (mission texts → `mission.<id>.<field>`), the terminal texts kept outside i18n (`scripts/i18n_game.mjs`: `news.ts` → `news.headline.*`, `news.kind.*`, `news.level.*`; `candlePatterns.ts` → `pattern.*`; `objectives.ts` → `objective.*`; `tutorials.ts` → `tutorial.*`; `lessons.ts` → `lessonq.*`), the shell screens' sources (`scripts/screen_sources.mjs`: `analysis.ts` → `finding.<code>[.safe|.one]`, `finding.liquidatedOnly`, `nextLesson.<code>`; `academy.ts` → `academyLesson.<id>.…`) and `packs.ts` (pinned publisher keys) with the web's own TypeScript compiler API, adds `resources/i18n/desktop.json`, and writes `resources/i18n/strings.csv` (Raven's locale CSV, `key,pt-BR,en`), `resources/packs/pinned_keys.json` and `resources/web/content.json` (the screen rules as data: curriculum `STAGES`, campaign constants, `REPORT_MIN_TRADES`, `AFTER_RELEASE_BARS`, `SHORTCUT_ACTIONS` / `DEFAULT_KEYMAP`, academy `LESSONS`). Each key comes from one source; a key produced twice is a generator error. `check_i18n.mjs` fails when any output drifts.

```bash
(cd apps/web && npm ci)                      # the generator uses apps/web/node_modules/typescript
node apps/desktop/scripts/gen_i18n.mjs       # after any web i18n / missions / packs / desktop.json change
node apps/desktop/scripts/check_i18n.mjs     # CI: drift, duplicate keys, placeholder mismatch
node --test apps/desktop/tests/scripts/*.test.mjs
```

Numbers follow the web's `format.ts` (ported with its test vectors): decimal comma and `.` grouping in PT-BR, never `-0,00`; typed numbers accept either decimal mark (`1,5` = 1.5).

### Web parity fixtures

`scripts/gen_web_fixtures.mjs` loads the web's own helpers through the web's Vite config (Svelte runes included) and runs them over fixed input grids (`scripts/web_fixtures_lib.mjs`): `sizing`, `stops`, `optionsMath`, `format` (both locales), `hotkeys`, `advisor` (both locales), `points`, `campaign` (a scripted campaign, step by step), `curriculum` and `audioRules`. The results go to `tests/fixtures/web/<helper>.json`, one case per line (`{"fn":…,"args":[…],"out":…}`); `tests/viewmodel/test_web_parity.cpp` replays every case through the C++ port (numbers with a 1e-9 relative tolerance, texts exactly). After a web helper changes:

```bash
node apps/desktop/scripts/gen_web_fixtures.mjs           # rewrite the fixtures, then make the ports pass
node apps/desktop/scripts/gen_web_fixtures.mjs --check   # CI: fails when the web now returns something else
```

## Retro look

- **Theme:** `resources/theme/tradelab.theme.xml` (the web's `app.css` tokens: window `#0B1010`, panel `#121C1B`, accent `#39FF88`, text `#E3F5EA`, attention `#FFC857`, info `#4FD8FF`; square corners, solid, short transitions) with the label classes `muted up down attention info accent title`; `tradelab-cb.theme.xml` is the colorblind palette (blue/orange for up/down, the web's `data-palette='cb'`). A unit test checks WCAG 2.2 AA (4.5:1) for every text colour on every background the theme declares.
- **Fonts** (bundled, embedded in the binary): IBM Plex Mono for text, tables and numbers, VT323 for titles only; both SIL Open Font License 1.1 (`resources/fonts/OFL-*.txt`; named in Settings). VT323 draws one font pixel per 0.08 em, so 12.5 px is one screen pixel per font pixel: titles use 25, 37.5 and 50 px. The monospace text font is wider than Raven's default: the terminal ticket keeps one field per row so it fits its 340 px column (a headless test checks that no control sticks out of its panel).
- **CRT:** `resources/shaders/crt.frag` (a Raven effect, `raven_shader.h`) runs on a full-window overlay `Border` that reads what is behind it: scanlines, bloom, a fixed grain and a vignette; nothing depends on time, so it never flickers. Settings has the master toggle (the web's `settings.crt`) and one per effect; all are off when the system asks for reduced motion. Without a usable shader (null backend, a driver refusing it) the overlay stays collapsed. `crt.spv` is checked in, so builds need no shader compiler: after editing the shader run `scripts/build_shaders.sh <Raven checkout>` (glslangValidator); a `script` test fails when `crt.spv.sha256` no longer matches `crt.frag`.
- **Accessibility:** every control has a screen-reader name (mission Start buttons say which mission), tables are Raven `Table`s, the focus ring is always shown, and every shell screen is usable from the keyboard (Tab / Enter / arrows); text scale 100–200% in Settings (`Raccessibility_set_text_scale`).

## Test

```bash
ctest --preset macos-release           # unit, golden, headless, cmake rules, scripts (not windowed)
ctest --test-dir build/macos-release -L windowed --output-on-failure   # needs a GPU (MoltenVK) and a display
```

Every test case starts with an empty data folder (`tests/main.cpp` points `TRADELAB_DATA_DIR` at a fresh per-process folder), so a remapped key or a settled run never leaks into another test.

| Label | What |
|---|---|
| `unit` | `tradelab_desktop_unit_tests` (doctest): json, i18n, format (web vectors), chart geometry, Vulkan message; the web parity fixtures (every case of `tests/fixtures/web`); sound (director with a fake player: playlists, crossfades, mute, volumes, stings; danger hysteresis 5%/8%; a resumed history plays no sting; prefs); docked layout (default widths, stored layout, reset); chart regime bands and branch marks; the terminal viewmodels (`tests/viewmodel/game`: sizing/stops/shortcuts/chart/options vectors from the web tests, ticket, chart, panels, GameViewModel with a fake engine: pipeline, plan, notices, dialogs, keys, drags, protection, options, advisor, points, perks, lessons, auto-advance); the shell (web ports, every shell screen from real engine snapshots, Settings remap reaching the terminal, terminal plans reaching the journal, theme contrast); AppViewModel and EngineHost (50-step batch = 50 single steps by digest, sink only on the UI thread, FIFO under concurrent enqueue, coalescing); storage (migrations, newer schema refused, log/checkpoints/progress/settings/packs, paths, lock contention, resume from checkpoint + tail, foreign or damaged checkpoint = full replay, engine/policy/digest notices, terminal commands and start options logged and replayed); **OFF02**: `tradelab_desktop_crash_child` is SIGKILLed at `before-commit`, `mid-commit`, `after-commit` and `after-apply`, the restart resumes with the expected digest and fill count; packs (pinned-key install, tampered/truncated/foreign-key refused, CSV import, catalog, downloads resumed after a restart, libcurl against a loopback server; PackService: catalog ready / offline from the cache / unavailable, a download paused after a chunk and resumed from the committed offset by the next launch, then verified and installed, CSV validation and import); progress and settings in SQLite (`test_profile_store.cpp`: progress, campaign and history read back by a new connection, the runs table as the shell's run list with journal, campaign link and left status written by the shell, completed = finished outside packs and assisted practice, settings and the shared key map, prefs (sound and panel layout included) and progress sharing one row, the one-time prefs.json import); assisted practice (`test_assisted.cpp`: the web's rewind-prefix vectors, a rewind stored as a new assisted run whose digest equals a fresh run given the same prefix, the original left, refusals, links stored and kept by a resume); the shell flows with a fake engine (`test_flows.cpp`, `test_pack_screens.cpp`: tutorial before the first briefing, rewind / another market and their exclusion from progress and points, saved campaign / pack / assisted runs offered and resumed, the debrief chart's entry/exit marks, candle table pages and lesson results wording, every catalog state and button, the CSV wizard from file to pack, the catalog.ts / packs.ts helper vectors); updates (signature, tampering, wrong key, versions, https only, wrong size/hash/truncated refused, nothing without the click, apply by rename, the macOS bundle swap, the menu offer) |
| `golden` | `tradelab_desktop_golden_tests`: every shared fixture in `engine/tests/golden` (task 1.6) replayed through the desktop's action adapter to its frozen `digest`, `eventsDigest`, status and rejections, reveals by SHA-256; each mission run is also recorded to SQLite and resumed from it to the same digest. Its own binary so each slice of the universal build can run it (`arch -x86_64`) |
| `headless` | `tradelab_desktop_headless_tests`: Raven null platform + null backend; every view loads, every named widget/action exists and is bound, injected clicks/keys/text play mission 01 from the menu to the debrief; terminal flows (`tests/support/game_flows.hpp`): place + drag the stop + cancel a limit, stop dragged past liquidation (confirmed) then liquidation, option buy; shortcuts, remapping, chart focus and the bar table; no ticket control outside its panel; keyboard-only menu → briefing → terminal; screen-reader names on every shell screen; curriculum → Academy lesson; journal; settings (sound off reaches the terminal); docked panels (default widths at 1024×768, a moved layout restored at the next launch, Reset layout); the web's eight tracks on miniaudio's null device; the shell flows of `tests/support/shell_flows.hpp` (`test_headless_flows.cpp`): tutorial, assisted practice, debrief chart and lesson results, CSV import, catalog download cut and resumed, a campaign mission and a pack run resumed by the next launch |
| `windowed` | `tradelab_desktop_windowed_tests`: the Vulkan renderer driven by `Rwindow_inject_*`; `Rwindow_capture` frames saved as PNG in `$TRADELAB_CAPTURE_DIR` (default `build/<preset>/captures`); m01 to the debrief, a 1024×768 terminal, the terminal flows above, a campaign mission from the menu to its debrief and on, curriculum, an Academy lesson (chart pixels), journal, CRT on/off and the colorblind palette; the shell flows (`test_windowed_flows.cpp`, captures `flow-*.png`): the tutorial before the first briefing (keys, skip, replay), assisted rewind from the terminal and another market from the debrief, the debrief chart (candle pixels), candle table and lesson results, the CSV import with a gap and its practice run, the catalog download cut after its first chunk, resumed, installed and played from a loopback server, settings / progress / a campaign mission and a pack run resumed by a second launch; the chart with **5,000 bars** measured with `RFrameStats` (the display list and layout+paint under a 60 fps frame; with render time too on a GPU, reported on lavapipe: `captures/chart-5000-bars.txt`); English chosen **mid-run** through the menu (resume, digest unchanged and equal to a run without the switch, quantities typed with each locale's decimal mark); **OFF01** (the update host refuses connections: the check fails quietly, m01 plays to its saved debrief); **reference images** (below). Timing-sensitive: run them on an idle machine |
| `playtest` (also `windowed`) | `tradelab_desktop_playtest_tests`: the twelve missions played to the debrief (PT-BR and en, 1280×800 and 1024×768, disciplined and reckless: stop and target, liquidation, trailing stop in percent, options bought and written, straddle, hedge, limit order, early close, waiting), a campaign through three missions, an Academy lesson, a remapped advance key with the CRT off. Each screen is checked for text cut by its panel or control; captures and a text report per run (account line after each change, the terminal's and the debrief's texts) go to `$TRADELAB_CAPTURE_DIR/playtest`. See Playtest below |
| `novulkan` | Linux: the app with no usable Vulkan driver must exit 3 with the message (PT-BR and en) |
| `cmake` | configure with `-ffast-math` fails naming the flag; a viewmodel file including a Raven header fails to build while the same file builds in the view layer |
| `script` | `check_raven_pin.py` and its tests; the checked-in `crt.spv` matches `crt.frag` and is SPIR-V |

**Reference images** (`tests/support/reference.*`, `tests/windowed/reference/*.png`): the menu, the m01 briefing and the m01 terminal after ten bars (seed 1, CRT off) are captured, reduced to 320×200 by box averaging (so a 1x hosted runner and a 2x Retina Mac compare alike) and compared with the reference: a pixel differs when a channel is off by more than 40, and at most 2% may differ. One set serves Linux (lavapipe) and macOS (MoltenVK). After an intended change of those screens: run the windowed tests with `TRADELAB_UPDATE_REFERENCES=1`, look at the new files and commit them; every run also leaves the reduced captures in `$TRADELAB_CAPTURE_DIR/reference`.

Linux in Docker (what CI does; `--init` because `xvfb-run` must not be PID 1):

```bash
docker run --rm --init -v "$PWD":/src -w /src ubuntu:24.04 bash -c '
  apt-get update && apt-get install -y build-essential clang cmake ninja-build git python3 libx11-dev zlib1g-dev \
    libvulkan1 mesa-vulkan-drivers xvfb xauth curl ca-certificates file desktop-file-utils weston libwayland-client0 libxkbcommon0
  export CC=clang CXX=clang++
  cmake -S apps/desktop -B /tmp/b -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b -j 8
  ctest --test-dir /tmp/b -LE "windowed|novulkan" --output-on-failure
  export VK_ICD_FILENAMES=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json)
  xvfb-run -a -s "-screen 0 1920x1080x24" ctest --test-dir /tmp/b -L windowed --output-on-failure
  apps/desktop/packaging/smoke_wayland.sh /tmp/b/tradelab
  apps/desktop/packaging/package_appimage.sh /tmp/b /tmp/dist
  xvfb-run -a apps/desktop/packaging/smoke_appimage.sh /tmp/dist/TradeLab-$(uname -m).AppImage'
```

Cognitive complexity (≤ 15 per function, clang-tidy `readability-function-cognitive-complexity`, sources and tests): `scripts/check_complexity.sh build/<preset>` (`CLANG_TIDY=` picks the binary).

CI: `.github/workflows/desktop.yml`. Linux (ubuntu:24.04 container): build, i18n, pin and web-parity-fixture checks, unit + golden + headless + cmake + script tests, windowed tests under Xvfb with Mesa lavapipe (captures uploaded), the Wayland smoke test on headless Weston (`packaging/smoke_wayland.sh`: no X11 display, Raven told to use Wayland), the AppImage and its smoke test (uploaded as an artifact), the no-Vulkan test with `libvulkan1` removed, clang-tidy complexity; `linux-headless` preset build + tests; **TSan** (on the runner itself, `vm.mmap_rnd_bits=28`): the unit tests built with `-fsanitize=thread`, running the EngineHost and storage host cases. macOS: universal build, unit + golden + headless tests, the goldens on the x86_64 slice under Rosetta, windowed tests (blocking: they pass on the hosted runners' Metal device), `TradeLab.app` (uploaded, unsigned) and its Finder-launch smoke test (best effort).

ThreadSanitizer locally: configure a separate build with `-fsanitize=thread` in `CMAKE_C_FLAGS`, `CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS`, build `tradelab_desktop_unit_tests` and run it with `-tc="engine host*,storage host*"`.

### Playtest

`tradelab_desktop_playtest_tests` plays the app like a player and leaves what a reviewer reads: `playtest/<run>-*.png` and `playtest/<run>.txt` under `$TRADELAB_CAPTURE_DIR`. The engine is shared, so the numbers must match the web's for the same mission, seed and actions: open the web with `?seed=<seed>`, play the same orders (the report names them: side, quantity, preset chips, the limit price it typed) and compare the account line after each step and the debrief. The last comparison (all twelve missions) found the same numbers everywhere; the hover readout of the web's debrief chart is the only text the desktop does not show (its debrief chart is read only). A text cut by its panel or control fails the run (`tests/support/text_dump.hpp`: a label drawn past the visible part of its panels, a label narrower than its text given one line, a button narrower than its text). Under AddressSanitizer: configure a separate build with `-fsanitize=address,undefined` in `CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS` (sanitizers do not touch the engine's floating-point rules) and run the unit, golden, headless, windowed and playtest binaries.
