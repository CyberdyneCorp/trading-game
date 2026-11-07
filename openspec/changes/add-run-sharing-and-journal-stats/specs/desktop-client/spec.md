## ADDED Requirements

### Requirement: Run files and journal statistics on the desktop
The desktop client SHALL export and import run files in the web's format through the linked engine's `tl_run_file_verify`. It SHALL store imported runs apart from its own runs, so they never count, and SHALL offer the replay viewer and the journal statistics screen with the web's rules. Its statistics SHALL be computed by a port of the web's aggregation, checked against the web parity fixtures.

#### Scenario: File exported on the web opens on the desktop
- **WHEN** the desktop imports every valid file of `engine/tests/runfiles/`
- **THEN** each SHALL be accepted with the expected digests, and each invalid file SHALL be refused with its expected reason

#### Scenario: File exported on the desktop opens on the web
- **WHEN** a run exported by the desktop is imported on the web
- **THEN** the web SHALL accept it and replay it to the same digest
