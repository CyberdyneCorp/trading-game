# Proposal: add-native-clients

## Why

Desktop (C++20) and iPad/iPhone (SwiftUI) clients bring full offline play and native ergonomics. Both must reproduce the web's results exactly (same engine, same goldens) and the shared `terminal-ui` contract, while using native windowing, input and storage. Android follows later with the same contracts.

## What Changes

Nothing below exists yet; this change is planned for Phases 3–4.

- `apps/desktop`: C++20 on [Raven](https://github.com/oscar-soirey/Raven) (C11 GUI library, Apache-2.0), pinned at commit `0fc738631c33844d6da732210267d21b5223f960` and fetched by CMake `FetchContent`. Raven opens its own windows (Cocoa on macOS, X11 and Wayland on Linux) and draws with Vulkan loaded at run time (MoltenVK on macOS, shipped inside the app). The app links the engine's static library (`tradelab::engine`) directly, follows MVVM (`model/`, `viewmodel/`, `view/` with Raven XML layouts), runs the simulation on a worker thread, and gets the retro look from a Raven theme file and a CRT effect shader. Texts are generated from the web catalogs (PT-BR first, en second). Offline packs and SQLite storage in the platform user-data directory; macOS `.app` and Linux AppImage with Ed25519-verified updates. Targets: macOS (arm64; universal only if the x86_64 slice passes the goldens) and Linux (x86-64, X11 and Wayland). Windows is out of scope for this change.
- First desktop release is guest-only and fully offline: sign-in and sync wait for Cyberdyne Auth (`add-cyberdyne-identity-and-backend`); their tasks stay open.
- `apps/ios`: Swift + SwiftUI, engine via Swift C++ interop (XCFramework built by CMake); iPad terminal layout (keyboard/trackpad), iPhone tabbed layout; offline packs, SQLite storage, `ASWebAuthenticationSession`, Keychain, background-safe persistence.
- Swift binding (`bindings/swift/`) added to the golden-vector CI matrix, which today covers native C++, WASM and the Python binding (`.github/workflows/engine.yml`).

## Capabilities

### New Capabilities
- `desktop-client`: desktop-specific behavior (platforms, GPU requirement, engine linkage and threading, rendering and effects, input, localization, storage, packaging, updates).
- `ios-client`: iPadOS/iOS-specific behavior (layouts, interop, lifecycle, storage, input).

### Modified Capabilities
None. (Both implement `terminal-ui`, `offline-sync` and `player-identity` from earlier changes; the desktop client implements `player-identity` and sync only once Cyberdyne Auth exists.)

## Non-goals

- Android (separate later change: Kotlin/Compose + JNI with the same contracts).
- Windows desktop builds (Raven supports Win32, but packaging, signing and CI for Windows are a later change).
- Desktop sign-in and sync in the first desktop release (blocked on Cyberdyne Auth).
- Steam or Mac App Store distribution in the first desktop release (direct signed download first).
- Apple Watch / visionOS.

## Impact

- New: `apps/desktop/`, `apps/ios/`, `bindings/swift/`, and shared golden-vector fixtures extracted from the engine tests (golden digests currently live inline in `engine/tests` and `bindings/python/tests`).
- Desktop third-party code: Raven (FetchContent, pinned commit), SQLite amalgamation (vendored), miniaudio (vendored), the OFL fonts the web names (IBM Plex Mono, VT323), MoltenVK (bundled in the macOS app). No engine change: no golden digest moves.
- CI: Linux job in `ubuntu:24.04` (headless null-platform tests, windowed tests under Xvfb with Mesa lavapipe), macOS arm64 job (MoltenVK), golden matrix + desktop build + Swift; macOS runners for Xcode builds.
- Requires an Apple Developer ID (signing and notarization of the macOS app) and the release signing key for updates — both held by a human, not CI.
