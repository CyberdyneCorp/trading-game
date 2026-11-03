# Spec Delta

## Purpose

Defines the native SwiftUI client for iPad and iPhone: layouts, engine interop, lifecycle durability, storage, input and sign-in, with complete offline play.

## ADDED Requirements

### Requirement: Supported devices
The app SHALL support iPadOS and iOS (latest two major versions) on iPad and iPhone, and SHALL produce the same golden digests as all other targets.

#### Scenario: Golden parity on device
- **WHEN** the golden suite runs in the iOS test target on a physical device
- **THEN** every digest SHALL equal the reference digests

### Requirement: iPad terminal layout
In landscape on iPad the app SHALL implement the `terminal-ui` large-screen layout, support Stage Manager resizing, hardware keyboard shortcuts with the same meanings as web/desktop (discoverable via the Command key overlay) and pointer hover on chart elements.

#### Scenario: Hardware keyboard
- **WHEN** a hardware keyboard is attached
- **THEN** advance, open ticket and close position SHALL be available as shortcuts

### Requirement: iPhone layout
On compact width the app SHALL use native tabs — Market, Trade, Portfolio, News, Learn — with the order ticket as a bottom sheet and an optional full-screen chart. It SHALL NOT render a scaled-down desktop layout.

#### Scenario: Ticket as sheet
- **WHEN** the player taps "Trade" on iPhone
- **THEN** the order ticket SHALL open as a bottom sheet over the chart

### Requirement: Touch targets and accessibility
Interactive elements SHALL be at least 44×44 pt, support Dynamic Type for non-chart text, VoiceOver with the accessible chart table, and Reduce Motion.

#### Scenario: VoiceOver on chart
- **WHEN** VoiceOver focuses the chart
- **THEN** it SHALL read the current closed bar summary and never a future bar

### Requirement: Engine via Swift C++ interop
The app SHALL call the engine through Swift C++ interop, running simulation work off the main actor.

#### Scenario: Main thread free
- **WHEN** a batch advance runs
- **THEN** the main actor SHALL remain responsive to input

### Requirement: Lifecycle durability
The app SHALL persist every command before acknowledging it and SHALL save a checkpoint when moving to background, so termination by the system never loses committed decisions.

#### Scenario: Backgrounded and killed
- **WHEN** the app is backgrounded mid-run and the system terminates it
- **THEN** reopening SHALL restore the run at the last committed command

### Requirement: Storage and downloads
The app SHALL store runs and progress in SQLite and packs in Application Support excluded from iCloud backup when re-downloadable, SHALL use background URL sessions for resumable pack downloads, and SHALL satisfy every `offline-sync` requirement.

#### Scenario: Download continues in background
- **WHEN** the app is backgrounded during a pack download
- **THEN** the download SHALL continue and install atomically after verification

### Requirement: Sign-in
The app SHALL sign in with `ASWebAuthenticationSession` using authorization code + PKCE and SHALL keep refresh tokens in the Keychain with this-device-only accessibility.

#### Scenario: Restore on new device
- **WHEN** the user restores an iCloud backup to a new device
- **THEN** the refresh token SHALL NOT be restored and the user SHALL sign in again, while local runs remain available
