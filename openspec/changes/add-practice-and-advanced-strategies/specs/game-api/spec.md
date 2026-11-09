## ADDED Requirements

### Requirement: Replay of policy v18 runs and older policies
The API SHALL re-simulate runs recorded under execution policy v18, including the `tif`/`tifBars` fields and `oco` actions, with the current engine. It SHALL re-simulate v17 runs through the policy cache within the configured policy window (v16–v18 with the default window of 3). It SHALL ignore the client tags `combo`, `strategy`, `roll` and `adjust` and the `daily` and `mistake` run links. No new endpoint SHALL be added.

#### Scenario: v17 run after the bump
- **WHEN** a run recorded under policy v17 is synced after the API moves to v18
- **THEN** the API SHALL replay it with the cached v17 engine and accept it when its digest matches

#### Scenario: v18 run with OCO
- **WHEN** a v18 run with a linked OCO pair and an IOC limit is synced
- **THEN** the API SHALL replay it with the current engine to the client's digest
