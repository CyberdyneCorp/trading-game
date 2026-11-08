## ADDED Requirements

### Requirement: Trade management, advanced options and psychology stages
The curriculum SHALL add three stages:
- **Trade management**: requires Risk, assessed by m22, on the campaign path.
- **Advanced options and carry**: requires Strategies, assessed by m18, m19, m20 and m21, elective.
- **Psychology**: requires Events, assessed by m23, m24 and m25, on the campaign path.

The existing stages SHALL keep their requirements, so no stage that is passed or open becomes locked. The map SHALL draw the new stages as branches. The stage count SHALL become 17.

#### Scenario: Nothing passed becomes locked
- **WHEN** a player who passed Risk, Strategies and Events before the update opens the map
- **THEN** every stage they had passed SHALL still be passed, and the three new stages SHALL be open

### Requirement: The 21-mission campaign
New campaign runs SHALL play:

m06, m01, m03, m02, m07, m22, m04, m09, m05, m10, m13, m14, m15, m16, m17, m08, m11, m23, m24, m25, m12.

This requirement replaces the 17-mission order of add-academy-and-strategies. m22 and m23–m25 SHALL meet no lock the engine cannot enforce, and the capstone SHALL stay last.

#### Scenario: m22 after m07
- **WHEN** a new campaign run finishes m07
- **THEN** the next campaign mission SHALL be m22

### Requirement: Elective stages
A stage MAY be elective. Its missions SHALL NOT be campaign legs. A finished, non-assisted free-play or daily-challenge run of one of its missions SHALL count as assessed, so the stage can pass. Assisted attempts, imported runs and pack runs SHALL NOT count.

#### Scenario: Elective passed in free play
- **WHEN** a player finishes m18, m19, m20 and m21 in free play without assistance
- **THEN** the Advanced options and carry stage SHALL be passed

### Requirement: Campaign order snapshot
A campaign run SHALL store its mission order when it starts and SHALL follow that order to its end. A campaign run that predates the snapshot SHALL be given the 17-mission order of add-academy-and-strategies when progress is migrated.

#### Scenario: Active run keeps its order
- **WHEN** a player updates in the middle of a campaign run at its sixth mission (m04 in the old order)
- **THEN** the run's next mission SHALL still be m04, not m22
