# Design: add-native-clients

## Context

Not started: there is no `apps/desktop`, `apps/ios` or `bindings/swift` yet. The engine is consumed today through (1) the C++20 static library `tradelab_engine` (CMake alias `tradelab::engine`, public headers in `engine/include/tradelab/`, e.g. `engine.hpp`), and (2) the stable C ABI `engine/include/tradelab/c_api.h` (implemented in `engine/src/c_api.cpp`): JSON-returning `tl_*` functions over an opaque `tl_run` handle, built into the WASM module (`engine/src/wasm_main.cpp`) and into the shared library target `tradelab_capi` (`libtradelab`) loaded by `bindings/python` for server replay.

## Desktop

- **Stack:** C++20, SDL3 (window, input, GPU via `SDL_GPU` → Metal/Vulkan/D3D12), Dear ImGui (docking branch) with the SDL3 + SDL_GPU backends, ImPlot evaluated for the chart only if a custom ImDrawList renderer misses the retro look (default: custom renderer sharing the candle geometry logic with the web renderer spec, not code).
- **Why not Raven:** the reference video's library (C + Vulkan) is young and Vulkan-only (MoltenVK on macOS). We take the look (dense panels, crisp pixels, CRT shader), not the dependency. Revisit if it matures.
- **Architecture (MVVM in C++):** `model/` (engine adapter, repositories), `viewmodel/` (plain structs + commands, unit-testable without a window), `view/` (ImGui draw functions only).
- **Engine linkage:** link `tradelab::engine` from `engine/CMakeLists.txt` (add_subdirectory) and call the C++ API directly; no WASM, no IPC. The engine's determinism flags apply to the engine target, not the app.
- **Threading:** one simulation worker thread with a command queue; UI reads immutable snapshots (double-buffered).
- **Storage:** SQLite (WAL) via a thin wrapper; packs under the platform user-data dir (SDL_GetPrefPath).
- **Auth:** loopback HTTP listener (cpp-httplib or minimal custom), system browser via `SDL_OpenURL`; credential store wrapper (Keychain / Credential Manager / libsecret). HTTPS via libcurl.
- **Packaging:** macOS .app notarized + Sparkle-style signed updates; Windows MSIX or signed installer; Linux AppImage. Dependencies via vcpkg manifest, pinned.
- **Fonts:** licensed monospace + pixel font atlas baked at integer scales.

## iOS / iPadOS

- **Stack:** Swift 6, SwiftUI, Swift C++ interop (`-cxx-interoperability-mode=default`) against the engine C++ API; Swift Package wrapping the engine as an XCFramework built by CMake. Alternative if interop proves brittle: the stable C ABI (`tradelab/c_api.h`) through a module map, the surface the WASM and Python bindings already use.
- **Chart:** SwiftUI `Canvas` (or Metal if 5,000-bar budget fails), same visual rules.
- **Architecture:** MVVM with `@Observable` view models; engine actor (`actor EngineHost`) owning the run state.
- **Storage:** GRDB (SQLite) for runs/progress; `URLSession` background configuration for packs.
- **Auth:** `ASWebAuthenticationSession` + PKCE; Keychain `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`.
- **Golden tests:** XCTest target replays the engine's golden scripted runs (to be extracted from `engine/tests` / `bindings/python/tests` into shared fixtures) and compares digests on simulator and on a physical device in CI (device farm).

## Risks
- Swift C++ interop edge cases with templates → expose a narrow, non-templated facade header for Swift (or fall back to the C ABI).
- SDL_GPU maturity on Linux drivers → fallback to the OpenGL backend of ImGui.
- App Store review on "trading" category → clear "educational simulation, no real money" metadata.
