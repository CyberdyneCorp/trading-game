#pragma once

// Pack manifest schema v1 and v2 (spec scenario-packs "Pack manifest"). Field reference: engine/README.md
// "Scenario packs". v2 adds margin tiers, mission objectives and limits, mark-price bars and settlement
// sub-bars; a v1 manifest that names any of them is refused (invalid_field on the first one). Decimals are strings of scaled integers; counts are JSON integers.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "json_in.hpp"
#include "tradelab/scenario.hpp"

namespace tradelab::detail {

struct ManifestFile {
  std::string name;
  std::uint64_t size = 0;
  std::uint64_t decompressed_size = 0;
  std::string sha256;  // lowercase hex of the stored bytes
};

struct ManifestLicense {
  std::string name;
  std::string attribution;
  bool transformation = false;
  bool commercial = false;
  bool offline_redistribution = false;
};

struct Manifest {
  std::uint32_t schema_version = 0;
  std::string scenario_id;
  std::uint32_t pack_version = 0;
  std::string mode;
  bool private_pack = false;
  std::string engine_min;
  std::string engine_max;
  std::uint32_t execution_policy = 0;
  std::uint32_t scoring_version = 0;
  std::string timeframe;
  std::uint32_t step_seconds = 0;
  std::uint32_t display_precision = 0;
  std::string alias_policy;
  ContractSpec contract;
  CostModel costs;
  std::string funding_source;  // "file" (funding.bin) or "none" (zero rates)
  std::string iv_kind;         // "realized", "none" or "file" (an IV column in options.bin)
  bool has_options = false;    // instruments.options present: options.bin carries the series
  OptionSpec options;          // parameters only; series and IV come from options.bin
  MissionRules mission;
  std::uint32_t bars = 0;
  std::uint32_t gaps = 0;
  // data.gapPolicy (optional, policy v17): "skip" or "carryForward" when gaps are filled with
  // continuity bars; bars.bin then has a sixth column (1 = continuity bar). Empty = absent.
  std::string gap_policy;
  std::uint32_t events = 0;            // rows of the news table in events.bin
  std::uint32_t liquidity_shocks = 0;  // rows of the liquidity table in events.bin
  std::uint32_t option_series = 0;     // rows of the series table in options.bin
  std::optional<std::uint32_t> branch_step;  // mode "branch": bars after it are synthetic
  // Schema v2: contract.margin_tiers (marginModel.tiers) and the mission's objectives and limits are
  // read into `contract` and `mission`; mark.bin holds one mark bar per bar when `mark_bars`, and
  // subbars.bin `sub_bars` rows of settlement sub-bar closes.
  bool mark_bars = false;
  std::uint32_t sub_bars = 0;
  std::string first_v2_field;  // dotted path of the first v2-only field present, empty when none
  std::vector<ManifestFile> files;
  std::string signing_key_id;
  ManifestLicense license;
  bool downloadable = false;
};

struct ManifestError {
  std::string reason;  // missing_field | invalid_field
  std::string field;   // dotted path
};

constexpr std::size_t kMaxManifestTiers = 16;
constexpr std::size_t kMaxManifestObjectives = 8;

// Reads and checks every required field; the first problem wins.
bool parse_manifest(const JsonValue& root, Manifest* out, ManifestError* err);

// "a.b.c" -> comparable triple; false when malformed.
bool parse_semver(const std::string& v, std::array<std::uint32_t, 3>* out);

}  // namespace tradelab::detail
