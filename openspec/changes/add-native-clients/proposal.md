# Proposal: add-native-clients

## Why

Desktop (C++20) and iPad/iPhone (SwiftUI) clients bring full offline play and native ergonomics. Both must reproduce the web's results exactly (same engine, same goldens) and the shared `terminal-ui` contract, while using native windowing, input, storage and sign-in. Android follows later with the same contracts.

## What Changes

Nothing below exists yet; this change is planned for Phases 3–4.

- `apps/desktop`: C++20, Dear ImGui (docking) + SDL3, linking the engine's static library (`tradelab::engine`) directly; GPU rendering with a CRT post-process shader; offline packs, SQLite storage, loopback OIDC sign-in, sync. macOS, Windows, Linux.
- `apps/ios`: Swift + SwiftUI, engine via Swift C++ interop (XCFramework built by CMake); iPad terminal layout (keyboard/trackpad), iPhone tabbed layout; offline packs, SQLite storage, `ASWebAuthenticationSession`, Keychain, background-safe persistence.
- Swift binding (`bindings/swift/`) added to the golden-vector CI matrix, which today covers native C++, WASM and the Python binding (`.github/workflows/engine.yml`).

## Capabilities

### New Capabilities
- `desktop-client`: desktop-specific behavior (platforms, rendering, input, storage, packaging, updates).
- `ios-client`: iPadOS/iOS-specific behavior (layouts, interop, lifecycle, storage, input).

### Modified Capabilities
None. (Both implement `terminal-ui`, `offline-sync` and `player-identity` from earlier changes.)

## Non-goals

- Android (separate later change: Kotlin/Compose + JNI with the same contracts).
- Steam or Mac App Store distribution in the first desktop release (direct signed download first).
- Apple Watch / visionOS.

## Impact

- New: `apps/desktop/`, `apps/ios/`, `bindings/swift/`, and shared golden-vector fixtures extracted from the engine tests (golden digests currently live inline in `engine/tests` and `bindings/python/tests`).
- CI: macOS runners for Xcode builds, Windows runner for MSVC/Clang-cl, golden matrix + Swift.
- Requires Apple developer account, Windows code-signing certificate, macOS notarization.
