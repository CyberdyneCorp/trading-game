# Spec Delta

## Purpose

Defines the deterministic, lookahead-free simulation clock and numeric foundation that every client and the server share, so identical inputs always produce identical, verifiable results.

## ADDED Requirements

### Requirement: Pure engine boundary
The engine SHALL accept only a validated scenario, a seed and an ordered command log as inputs, and SHALL emit only state snapshots and execution events as outputs. The engine SHALL NOT read the wall clock, the device locale, environment variables, files or the network, and SHALL NOT spawn threads.

#### Scenario: No ambient inputs
- **WHEN** the same scenario, seed and commands are run under different time zones, locales and system clocks
- **THEN** every emitted event and the final state digest SHALL be identical

### Requirement: Bar-close decision clock
The engine SHALL expose decision points only after a bar has closed. A command submitted at decision step `t` SHALL become eligible for execution no earlier than the first price of step `t+1`.

#### Scenario: Order submitted after close
- **WHEN** a market order is submitted at decision step `t`
- **THEN** its earliest possible fill SHALL be priced from the open of bar `t+1`
- **AND** no fill SHALL be attributed to bar `t` or earlier

### Requirement: Fixed step sequence
For each advanced step the engine SHALL perform, in this order: apply queued commands in sequence order; release events scheduled for this step; process the opening price and gaps; evaluate liquidations and triggers against the bar using the intrabar policy; execute fills and charge costs; apply funding if the step is a funding boundary; settle expiring options; update positions, margin and equity; close the bar and update indicators and trailing references; emit a checkpoint.

#### Scenario: Sequence is observable
- **WHEN** a step contains a gap, a stop trigger, a funding boundary and an option expiry
- **THEN** the emitted events SHALL appear in the order defined by this requirement with strictly increasing event sequence numbers

### Requirement: No lookahead
State, indicators, option prices, liquidation thresholds and any data exposed to the player at decision step `t` SHALL depend only on scenario data with timestamps up to and including the close of bar `t` and on events released at or before `t`.

#### Scenario: Future data mutation (SIM01)
- **WHEN** all scenario data after bar `t` is replaced by arbitrary valid data
- **THEN** every event, indicator value, option quote and score component emitted up to step `t` SHALL be unchanged

### Requirement: Fixed-point numerics
Prices, quantities, money, fees, funding rates, margin and option premiums SHALL be represented as signed 64-bit integers with fixed scales per quantity type, versioned with the execution policy. Scenario tick sizes, lot sizes and other price- or quantity-denominated parameters SHALL be exact multiples of those scales, and a scenario violating this SHALL be rejected. Every rounding SHALL use a rule declared per quantity (e.g. fees round up against the player, sizes round down to lot). Overflow SHALL be detected and reported as an engine error, never wrapped silently.

#### Scenario: Fee rounding
- **WHEN** a fee computes to a value between two representable units
- **THEN** it SHALL be rounded toward the direction that costs the player more

#### Scenario: Tick finer than engine scale
- **WHEN** a scenario declares a tick size that is not an exact multiple of the price scale
- **THEN** the scenario SHALL be rejected with reason `invalid_scale`

#### Scenario: Overflow
- **WHEN** a command would make any quantity exceed the int64 range
- **THEN** the command SHALL be rejected with reason `numeric_overflow` and state SHALL be unchanged

### Requirement: Cross-platform bit-identical results
The engine SHALL produce bit-identical events and digests on every supported target (native x86-64, native arm64, WebAssembly, Python binding, Swift interop). Any floating-point computation SHALL use only correctly rounded IEEE-754 operations (add, sub, mul, div, sqrt) and engine-provided kernels for transcendental functions, and SHALL be quantized to fixed-point before it affects state.

#### Scenario: Golden vector on all targets (SIM02)
- **WHEN** a golden fixture is replayed on every supported target
- **THEN** each target SHALL produce the same final state digest and the same event stream digest

### Requirement: Deterministic randomness
All randomness SHALL come from an engine-provided, versioned PRNG seeded from the run seed and a stable stream identifier. The engine SHALL NOT use platform random number distributions.

#### Scenario: Seeded reproducibility
- **WHEN** a synthetic scenario is run twice with the same seed and commands
- **THEN** both runs SHALL produce identical bars, events and digests
- **AND** a different seed SHALL produce a run with a different run identity

### Requirement: Commands and idempotency
Each command SHALL carry a sequence number, the decision step it was issued at, a type, a payload and a client command id. The engine SHALL reject commands with a non-increasing sequence number or a decision step different from the current one, and SHALL treat a repeated client command id as a no-op returning the original outcome.

#### Scenario: Duplicate command
- **WHEN** the same command (same client command id) is applied twice
- **THEN** only one order SHALL exist and the second application SHALL return the first result

### Requirement: Checkpoints and digest
After every step the engine SHALL be able to serialize a checkpoint from which replay continues identically, and SHALL compute a state digest (SHA-256 over a canonical serialization) covering positions, orders, balances, margin, open option positions, RNG state and step index.

#### Scenario: Resume from checkpoint
- **WHEN** a run is stopped at step `k`, restored from its checkpoint and continued with the remaining commands
- **THEN** its final digest SHALL equal the digest of an uninterrupted run

### Requirement: Versioning
The engine SHALL report its semantic version and an execution-policy version. A run SHALL record both, and the engine SHALL refuse to resume a checkpoint produced by an incompatible execution-policy version.

#### Scenario: Incompatible checkpoint
- **WHEN** a checkpoint from execution-policy version 1 is loaded by an engine that only supports version 2
- **THEN** loading SHALL fail with reason `incompatible_policy_version` and no state SHALL be produced
