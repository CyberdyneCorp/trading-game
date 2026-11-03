# Tasks: add-native-clients

## 1. Desktop (Phase 3)
- [ ] 1.0 Extract the engine's golden scripted runs and digests (inline in `engine/tests` and `bindings/python/tests` today) into shared fixtures
- [ ] 1.1 CMake + vcpkg app target; SDL3 + ImGui docking + SDL_GPU backend; CI builds on macOS/Windows/Linux
- [ ] 1.2 Retro theme, font atlas, CRT post-process shader with toggles
- [ ] 1.3 Simulation worker thread + snapshot double buffering
- [ ] 1.4 Terminal panels (chart, rail, ticket, chain, fills, journal, cards, news), layout persistence/reset
- [ ] 1.5 SQLite storage, pack install/verify, crash-recovery test (OFF02)
- [ ] 1.6 Loopback OIDC sign-in + credential store + sync client
- [ ] 1.7 Goldens inside desktop build on all three OSes
- [ ] 1.8 Signing, notarization, updater with signature verification

## 2. iPad / iPhone (Phase 4)
- [ ] 2.1 Engine XCFramework + Swift facade (`bindings/swift`); golden XCTest (simulator + device)
- [ ] 2.2 iPad terminal layout, keyboard shortcuts, pointer hover
- [ ] 2.3 iPhone tab layout, bottom-sheet ticket, full-screen chart
- [ ] 2.4 GRDB storage, background-safe checkpoints, background pack downloads
- [ ] 2.5 ASWebAuthenticationSession sign-in, Keychain, sync
- [ ] 2.6 Accessibility pass (VoiceOver chart table, Dynamic Type, Reduce Motion)
- [ ] 2.7 Airplane-mode E2E (OFF01); TestFlight
