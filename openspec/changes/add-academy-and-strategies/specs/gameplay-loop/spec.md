## ADDED Requirements

### Requirement: Strategy comparison in the debrief
For m13–m17, the debrief SHALL include a strategy comparison computed by the engine after the run ends. It values the mission's declared structures on this seed's path:
- entered at the open after the player's first option entry, or at the mission's reference bar when there was none;
- held to the mission's exit rule;
- with the engine's quotes, fees and margin rules.

For each structure it SHALL report the net P&L, the maximum drawdown and any margin call, and the client SHALL show the rows next to the player's own result, labeled as computed on this seed. The comparison SHALL be report-only: not a score input and in no digest. Clients SHALL request it only after the run has ended.

#### Scenario: Covered call compared (m14)
- **WHEN** a player finishes m14 having held only a long
- **THEN** the debrief SHALL show long-only and the covered call from the reference bar on this seed, with their net P&L, maximum drawdown and margin calls

#### Scenario: Not before the end
- **WHEN** the run is still active
- **THEN** the client SHALL NOT request the comparison, and the worker schema test SHALL enforce it
