# Spec Delta

## Purpose

Defines offline play on native clients and the installed web app, and the durable, idempotent synchronization of runs and progress with the server across devices.

## ADDED Requirements

### Requirement: Full offline play
After a pack is downloaded and verified, desktop and iOS clients, and the web client once installed as a PWA, SHALL allow starting, pausing, resuming and completing missions, reading lessons and viewing debriefs without any network connection or AI service.

#### Scenario: Airplane mode (OFF01)
- **WHEN** the device is in airplane mode with an installed pack
- **THEN** a full mission including debrief SHALL complete successfully

### Requirement: Durable decisions
Clients SHALL persist each command transactionally in local storage before showing it as accepted, and SHALL store checkpoints so that a crash restores the last consistent state without duplicating fills.

#### Scenario: Killed during write (OFF02)
- **WHEN** the app is terminated while persisting a command
- **THEN** on restart the run SHALL resume from the last committed command with no duplicated fill

### Requirement: Atomic pack installation
Downloads SHALL be resumable; a pack SHALL be installed atomically only after size, hash and signature verification, and SHALL require the free space declared in the manifest.

#### Scenario: Interrupted download
- **WHEN** a download is interrupted at 60%
- **THEN** no partial pack SHALL be usable and the download SHALL resume from the stored offset

### Requirement: Idempotent batch sync
When online and authenticated, clients SHALL upload run commands and completions in batches keyed by run id and command sequence. Re-sending a batch SHALL NOT duplicate commands or runs.

#### Scenario: Resent batch (SYNC01)
- **WHEN** the same batch is sent twice and the user then syncs from a second device
- **THEN** the server SHALL hold exactly one copy of each command and both devices' attempts SHALL be preserved

### Requirement: Per-device attempts
Each attempt SHALL have a globally unique run id generated on the device. Attempts from different devices SHALL never overwrite each other; both SHALL be shown.

#### Scenario: Same mission on two devices
- **WHEN** the same mission is played offline on iPad and desktop
- **THEN** after sync both attempts SHALL appear in history

### Requirement: Merge rules
Lesson and campaign progress SHALL merge as the union of server-verified achievements. Preferences SHALL use last-write-wins by server receive time.

#### Scenario: Union of progress
- **WHEN** device A completed lesson 3 and device B completed lesson 4 offline
- **THEN** after sync both lessons SHALL be complete on both devices

### Requirement: Engine pinning for in-flight runs
A client update SHALL NOT change the result of an in-progress run: the client SHALL keep the run's engine/execution-policy version available or apply a tested migration, and SHALL block updates that would make an in-progress run unplayable.

#### Scenario: Update mid-run
- **WHEN** the app updates to a new engine version during an unfinished run
- **THEN** the run SHALL continue with its recorded execution-policy version and produce the same digest as before the update

### Requirement: Web persistence
The web client SHALL save and resume runs locally in the browser (IndexedDB) and sync them when signed in, and SHALL support full offline play as specified in the `web-client` offline PWA requirements.

#### Scenario: Browser reload
- **WHEN** the browser tab is reloaded mid-run
- **THEN** the run SHALL resume at the same step with the same state digest
