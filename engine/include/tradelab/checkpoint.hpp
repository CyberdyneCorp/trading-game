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

constexpr std::uint32_t kCheckpointFormat = 2;  // v2: start options and advice (policy v11)

std::vector<std::uint8_t> save_checkpoint(const RunState& st, std::string_view scenario_id);

// Fails with IncompatiblePolicy for another execution-policy version, ScenarioMismatch for
// another scenario, TruncatedInput / NonCanonicalEncoding for damaged data.
Result<RunState> load_checkpoint(std::span<const std::uint8_t> data, std::string_view scenario_id);

}  // namespace tradelab
