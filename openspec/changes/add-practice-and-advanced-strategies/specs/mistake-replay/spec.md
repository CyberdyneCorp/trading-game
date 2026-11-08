## ADDED Requirements

### Requirement: Retry from a flagged moment
Every debrief finding that has a step and a warning tone, and every warning coach moment, SHALL offer "Retry from here". It SHALL start an assisted attempt that keeps the actions logged before that step and resumes at that step's decision. The run record's link SHALL be `{"from","kind":"mistake","step","code"}`. The terminal SHALL show a banner naming the moment, and SHALL NOT say what happened after it.

#### Scenario: Back before the mistake
- **WHEN** the player retries a `revenge_trade` moment at step 71
- **THEN** the new attempt SHALL start at the decision of step 71, with every action logged before step 71 replayed and none from step 71 on

### Requirement: Mistake retries never count
A mistake retry SHALL follow the rules of assisted practice:
- no advisor;
- never a mission completion, advisor points or a campaign leg;
- excluded from journal statistics;
- not offered inside a campaign mission in progress or on an imported run.

#### Scenario: Not counted
- **WHEN** a mistake retry reaches its debrief with a high score
- **THEN** the mission's completion, the points, the campaign and the journal statistics SHALL be unchanged

### Requirement: Avoided or repeated
At a mistake retry's debrief the client SHALL compare the retry's coach moments and findings with the original moment's code. It SHALL say **repeated** when the same code occurs at a step from the moment's step to five steps later, and **avoided** otherwise, and SHALL show both results side by side, labeled one sample.

#### Scenario: Avoided
- **WHEN** the retry of an `averaging_down` moment at step 90 has no `averaging_down` finding from step 90 to step 95
- **THEN** the debrief SHALL say the mistake was avoided
