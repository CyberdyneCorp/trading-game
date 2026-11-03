# Proposal: add-native-clients

## Why

Desktop (C++20) and iPad/iPhone (SwiftUI) clients bring full offline play and native ergonomics. Both must reproduce the web's results exactly (same engine, same goldens) and the shared `terminal-ui` contract, while using native windowing, input, storage and sign-in. Android follows later with the same contracts.

## What Changes

- `apps/desktop`: C++20, Dear ImGui (docking) + SDL3, linking the engine directly; GPU rendering with a CRT post-process shader; offline packs, SQLite storage, loopback OIDC sign-in, sync. macOS, Windows, Linux.
- `apps/ios`: Swift + SwiftUI, engine via Swift C++ interop; iPad terminal layout (keyboard/trackpad), iPhone tabbed layout; offline packs, SQLite storage, `ASWebAuthenticationSession`, Keychain, background-safe persistence.
- Swift binding added to the golden-vector CI matrix.

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

- New: `apps/desktop/`, `apps/ios/`, `bindings/swift/`.
- CI: macOS runners for Xcode builds, Windows runner for MSVC/Clang-cl, golden matrix + Swift.
- Requires Apple developer account, Windows code-signing certificate, macOS notarization.
