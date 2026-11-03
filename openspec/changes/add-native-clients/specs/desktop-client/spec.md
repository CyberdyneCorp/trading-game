# Spec Delta

## Purpose

Defines the native C++20 desktop client: supported platforms, engine linkage, rendering and input, local storage, sign-in and distribution, implementing the shared terminal UX with full offline play.

## ADDED Requirements

### Requirement: Supported platforms
The desktop client SHALL run on macOS (arm64, latest two major versions), Windows 10/11 (x86-64) and Linux (x86-64, glibc-based, Wayland and X11) and SHALL produce the same golden digests as every other target.

#### Scenario: Golden parity
- **WHEN** the golden suite runs inside the desktop build on each platform
- **THEN** every digest SHALL equal the web and server digests

### Requirement: Direct engine linkage
The desktop client SHALL link the engine library directly (no WASM, no IPC) and SHALL run simulation work off the UI thread so rendering never blocks on a step.

#### Scenario: Smooth UI during batch advance
- **WHEN** the player advances a 50-step batch
- **THEN** the UI SHALL keep rendering and show progress until the batch completes

### Requirement: Terminal UX parity
The desktop client SHALL implement every `terminal-ui` requirement, with dockable and resizable panels whose layout is persisted per user, and a reset-to-default layout command.

#### Scenario: Layout reset
- **WHEN** the player selects "Reset layout"
- **THEN** panels SHALL return to the default terminal arrangement

### Requirement: Rendering and effects
The desktop client SHALL render through the GPU with pixel-snapped text at integer scale factors, support high-DPI displays, and apply CRT effects as an optional post-process that is disabled when the OS requests reduced motion or the user turns it off.

#### Scenario: High-DPI
- **WHEN** the window moves to a 2× display
- **THEN** text and candles SHALL stay crisp without blurring

### Requirement: Keyboard, mouse and gamepad-free input
All actions SHALL be reachable by keyboard and mouse, with remappable shortcuts identical in meaning to the web defaults. Advance and submit SHALL never share a binding.

#### Scenario: Same shortcut meaning
- **WHEN** a player presses the advance-one-step shortcut on desktop
- **THEN** it SHALL perform the same action as on the web client

### Requirement: Local storage and offline
The desktop client SHALL store runs, commands, checkpoints, progress and installed packs locally (SQLite with WAL plus verified pack files in the user data directory) and SHALL satisfy every `offline-sync` requirement.

#### Scenario: Power loss
- **WHEN** the machine loses power during a step
- **THEN** on restart the run SHALL resume from the last committed command without duplicated fills

### Requirement: Sign-in
The desktop client SHALL sign in through the system browser with a loopback redirect on `127.0.0.1` and an ephemeral port, and SHALL store refresh tokens only in the OS credential store.

#### Scenario: Browser returns to app
- **WHEN** the user completes sign-in in the browser
- **THEN** the loopback listener SHALL receive the code, the browser tab SHALL show a "return to TradeLab" page and the listener SHALL close

### Requirement: Signed distribution and safe updates
Builds SHALL be code-signed (and notarized on macOS). Updates SHALL verify signatures before install and SHALL NOT break in-progress runs (engine pinning per `offline-sync`).

#### Scenario: Tampered update
- **WHEN** an update package fails signature verification
- **THEN** it SHALL NOT be installed and the current version SHALL keep running
