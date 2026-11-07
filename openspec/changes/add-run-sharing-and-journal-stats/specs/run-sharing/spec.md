## ADDED Requirements

### Requirement: Run file format
A client SHALL export a run as a run file: UTF-8 JSON with `format` `"tradelab-run"` and `version` 1, and exactly the members `mission`, `pack`, `seed`, `options`, `engine`, `labels`, `actions`, `journal` and `result` (design D1). Actions SHALL use the shared action format of the golden fixtures. Numbers SHALL be integers, and every price, quantity and amount SHALL be a decimal string. A run file SHALL be at most 1 MiB and hold at most 20,000 actions. The web and the desktop SHALL write and read the same format, so a file exported by either client opens in the other.

#### Scenario: Finished run round trip
- **WHEN** a player exports a finished run and imports the file on another device or client
- **THEN** the import SHALL be accepted, and the replayed run SHALL have the recorded status and digests

#### Scenario: Run in progress
- **WHEN** a player exports a run that is still active
- **THEN** the file's `result.status` SHALL be `active`, and importing it SHALL replay to the recorded digest and offer the replay without a debrief at the end

### Requirement: Run files hold no device or player identity
A run file SHALL NOT contain a run id, timestamps, a device key or signing seed, an account, a locale or progress. Plan notes (free text) SHALL be included only when the player opts in at export, which SHALL be off by default; otherwise `journal` SHALL be empty. A run on a private pack (a CSV import) SHALL NOT be exportable, and the export control SHALL say why. A catalog pack SHALL be named only by its digest.

#### Scenario: Notes left out by default
- **WHEN** a player exports a run with three plan notes without ticking "include my plan notes"
- **THEN** the file's `journal` SHALL be `[]`, and the file SHALL still verify

#### Scenario: Private pack
- **WHEN** a player opens the export control of a run played on an imported CSV pack
- **THEN** export SHALL be unavailable, with a message that runs on private markets cannot be shared

### Requirement: The engine verifies run files
The engine SHALL expose `tl_run_file_verify(file, pack, pack_len, keys)`, which does not affect any other run. It SHALL parse the file with the strict JSON reader within the limits of design D2 and validate every member and every action against the format. It SHALL refuse a file whose execution policy differs from the engine's, then replay the actions from a fresh run, assigning client ids as the golden tests do. It SHALL accept the file only when the final status, state digest and events digest equal the recorded `result`. On success it SHALL return the canonical file, its SHA-256 (`fileDigest`), the counts of actions and rejected commands, and the debrief when the run ended. On failure it SHALL return a reason code from design D2 and, for an invalid member, its path. Malformed input of any kind SHALL be refused without crashing, without unbounded memory or time, and without reaching a run command.

#### Scenario: Tampered action
- **WHEN** one order quantity in a valid run file is changed and the file is verified
- **THEN** the result SHALL be `{"ok":false,"reason":"digest_mismatch"}`

#### Scenario: Tampered digest
- **WHEN** the `result.digest` of a valid run file is changed
- **THEN** the result SHALL be `digest_mismatch`

#### Scenario: Another policy
- **WHEN** a file recorded under execution policy 16 is verified by a policy-17 engine
- **THEN** the result SHALL be `policy_mismatch` and no replay SHALL run

#### Scenario: Hostile input
- **WHEN** the fuzz test verifies thousands of random mutations of valid files (bytes flipped, truncated, duplicated or deleted)
- **THEN** every call SHALL return a JSON verdict, and only unmutated files and mutations that keep the same canonical content SHALL be accepted

#### Scenario: Pack run without its pack
- **WHEN** a file naming a catalog pack is verified without the pack bytes
- **THEN** the result SHALL be `pack_required` with the pack's digest, and a client SHALL offer to install it from the catalog

### Requirement: Imported runs never count
An imported run SHALL be stored apart from the player's own runs, keyed by its `fileDigest`, so importing the same run again keeps one copy. It SHALL NOT count toward:
- missions completed or curriculum stages;
- the campaign;
- advisor points or intel perks;
- journal statistics.

It SHALL be labeled as imported wherever it is shown. Removing it SHALL delete only the imported copy.

#### Scenario: Imported finished mission
- **WHEN** a player imports a finished run of a mission they never finished
- **THEN** that mission SHALL NOT be marked completed, and the perks, points and curriculum SHALL be unchanged

### Requirement: Replay viewer
A client SHALL show an imported run, or one of the player's own runs, in a replay viewer. The viewer SHALL rebuild the run in the client's engine from the file and SHALL NOT log, persist or settle anything. It SHALL offer next bar (the actions up to and including the next advance, one bar at a time), play and pause, and to the end. It SHALL show the chart, the account and position, and the events up to the current bar. When the run ended, it SHALL then show the debrief without the player's own controls: points, rewind, another market, play again and the campaign. Leaving the viewer SHALL leave the player's resumable run exactly as it was.

#### Scenario: Bar by bar
- **WHEN** a player steps an imported run with `{"k":"adv","n":5}` in its log
- **THEN** each step SHALL advance one bar, and after five steps the state digest SHALL equal the digest after the original 5-bar advance

#### Scenario: Viewer leaves the saved run alone
- **WHEN** a player with a resumable run watches an imported run and returns to the menu
- **THEN** "Resume" SHALL restore the resumable run with its own digest and actions

### Requirement: Export and import controls
The web and the desktop SHALL offer export on the debrief, on each run of the journal and on the statistics screen, with the option to include plan notes. They SHALL offer import in the menu and in the journal. An import SHALL show the verdict in the player's language: accepted (open the viewer), or the refusal reason with its detail (the policy versions, the missing pack, the invalid member).

#### Scenario: Refused import
- **WHEN** a player imports a file whose digest was edited
- **THEN** the import SHALL be refused with a message that the file does not reproduce its recorded result, and nothing SHALL be stored
