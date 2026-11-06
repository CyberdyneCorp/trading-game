## ADDED Requirements

### Requirement: Strategies stage and the 17-mission graph
The curriculum SHALL add a Strategies stage after Options III and before Patterns & Indicators. It requires Options III, has no unlock of its own, and is assessed by m13, m14, m15, m16 and m17. This requirement replaces "Phase-2 mission graph" of add-scenarios-and-gameplay. The campaign SHALL play:

m06, m01, m03, m02, m07, m04, m09, m05, m10, m13, m14, m15, m16, m17, m08, m11, m12.

The other stages keep their assessment missions and unlocks.

#### Scenario: Strategies opens after Options III
- **WHEN** a player has passed Options III
- **THEN** Strategies SHALL be open and m13 SHALL be the next campaign mission

#### Scenario: Capstone still last
- **WHEN** a campaign run reaches m12
- **THEN** every other campaign mission, m13–m17 included, SHALL come before it

#### Scenario: No lock the engine cannot enforce
- **WHEN** the campaign's missions are checked against the locks they meet
- **THEN** m13–m17 SHALL meet no lock, since they come after Options III

### Requirement: Lessons prepare, missions assess
A stage with assessment missions SHALL be passed, and SHALL grant its unlocks, by its missions only: its Academy lessons are preparation and SHALL NOT gate it. A stage with lessons and no mission (Foundations) SHALL still pass when its lessons are completed and SHALL still never block later stages. The curriculum map SHALL show, per stage, its lessons with how many are done.

#### Scenario: Lesson not done
- **WHEN** a player finishes m03 in a campaign without completing the Futures II lesson
- **THEN** Futures II SHALL be passed and leverage above 2× SHALL be unlocked

#### Scenario: Lessons on the map
- **WHEN** the curriculum map is shown
- **THEN** each stage SHALL list its Academy lessons with done and total counts and a start or retake control

### Requirement: Recommended lesson opens the Academy
When the debrief recommends a stage, it SHALL offer to open that stage's first Academy lesson not yet completed, or its first lesson when all are done.

#### Scenario: Stop beyond liquidation
- **WHEN** a debrief recommends Futures II and the player has not completed its lesson
- **THEN** the debrief SHALL offer to open the Futures II lesson

### Requirement: Campaign debrief names the stages passed
A campaign mission's debrief SHALL name only the stages that are passed once it is finished: its stages whose missions are all assessed. A stage with several missions SHALL be named by its last one.

#### Scenario: First Strategies mission
- **WHEN** m13 is finished in a campaign run and m14 to m17 are not yet assessed
- **THEN** the debrief SHALL NOT say that the Strategies stage was passed
