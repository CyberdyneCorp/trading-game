#pragma once

// The shared action format (bindings/python/tradelab_engine/replay.py; the golden fixtures and run
// files) applied through the C ABI, and the schema of a run file (design D1/D2 of
// add-run-sharing-and-journal-stats). Internal: the C ABI entry point is tl_run_file_verify.

#include <cstdint>
#include <string>
#include <string_view>

#include "json_in.hpp"
#include "tradelab/c_api.h"

namespace tradelab::detail {

// Limits of a run file (design D1, D2).
inline constexpr std::size_t kRunFileMaxBytes = std::size_t{1} << 20U;
inline constexpr std::size_t kRunFileMaxActions = 20'000;
inline constexpr std::size_t kRunFileMaxJournal = 500;
inline constexpr std::int64_t kMaxAdvance = 10'000;

// The reader limits a run file is parsed with.
JsonLimits run_file_limits();

// True for {"k":"adv"}: the caller advances `n` bars and assigns no client id.
bool is_advance(const JsonValue& action);

// Applies one non-advance action with client id `cid`. Returns "" when the engine accepted it,
// otherwise the rejection reason ("unknown_action_kind" for a kind the format does not have).
std::string apply_action(tl_run* run, std::uint32_t cid, const JsonValue& action);

// The first problem of a parsed run file against the version 1 schema: `reason` is empty when the
// file is valid. Reasons: not_a_run_file, unsupported_version, invalid_field (with `path`, e.g.
// "actions[12].qty").
struct RunFileProblem {
  std::string reason;
  std::string path;
};
RunFileProblem check_run_file(const JsonValue& root);

}  // namespace tradelab::detail
