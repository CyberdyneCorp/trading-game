## ADDED Requirements

### Requirement: Templates and free legs
The option ticket SHALL offer a strategy builder with these templates:
- bull call spread;
- bear put spread;
- covered call;
- protective put;
- straddle;
- strangle;
- iron condor.

Straddle and strangle SHALL also be offered short. Free legs SHALL be allowed too: up to four option legs on one expiry, plus an optional perpetual leg. A template SHALL fill strikes from the listed chain around the last price, and every leg's strike, side and quantity SHALL stay editable. Covered call and protective put SHALL offer to use the player's existing long perpetual position instead of a new perpetual order; when a long is held, that choice SHALL be the default, with as many option contracts as the long's underlying units (down to the option lot). Templates that need option writing SHALL be shown disabled when writing is not allowed in the run, naming the reason and the stage that unlocks it. Legs on different expiries SHALL be refused with an explanation.

#### Scenario: Iron condor from the chain
- **WHEN** the player picks the iron condor template with the last price at 100 on a chain with strikes 2,5 apart
- **THEN** the builder SHALL propose four legs: long put, short put, short call and long call with strikes K1 < K2 < 100 < K3 < K4, all on one expiry

#### Scenario: Covered call on a held long
- **WHEN** the player holds a long of 48.2 units (option multiplier 1, lot 0.1) and picks the covered call template
- **THEN** the builder SHALL use that long, propose one short call of 48.2 contracts and no perpetual order

#### Scenario: Writing locked
- **WHEN** the run does not allow option writing
- **THEN** the bull call spread, bear put spread, covered call, iron condor and short straddle and strangle SHALL be disabled with the reason, and the protective put, straddle and strangle SHALL stay available

### Requirement: Pre-trade analysis from the engine
Before submission the builder SHALL show:
- the combined payoff at expiry and today;
- the breakevens;
- the maximum profit and maximum loss, or "unbounded";
- the net premium (debit or credit) and the fees;
- the margin required under the mission's margin rule, and the available balance after the combo;
- the net delta, gamma, theta and vega.

All of these SHALL come from the engine: the what-if valuation at the prices the legs would cross, and a dry run of the legs against the current run state that changes nothing. Submission SHALL be disabled while the dry run refuses any leg, with that leg's reason shown. The plan asked before submission SHALL name the combo's contracts, and when the structure has no maximum loss it SHALL say that the planned risk shown is only the margin required.

#### Scenario: Unbounded loss named
- **WHEN** the legs are a short call and a short put with no long legs
- **THEN** the maximum loss SHALL be shown as unbounded, and the analysis SHALL name the side (above the upper breakeven) where it is unbounded

#### Scenario: Dry run changes nothing
- **WHEN** the builder previews a combo
- **THEN** the run's state and its state digest SHALL be unchanged

### Requirement: Submission and partial failure
A combo SHALL be submitted as ordinary market orders at the current decision step, in this order: long option legs, then the perpetual leg, then short option legs. Every leg SHALL be a logged command tagged with a combo id that replay ignores.
- **Refused at placement:** if any leg is refused when placed, the client SHALL cancel the combo's legs already accepted before the market can advance, and SHALL report which leg was refused and why.
- **Incomplete at execution:** if some legs fill and others are cancelled when they execute, the client SHALL mark the combo incomplete and show the open legs and the risk they now carry, computed by the engine. It SHALL offer to close them (after a confirmation) or keep them, and SHALL NOT place or cancel anything on its own.

#### Scenario: A refused leg cancels the others
- **WHEN** the third leg of an iron condor is refused for insufficient margin at placement
- **THEN** the two legs already accepted SHALL be cancelled at the same step, no leg SHALL execute, and the reason SHALL be shown

#### Scenario: Short leg without its cover
- **WHEN** at execution the long call of a bear call spread is cancelled and the short call fills
- **THEN** the client SHALL mark the combo incomplete, say that the short call's loss is now unbounded above its breakeven, and offer to close it

#### Scenario: Tags do not change replay
- **WHEN** a run log with combo tags is replayed with the tags removed
- **THEN** the state digest SHALL be identical

### Requirement: Combos in the journal and debrief
The journal and the debrief SHALL group the legs of a combo under its structure name (e.g. "Iron condor · 4 legs", "Covered call · 1 leg") with its net premium and its result, and SHALL list an incomplete combo as incomplete. The debrief SHALL show one R for the combo, its summed net over the whole plan's risk, and no R on its legs (the plan's risk is split over them).

#### Scenario: Debrief grouping
- **WHEN** a run's debrief lists trades that came from one combo
- **THEN** they SHALL be shown under one combo row with the structure name, their summed net result and one R for the combo
