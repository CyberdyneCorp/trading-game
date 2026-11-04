# Spec Delta

## Purpose

Defines web-specific behavior of the SvelteKit client: engine isolation in a worker, local persistence, keyboard control, session handling through the BFF and performance budgets.

## ADDED Requirements

### Requirement: Engine in a worker
The web client SHALL run the WASM engine in a dedicated Web Worker and SHALL communicate through a typed message protocol that carries only commands in and current-step state/events out. The main thread SHALL never load scenario bars after the current step.

#### Scenario: No future data on main thread
- **WHEN** the main thread's memory and message log are inspected mid-run
- **THEN** no bar, event or reveal data after the current step SHALL be present

### Requirement: Local persistence
Runs, commands and checkpoints SHALL be stored in IndexedDB, with each command committed before it is acknowledged in the UI. Installed packs SHALL be cached by digest.

#### Scenario: Reload mid-run
- **WHEN** the tab is closed and reopened
- **THEN** the run SHALL resume at the same step with the same digest

### Requirement: Completed missions in the menu
The mission menu SHALL mark a mission "Completed" when at least one run of it on this device reached the end of the mission, derived from the stored runs. A bankrupt or unfinished run SHALL NOT mark it.

#### Scenario: Back to the menu after a finished mission
- **WHEN** the player finishes a mission and returns to the menu, or reloads the page later
- **THEN** that mission's card SHALL show the Completed mark and no other card SHALL

### Requirement: CSV import wizard
The menu SHALL offer a CSV import wizard that reads the file on the device, asks for the metadata the file cannot carry (asset, pair, venue, bar interval, timestamp unit, volume unit, leverage cap), shows the engine's validation report (errors by line in plain language, gaps with their size, rows, period and whether funding is present) and, only for a valid file, creates a practice mission from a private pack. The wizard SHALL NOT correct data or units on its own; it MAY pre-select the unit and interval guessed from the first rows. Imported markets SHALL be listed and labeled as known scenarios, and during a run the UI SHALL show neither the real asset nor unmasked prices; the reveal SHALL appear only in the debrief. A run on an imported pack SHALL resume after a reload like any other run.

#### Scenario: Wrong timestamp unit in the wizard (DATA01-web)
- **WHEN** the player declares seconds for a file whose timestamps are in milliseconds and validates
- **THEN** the wizard SHALL show an error naming the line and the unit problem, and SHALL NOT offer to create the mission

#### Scenario: Practice run on an imported market
- **WHEN** the player creates a mission from a valid CSV and plays it to the end
- **THEN** the briefing and top bar SHALL mark it as a known scenario with masked prices, and the debrief SHALL reveal the pair and the period

#### Scenario: Pack from an unknown signer
- **WHEN** the player loads a `.tlpack` file not signed by this device or a pinned publisher
- **THEN** it SHALL NOT be installed and the menu SHALL say why


The web client SHALL be an installable Progressive Web App. A service worker SHALL precache the versioned app shell, fonts, i18n catalogs and WASM engine, so that after one online visit the app starts with no network connection. Offline, the client SHALL satisfy the `offline-sync` full-offline-play requirement for packs already downloaded, and SHALL show catalog, download and sync as unavailable rather than failing.

#### Scenario: Offline start (OFF01-web)
- **WHEN** the installed web app is opened in airplane mode with one downloaded pack
- **THEN** the player SHALL start, finish and debrief a mission, and open lessons, with no network request succeeding

#### Scenario: Not yet downloaded
- **WHEN** the player is offline and selects a pack that is not downloaded
- **THEN** the client SHALL say it needs a connection to download, without an error page

### Requirement: Persistent offline storage
Downloaded packs SHALL be stored by digest in the Origin Private File System (or IndexedDB where unavailable) after hash and signature verification, and runs in IndexedDB. The client SHALL request persistent storage (`navigator.storage.persist()`), SHALL show used and available quota before a download, and SHALL warn when storage is not persistent that the browser may evict offline data (notably Safari when the app is not installed to the Home Screen). Evicted packs SHALL be detected on start and offered for re-download; runs that are not yet synced SHALL be flagged as at risk.

#### Scenario: Quota exceeded
- **WHEN** a pack download would exceed the available quota
- **THEN** the download SHALL not start and the client SHALL show the space needed and how to free it

#### Scenario: Storage evicted
- **WHEN** the browser evicted a downloaded pack while the app was closed
- **THEN** on start the pack SHALL be shown as not downloaded and no run SHALL start from partial data

### Requirement: Safe web updates
A new app version SHALL be installed by the service worker in the background and activated only on the player's confirmation or the next cold start, never in the middle of a mission. Each cached app version SHALL keep the engine WASM build required by every unfinished local run (engine pinning per `offline-sync`), and a version SHALL be purged only when no unfinished run needs it.

#### Scenario: Update during a mission
- **WHEN** a new version is deployed while the player is mid-mission offline or online
- **THEN** the mission SHALL continue on the current version and the update SHALL be offered after it ends

#### Scenario: Old run after update
- **WHEN** an unfinished run recorded with engine 1.2 is resumed after the app updated to engine 1.3
- **THEN** it SHALL resume on engine 1.2 and produce the same digest

### Requirement: Keyboard operation
Every action SHALL be reachable by keyboard with visible focus. Default shortcuts SHALL include advance one step, advance batch, open ticket, close position, toggle option chain and open journal, SHALL be listed in a help overlay, and SHALL be remappable. Advance SHALL never share a key with order submission.

The default bindings SHALL be: `Space` advance one bar (stops auto-advance instead while it runs), `Shift+Space` advance five bars (stopping early at a calibration question), `B` / `S` set the ticket side to buy / sell, `X` close the position, `P` open or close the candle pattern sheet, `A` toggle auto-advance, `?` toggle the help overlay and `Esc` close dialogs or cancel marking a level on the chart. Orders are submitted only with the ticket's submit button (`Enter` when it has focus). Shortcuts SHALL be ignored while typing in a text field (a focused checkbox, radio, slider or select keeps its own keys but not `Space`, which still advances), while a plan or calibration dialog is open or while marking a level on the chart, and with Ctrl/Cmd/Alt held. The pattern sheet and the help overlay are modal: while open, only their own toggle key and `Esc` act.

#### Scenario: Shortcut help
- **WHEN** the player presses `?`
- **THEN** a shortcut overlay SHALL list all bindings

#### Scenario: Space never advances behind a dialog
- **WHEN** the plan dialog, a calibration question, the pattern sheet or the help overlay is open and the player presses `Space`
- **THEN** the market SHALL NOT advance

### Requirement: Session via BFF
The web client SHALL authenticate only through the game API BFF endpoints, SHALL send the CSRF token on mutating requests and SHALL NOT handle OAuth tokens directly.

#### Scenario: Session expired
- **WHEN** the BFF session expires mid-run
- **THEN** play SHALL continue locally and a non-blocking prompt SHALL ask the user to sign in to sync

#### Scenario: Offline with an old session
- **WHEN** the app is used offline for days and the BFF session has expired
- **THEN** play SHALL continue with the last known profile shown as "offline", and sync SHALL require sign-in once online

### Requirement: Performance budgets
On the reference laptop the web client SHALL meet: p95 interaction latency under 100 ms, chart rendering near 60 fps during pan/zoom with 5,000 bars, mission ready under 3 s after pack load, and initial JavaScript + WASM under 1 MB gzipped. Budgets SHALL be measured in CI on a fixed dataset.

#### Scenario: Budget regression
- **WHEN** a change increases the gzipped bundle above budget
- **THEN** CI SHALL fail

### Requirement: Supported browsers
The web client SHALL support the latest two versions of Chrome, Edge, Firefox and Safari on desktop and iPadOS, and SHALL show a clear message on unsupported browsers (e.g. missing WebAssembly or Ed25519 WebCrypto).

#### Scenario: Missing capability
- **WHEN** the browser lacks WebCrypto Ed25519
- **THEN** the client SHALL show an unsupported-browser message instead of failing silently
