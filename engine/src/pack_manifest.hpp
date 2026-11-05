#pragma once

// Pack manifest schema v1 (spec scenario-packs "Pack manifest"). Field reference: engine/README.md
// "Scenario packs". Decimals are strings of scaled integers; counts are JSON integers.

#include <array>
#include <cstdint>
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
  std::string iv_kind;         // "realized" or "none" (packs carry no option series yet)
  MissionRules mission;
  std::uint32_t bars = 0;
  std::uint32_t gaps = 0;
  std::vector<ManifestFile> files;
  std::string signing_key_id;
  ManifestLicense license;
  bool downloadable = false;
};

struct ManifestError {
  std::string reason;  // missing_field | invalid_field
  std::string field;   // dotted path
};

// Reads and checks every required field; the first problem wins.
bool parse_manifest(const JsonValue& root, Manifest* out, ManifestError* err);

// "a.b.c" -> comparable triple; false when malformed.
bool parse_semver(const std::string& v, std::array<std::uint32_t, 3>* out);

}  // namespace tradelab::detail
