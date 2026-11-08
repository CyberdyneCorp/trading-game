## ADDED Requirements

### Requirement: Heatmap view in the lab and the builder
The what-if lab and the strategy builder SHALL offer a P&L heatmap over price and time to the last expiry of the legs. Every cell SHALL come from the engine's heatmap. The view SHALL:
- use a diverging palette that stays readable in the colorblind setting;
- show the value of the hovered or focused cell;
- offer a table view for screen readers.

#### Scenario: Cell from the engine
- **WHEN** the player focuses a heatmap cell
- **THEN** the shown P&L SHALL equal the engine's heatmap value for that price and time

#### Scenario: Accessible table
- **WHEN** a screen reader user opens the heatmap
- **THEN** a table of prices by times with the same values SHALL be available

### Requirement: Multi-expiry analysis display
When the legs span two expiries, the lab and the builder SHALL show the engine's `horizon` analytics (curve, breakevens, maximum profit and loss), labeled "at the first expiry, at today's implied volatility level", and SHALL NOT show an expiry payoff that assumes one expiry.

#### Scenario: Calendar labeled
- **WHEN** a calendar spread is analysed
- **THEN** the analysis SHALL show the horizon curve and breakevens with the first-expiry label
