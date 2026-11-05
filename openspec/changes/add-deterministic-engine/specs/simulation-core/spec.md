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
Commands SHALL be applied in sequence order at the current decision point, before the step is advanced. For each advanced step the engine SHALL then perform, in this order: process the opening price (liquidation through a gap, gap-triggered stops and limits, then market orders) and execute option orders at the open; evaluate the bar using the intrabar policy (adverse exits and liquidation first, then favorable exits, then intrabar entries followed by an adverse check of the new position), executing fills and charging costs as they occur; at the close apply funding if the step is a funding boundary, liquidate at the close if required, settle expiring options and enforce option maintenance margin, ratchet trailing references, update metrics, and finish the run or declare bankruptcy. News items are scenario data published at their release step, so no release phase is needed. Indicators are derived from closed bars on demand, and a checkpoint SHALL be obtainable after any step.

#### Scenario: Sequence is observable
- **WHEN** a step contains a gap, a stop trigger, a funding boundary and an option expiry
- **THEN** the emitted events SHALL appear in the order defined by this requirement with strictly increasing event sequence numbers

### Requirement: No lookahead
State, indicators, option prices, liquidation thresholds and any data exposed to the player at decision step `t` SHALL depend only on scenario data with timestamps up to and including the close of bar `t` and on events released at or before `t`.

#### Scenario: Future data mutation (SIM01)
- **WHEN** all scenario data after bar `t` is replaced by arbitrary valid data
- **THEN** every event, indicator value, option quote and score component emitted up to step `t` SHALL be unchanged

### Requirement: Fixed-point numerics
Prices, quantities, money, fees, funding rates, margin and option premiums SHALL be represented as signed 64-bit integers with fixed scales per quantity type (price, quantity, money and volatility 1e-8; rates 1e-10), versioned with the execution policy. Values SHALL enter the engine as raw fixed-point integers or through a strict decimal parser that rejects any value finer than the scale, an exponent or whitespace, never rounding silently. Scenario validation SHALL reject non-positive tick or lot sizes, bar prices off the tick grid and minimum quantities off the lot grid. Every rounding SHALL use a rule declared per call site (e.g. fees round up against the player, liquidation prices round toward earlier liquidation). Overflow SHALL be detected and reported as an engine error, never wrapped silently.

#### Scenario: Fee rounding
- **WHEN** a fee computes to a value between two representable units
- **THEN** it SHALL be rounded toward the direction that costs the player more

#### Scenario: Value finer than engine scale
- **WHEN** a price is given as a decimal with more fractional digits than the price scale (e.g. `1.123456789`)
- **THEN** parsing SHALL fail with `InvalidDecimal` and no rounded value SHALL be produced

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
The engine SHALL report its semantic version and an execution-policy version. A checkpoint SHALL record the execution-policy version and the scenario id, and the engine SHALL refuse to resume a checkpoint produced by another execution-policy version or for another scenario.

#### Scenario: Incompatible checkpoint
- **WHEN** a checkpoint from another execution-policy version is loaded
- **THEN** loading SHALL fail with `IncompatiblePolicy` (the C ABI restore returns no run) and no state SHALL be produced

### Requirement: Interface definition
The enumerations shared by state, commands, events and the debrief, and every JSON shape the C ABI returns, SHALL be declared once in an IDL (`engine/idl/tradelab.idl`). The engine's C++ enumerations with their JSON names, and the TypeScript types of the web client, SHALL be generated from it, and CI SHALL fail when a generated file differs from what the IDL produces. The C ABI's output SHALL conform to the IDL: exactly the declared keys (optional ones may be absent), the declared value types, only declared enumeration names and decimal strings for fixed-point values.

#### Scenario: Stale generated types
- **WHEN** the IDL changes and the generated TypeScript types are not regenerated
- **THEN** the CI check SHALL fail and name the stale file

#### Scenario: Engine output drifts from the IDL
- **WHEN** the C ABI emits a key the IDL does not declare, omits a required one, or emits an undeclared enumeration name
- **THEN** the conformance test SHALL fail

### Requirement: Browser module size budget
The browser engine module (single-file ES module with the WebAssembly embedded) SHALL stay below 400 KB gzipped, checked in CI on every build.

#### Scenario: Module over budget
- **WHEN** an engine change makes the gzipped module 400 KB or larger
- **THEN** the web CI job SHALL fail
