## ADDED Requirements

### Requirement: Time in force in the tickets
The perpetual and option tickets SHALL offer the time in force the engine allows for the chosen order type: GTC, "day" (N = 86,400 / the bar interval in seconds), N bars, and IOC and FOK for limit orders. A choice the engine would refuse SHALL be disabled, with the reason. The orders panel SHALL show each working order's time in force and expiry bar, and the event feed SHALL explain `time_in_force` and `invalid_time_in_force`.

#### Scenario: IOC disabled on a stop
- **WHEN** the player selects a stop-market order
- **THEN** IOC and FOK SHALL be disabled with a reason, and GTC, day and N bars SHALL be available

### Requirement: Linking orders as OCO
The orders panel SHALL let the player select two working orders and link them as OCO after a confirmation. Linked orders SHALL be marked in the panel and on their chart lines. A refused link SHALL show the engine's reason.

#### Scenario: Link mark
- **WHEN** the player links a buy-stop and a sell-stop
- **THEN** both orders and both chart lines SHALL show the link mark until one fills or the link dissolves

### Requirement: Trailing stop easy to find
The ticket's protective section SHALL show Stop, Trailing and Target as visible tabs. The new-order ticket SHALL offer "Protect with" a fixed stop or a trailing stop (by price distance or % of the best close). Choosing trailing SHALL record a planned trailing stop. On the first decision after the entry fills, a notice SHALL offer to place it with its first trigger, and nothing SHALL be sent without the player's click. On the chart, a trailing stop's label SHALL show its rule and trigger, and a marker SHALL show the best close it follows, from bars up to the current step only.

#### Scenario: Planned trailing offered after the fill
- **WHEN** an entry planned with a 2% trailing stop fills
- **THEN** the next decision SHALL show a notice offering to place the trailing stop with its computed trigger, and no order SHALL be placed until the player confirms

#### Scenario: Chart label
- **WHEN** a trailing stop at 2% of the best close is working
- **THEN** its chart label SHALL read the rule and the trigger, and the best-close marker SHALL sit on the highest close since placement for a long
