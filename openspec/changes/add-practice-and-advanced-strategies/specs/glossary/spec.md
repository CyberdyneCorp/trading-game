## ADDED Requirements

### Requirement: Terms for the new features and missions
The glossary SHALL add these 26 terms in both locales, each with a short and a long definition and a stage:
- the order and terminal terms: `time-in-force`, `ioc`, `fok`, `oco`, `heatmap`, `portfolio-greeks`, `risk-limit`;
- the option terms: `delta-hedge`, `delta-neutral`, `rehedge`, `gamma-scalping`, `calendar-spread`, `diagonal-spread`, `term-structure`, `implied-move`, `event-volatility`, `synthetic-forward`, `put-call-parity`;
- carry: `carry`, `basis`;
- management: `scale-in`, `scale-out`, `breakeven-stop`, `partial-take-profit`;
- behavior: `averaging-down`, `cooldown`.

The existing `trailing-stop` term SHALL gain a worked example covered by a check. Every term the new UI marks or a new lesson or card links SHALL exist in both locales.

#### Scenario: Parity example checked
- **WHEN** the `put-call-parity` example states that a 100 call at 6 and a 100 put at 4 imply a forward of 102 at a zero rate
- **THEN** a check SHALL produce 102 (100 + 6 − 4)

#### Scenario: Marked term exists
- **WHEN** the ticket marks "IOC" as a glossary term
- **THEN** the `ioc` term SHALL exist in PT-BR and en
