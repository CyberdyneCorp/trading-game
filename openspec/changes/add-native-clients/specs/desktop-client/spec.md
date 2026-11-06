# Spec Delta

## Purpose

Defines the native C++20 desktop client built on Raven: supported platforms and GPU requirement, engine linkage and threading, MVVM structure, rendering and effects, input, localization, local storage, packaging and updates, implementing the shared terminal UX with full offline play. Sign-in and sync are added when Cyberdyne Auth exists.

## ADDED Requirements

### Requirement: Supported platforms
The desktop client SHALL run on macOS (arm64, latest two major versions) and on Linux (x86-64, glibc-based, under both X11 and Wayland), and SHALL produce the same golden digests as every other target. A macOS universal build SHALL ship only if its x86_64 slice passes the golden suite. Windows is not a supported platform of this change.

#### Scenario: Golden parity
- **WHEN** the golden suite runs inside the desktop build on Linux and on macOS
- **THEN** every digest SHALL equal the web and server digests

#### Scenario: Wayland and X11
- **WHEN** the client starts in a Linux session with a Wayland compositor, and again in an X11-only session
- **THEN** it SHALL open its window natively in each and play a mission to the debrief

### Requirement: Pinned GUI library
The client SHALL build against Raven at commit `0fc738631c33844d6da732210267d21b5223f960`, fetched by CMake `FetchContent` by full commit sha (never a branch or a tag), with Raven's examples, tests, tools and web module disabled. Raven headers SHALL be included only by the view layer's adapter. A build check SHALL fail when the pinned sha differs between the build files and the documentation.

#### Scenario: Pin drift
- **WHEN** the Raven sha in the CMake files is changed without updating the desktop README and design
- **THEN** the pin check SHALL fail in CI

#### Scenario: Layer boundary
- **WHEN** a model or viewmodel source file includes a Raven header
- **THEN** the build SHALL fail

### Requirement: Vulkan runtime requirement
The client SHALL draw through Vulkan loaded at run time: the system Vulkan loader and driver on Linux, and MoltenVK bundled inside the app on macOS (no Homebrew or SDK needed). When no usable Vulkan driver is found, the client SHALL NOT crash or exit silently: it SHALL show a native alert in the user's language (PT-BR by default) saying a Vulkan driver is required and none was found, with the platform's fix (Linux: install `libvulkan1` and the graphics card's Vulkan driver; macOS: download TradeLab again), print the same text and the library's error detail to stderr, and exit with status 3.

#### Scenario: Linux without a Vulkan loader
- **WHEN** the client starts on a Linux system without `libvulkan.so.1`
- **THEN** a native error alert (or stderr when no dialog tool exists) SHALL say that TradeLab needs a Vulkan driver and how to install one, and the process SHALL exit with status 3

#### Scenario: macOS without Homebrew
- **WHEN** the signed app runs on a Mac without MoltenVK installed system-wide
- **THEN** it SHALL draw using the MoltenVK bundled in the app

### Requirement: Direct engine linkage
The desktop client SHALL link the engine's static library (`tradelab::engine`) into its executable (no WASM, no IPC, no shared library) and SHALL NOT add global compiler flags that change the engine's floating-point determinism flags; the configure step SHALL fail if such flags are present.

#### Scenario: Fast-math refused
- **WHEN** the desktop project is configured with `-ffast-math` in its global C++ flags
- **THEN** the configure step SHALL fail with a message naming the flag

### Requirement: Simulation off the UI thread
All engine calls SHALL run on one simulation worker thread fed by a command queue. The worker SHALL hand immutable state snapshots to the UI thread (through Raven's UI-thread task queue), and the UI thread SHALL never block on a step.

#### Scenario: Smooth UI during batch advance
- **WHEN** the player advances a 50-step batch
- **THEN** the UI SHALL keep rendering and show progress until the batch completes, and the final snapshot SHALL equal the state after 50 single advances

### Requirement: MVVM structure
The client SHALL separate `model/` (engine adapter, repositories), `viewmodel/` (plain C++ state and commands) and `view/` (Raven XML layouts and thin glue). Viewmodels SHALL be testable without a window or GPU, and every XML view SHALL load and bind its named widgets and actions on Raven's headless null platform.

#### Scenario: Headless view check
- **WHEN** the headless test loads every view with the null platform and backend
- **THEN** each named widget and action SHALL exist and be bound to its viewmodel, with no Vulkan driver present

### Requirement: Terminal UX parity
The desktop client SHALL implement every `terminal-ui` requirement with the web client as the reference for screens, behavior and texts, in dockable and resizable panels whose layout is persisted per installation, with a reset-to-default layout command.

#### Scenario: Layout reset
- **WHEN** the player selects "Reset layout"
- **THEN** panels SHALL return to the default terminal arrangement

#### Scenario: Liquidation visible at minimum size
- **WHEN** the window is 1024×768 with an open perpetual position
- **THEN** the liquidation price and distance SHALL be visible in the rail without scrolling

#### Scenario: Same run, same numbers
- **WHEN** a mission is played on desktop and on the web with the same seed and the same actions
- **THEN** the account line after each step and every number of the debrief SHALL be the same

#### Scenario: No text cut at minimum size
- **WHEN** the window is 1024×768 (or larger) on any screen, including the option chain
- **THEN** no text SHALL be cut by its panel or its control: it wraps, or the layout gives it room

### Requirement: Rendering and effects
The desktop client SHALL use a Raven theme file carrying the `terminal-ui` color tokens, the monospace font for numbers and tables and the pixel font for titles only (both bundled with their licenses), drawn at integer multiples of the pixel font's design size. The chart SHALL be a custom-painted widget following the web chart's rules. The CRT look (scanlines, bloom, noise; never flicker) SHALL be a shader post-process with a master toggle and one toggle per effect, all off when the OS requests reduced motion or the user turns them off. Text SHALL stay crisp on high-DPI displays.

#### Scenario: High-DPI
- **WHEN** the window moves to a 2× display
- **THEN** text and candles SHALL stay crisp without blurring

#### Scenario: Reduced motion
- **WHEN** the OS reduced-motion setting is on
- **THEN** the CRT effects and animated transitions SHALL be disabled

### Requirement: Keyboard and mouse input
All actions SHALL be reachable by keyboard and mouse, with remappable shortcuts whose defaults and meanings are those of the web client. Advance and submit SHALL never share a binding.

#### Scenario: Same shortcut meaning
- **WHEN** a player presses the advance-one-step shortcut on desktop
- **THEN** it SHALL perform the same action as on the web client

### Requirement: Localization from the web catalogs
The desktop text catalogs SHALL be generated by a script from the web client's catalogs (plus a desktop-only catalog for texts the web does not have), never retyped. PT-BR SHALL be the default and en the second language. Numbers SHALL be formatted like the web (decimal comma in PT-BR) and typed numbers SHALL be accepted with either decimal mark. A CI check SHALL fail when the generated catalogs are out of date with the web catalogs or when a key's placeholders differ between languages.

#### Scenario: Catalog drift
- **WHEN** a web catalog string changes and the desktop catalog is not regenerated
- **THEN** the i18n check SHALL fail in CI

#### Scenario: Comma input
- **WHEN** a PT-BR player types the stop `95,26` in the ticket
- **THEN** the order SHALL be sized with a stop of 95.26

### Requirement: Local storage and offline
The desktop client SHALL store runs, actions, checkpoints, progress, settings and installed packs locally (SQLite with WAL in the platform user-data directory, verified pack files beside it), SHALL persist each command before showing it as accepted, and SHALL satisfy every `offline-sync` requirement that does not need an account.

#### Scenario: Killed during write (OFF02)
- **WHEN** the process is killed while persisting a command
- **THEN** on restart the run SHALL resume from the last committed command with no duplicated fill

#### Scenario: Reopened mid-mission
- **WHEN** the app is closed or killed in the middle of a free-play mission and opened again
- **THEN** the menu SHALL offer that run (mission and seed) and resuming it SHALL restore it at its last committed command

#### Scenario: Airplane mode (OFF01)
- **WHEN** the machine has no network and a pack is installed
- **THEN** a full mission including the debrief SHALL complete

### Requirement: Pack installation through the engine
Downloaded and imported packs SHALL be installed only after size and SHA-256 checks and the engine's signature and content validation against the pinned publisher keys (and the device key for private packs), with an atomic move into the packs directory. Downloads SHALL resume from the stored offset.

#### Scenario: Tampered pack
- **WHEN** a downloaded pack's bytes do not match its signed manifest
- **THEN** it SHALL NOT be installed and the catalog SHALL show the engine's reason

### Requirement: Packaging and safe updates
macOS builds SHALL be a `.app` bundle containing MoltenVK, code-signed with a Developer ID and notarized; Linux builds SHALL be an AppImage. Updates SHALL be offered only from the menu, SHALL verify size, SHA-256 and an Ed25519 signature against a release key compiled into the app before install, and SHALL NOT break in-progress runs.

#### Scenario: Tampered update
- **WHEN** an update package fails signature verification
- **THEN** it SHALL NOT be installed and the current version SHALL keep running

#### Scenario: Run survives an update
- **WHEN** the app updates while a mission is unfinished
- **THEN** reopening SHALL resume that run at its last committed command

### Requirement: Guest-only first release
Until Cyberdyne Auth exists, the desktop client SHALL run as a guest with no sign-in prompt and no network calls other than the pack catalog, pack downloads and the update check, each of which SHALL fail gracefully offline.

#### Scenario: No account needed
- **WHEN** a new user opens the client offline
- **THEN** they SHALL reach the mission menu and play without any sign-in step

### Requirement: Sign-in and sync once Cyberdyne Auth exists
When Cyberdyne Auth is available, the desktop client SHALL sign in through the system browser with a loopback redirect on `127.0.0.1` and an ephemeral port, SHALL store refresh tokens only in the OS credential store (Keychain on macOS, Secret Service on Linux), and SHALL sync runs per `offline-sync`; guest runs SHALL stay playable and be offered for upload after sign-in.

#### Scenario: Browser returns to app
- **WHEN** the user completes sign-in in the browser
- **THEN** the loopback listener SHALL receive the code, the browser tab SHALL show a "return to TradeLab" page and the listener SHALL close
