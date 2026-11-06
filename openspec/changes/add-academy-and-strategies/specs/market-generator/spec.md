## ADDED Requirements

### Requirement: Strategy mission learnability
m13–m17 SHALL declare learnability bands (design D8 of add-academy-and-strategies) and SHALL be checked over 60 seeds by scripted reference players through the engine, under the rules of "Mission learnability":
- buy-and-hold is positive in fewer than 75% of seeds;
- every reference strategy that can lose loses in at least 15% of seeds;
- a payoff property holds in every seed.

The pooled realism bands SHALL hold over all 17 missions. Adding m13–m17 SHALL NOT change any byte of an earlier mission's bars, news, reveal or golden.

#### Scenario: Spread against naked call (m13)
- **WHEN** m13 is generated for 60 seeds and the reference spread and naked call are entered after the headline and closed 3 bars before expiry
- **THEN** in seeds where the underlying rose less than the strike width the spread SHALL do at least as well as the call in more than 70%, and in seeds where it rose more than twice the width the call SHALL do better in more than 80%

#### Scenario: Protective put floor (m15)
- **WHEN** a long is held in m15 with an equal ATM put of the second expiry
- **THEN** its loss SHALL NOT exceed (entry − strike + premium + costs) × quantity in any seed

#### Scenario: Defined against undefined risk (m16)
- **WHEN** m16 is generated for 60 seeds and an iron condor and a short strangle with the same short strikes and quantity are held to settlement
- **THEN** the condor SHALL never lose more than (width − credit) × units + costs, and the strangle's worst loss SHALL be at least 3 times the condor's worst loss

#### Scenario: Theta and gamma near expiry (m17)
- **WHEN** an ATM option is priced 20 bars and 3 bars before its expiry on m17's generated paths
- **THEN** its theta per bar and its gamma 3 bars before SHALL each be at least twice their values 20 bars before

#### Scenario: Earlier missions untouched
- **WHEN** every earlier golden is replayed after m13–m17 are added
- **THEN** every digest SHALL be unchanged
