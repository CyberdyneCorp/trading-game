## ADDED Requirements

### Requirement: Tutorial before a mission
Each built-in mission SHALL have a tutorial of 3 to 5 cards in every shipped locale. Each card has a title, at most three sentences and an illustration, and together the cards cover the mission's concept, what to watch and which controls to use. The tutorial SHALL be shown before the briefing the first time the player starts that mission on the device. The player SHALL be able to skip it at any card, and SHALL be able to replay it from the briefing. Tutorial text SHALL NOT reveal the mission's market outcome (the reveal stays in the debrief).

#### Scenario: First start shows the tutorial
- **WHEN** the player starts a mission for the first time on this device
- **THEN** the tutorial SHALL be shown before the briefing
- **AND** after the last card or "Skip" the briefing SHALL be shown

#### Scenario: Later starts skip it, replay is available
- **WHEN** the player starts the same mission again
- **THEN** the briefing SHALL be shown directly
- **AND** a "Tutorial" control on the briefing SHALL show the cards again

### Requirement: Tutorials introduce the advisor and perks
Once the advisor exists, the first mission's tutorial SHALL explain the advisor, its cost in points and how points are earned (no card SHALL describe a control the game does not have yet). When a perk is unlocked, the next tutorial or briefing shown SHALL explain that perk and that it is modeled on a legal product.

#### Scenario: New perk explained
- **WHEN** the player finishes a second distinct mission and opens the next briefing
- **THEN** the briefing SHALL explain Fast wire I before the player can enable it
