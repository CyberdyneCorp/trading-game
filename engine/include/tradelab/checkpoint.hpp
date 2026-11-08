#pragma once

// Run checkpoints (spec: simulation-core "Checkpoints and digest", "Versioning").
// A checkpoint is the complete RunState in the canonical binary encoding, prefixed with a header
// (magic, format version, execution-policy version, scenario id). Restoring and continuing with
// the remaining commands yields the same digests as an uninterrupted run.

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "tradelab/result.hpp"
#include "tradelab/state.hpp"

namespace tradelab {

constexpr std::uint32_t kCheckpointFormat = 6;  // v2: start options and advice (policy v11); v3: starting balance, leverage cap (policy v12); v4: percentage trailing rate (policy v16); v5: lesson answers and decision records (scoring v11); v6: time in force and OCO links on orders, the last link step (policy v18)

std::vector<std::uint8_t> save_checkpoint(const RunState& st, std::string_view scenario_id);

// Fails with IncompatiblePolicy for another execution-policy version, ScenarioMismatch for
// another scenario, TruncatedInput / NonCanonicalEncoding for damaged data.
Result<RunState> load_checkpoint(std::span<const std::uint8_t> data, std::string_view scenario_id);

}  // namespace tradelab
