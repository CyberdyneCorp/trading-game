## ADDED Requirements

### Requirement: Limit-priced legs and combo time in force
Each builder leg SHALL be a market or a limit leg. A limit leg's default premium SHALL be the quote mid rounded to the tick toward the passive side (down for buys, up for sells), and SHALL be editable on the premium tick. A combo SHALL carry one time in force for all its limit legs: `ioc` (the default), N bars or `gtc`.
- The analysis SHALL value limit legs at their premiums.
- The dry run SHALL include each leg's type, premium and time in force.

This requirement replaces the "market legs only" limit of add-academy-and-strategies.

#### Scenario: IOC default
- **WHEN** the player switches a combo's legs to limit
- **THEN** the combo's time in force SHALL default to `ioc` and every leg SHALL show its limit premium

### Requirement: Combo states
The client SHALL derive each combo's state from its legs' orders and fills:
- `working`: some legs are still working;
- `complete`: every leg filled;
- `incomplete`: some legs filled and the others ended unfilled;
- `void`: nothing filled.

A working combo SHALL offer "Cancel working legs". An incomplete combo SHALL keep the existing notice (the risk of what is held, "Close the filled legs" after a confirmation, "Keep"). The client SHALL never act without the player.

#### Scenario: One leg filled, one still working
- **WHEN** after an advance the long leg of a `gtc` limit spread filled and its short leg is still working
- **THEN** the combo SHALL be shown as working with "Cancel working legs"

#### Scenario: IOC leg missed
- **WHEN** after an advance an `ioc` combo's long leg filled and its short leg was cancelled with `time_in_force`
- **THEN** the combo SHALL be incomplete and the notice SHALL name the held leg and its risk

### Requirement: Legs on two expiries
Free legs SHALL be allowed on up to two expiries. The builder SHALL offer calendar and diagonal templates, each with a reverse version:
- **calendar:** short near and long far at one strike;
- **diagonal:** short near at K2 and long far at K1, with K1 below K2 for calls and above it for puts.

Their analysis SHALL use the engine's horizon analytics. This requirement replaces "Legs on different expiries SHALL be refused" of add-academy-and-strategies.

#### Scenario: Calendar from the chain
- **WHEN** the player picks the call calendar template with the last price at 100 and expiries listed at +20 and +45 bars
- **THEN** the builder SHALL propose a short 100 call on the +20 expiry and a long 100 call on the +45 expiry

### Requirement: One-click roll
An option position or a combo SHALL offer "Roll". It proposes, as one submission:
- closing every leg of its near expiry;
- opening the same structure on a later listed expiry (the next by default, any later one selectable), at the same strikes when listed, else the nearest listed ones, with "re-centre at the money" as an option.

The roll SHALL keep the original combo id, tag its legs `roll`, be dry-run and valued (the holdings after the roll) before sending, and follow the ordering rule of this spec.

#### Scenario: Rolling a short put
- **WHEN** the player rolls a short 100 put expiring in 4 bars and the next expiry lists 100
- **THEN** the submission SHALL buy back the near 100 put first and then sell the next expiry's 100 put, both tagged with the original combo id

### Requirement: Adjusting a live combo
A live combo SHALL offer "Close leg" (the opposite order for one leg) and "Add hedge leg" (a perpetual leg defaulting to the hedge-to-neutral quantity, or an option leg). The new legs SHALL keep the combo id and carry an `adjust` tag. Before sending, the builder SHALL show the engine's what-if of the holdings after the adjustment. When a short would be left uncovered, the builder SHALL ask for a confirmation that names that leg.

#### Scenario: Closing a wing
- **WHEN** the player closes the long call wing of an iron condor
- **THEN** the analysis SHALL show the loss above the short call as unbounded and the builder SHALL ask for a confirmation naming the short call

### Requirement: One ordering rule for submissions
Every builder submission (a new combo, a roll or an adjustment) SHALL be placed in this order:
1. every option buy, whether it opens a long or closes a short;
2. the perpetual leg;
3. every option sell, whether it closes a long or opens a short.

A leg refused at placement SHALL cancel the accepted legs of that submission before the player can advance. Replay and the server SHALL ignore the `combo`, `strategy`, `roll` and `adjust` tags.

#### Scenario: Tagged log replays unchanged
- **WHEN** a log with roll and adjust tags and the same log without tags are replayed
- **THEN** both SHALL reach the same digest
