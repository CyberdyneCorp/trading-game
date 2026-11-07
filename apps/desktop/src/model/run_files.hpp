// Run files (port of apps/web/src/lib/game/runFile.ts; openspec add-run-sharing-and-journal-stats
// D1-D3): the file's members drafted from a run record and its action log, and the engine's run-less
// verification and sealing. The engine checks, replays and seals every file, so the web and the
// desktop accept and refuse the same files and write byte-identical canonical files for the same run.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "model/json.hpp"
#include "model/storage/run_store.hpp"

namespace tradelab::desktop {

// The engine's size limit (kRunFileMaxBytes).
inline constexpr std::size_t kRunFileMaxBytes = std::size_t{1} << 20U;

struct EngineStamp {
  int execution_policy = 0;
  int abi = 0;
  std::string version;
};

// A non-negative amount as the engine's decimal text (at most 8 decimals, trailing zeros dropped);
// nullopt for a non-number, a negative or a huge value. Same text as the web's decimalText.
std::optional<std::string> decimal_text(double v);

// The run file without "result" (only the engine writes it, tl_run_file_seal), as compact JSON.
// No run id, timestamps or device identity; plan notes (the record's journal) only when asked.
std::string run_file_draft(const storage::RunRecord& record, const std::vector<std::string>& actions, const EngineStamp& engine,
                           bool include_notes);

// Why a run cannot be exported (web exportBlock): "private_pack", "pack_missing", "no_actions"; nullopt when it can.
std::optional<std::string> export_block(const storage::RunRecord& record, std::size_t actions, const std::vector<storage::StoredPack>& packs);

// tl_run_file_verify / tl_run_file_seal: the parsed verdict (ok, reason, path, pack, detail, ...,
// run, fileDigest, debrief). `pack` and `keys_json` only for a catalog-pack run. Thread-safe.
json::Value verify_run_file(const std::string& text, std::span<const std::uint8_t> pack = {}, const std::string& keys_json = "[]");
json::Value seal_run_file(const std::string& text, std::span<const std::uint8_t> pack = {}, const std::string& keys_json = "[]");

// "tradelab-<mission>-<seed>.tlrun"
std::string run_file_name(const std::string& mission, std::uint32_t seed);

}  // namespace tradelab::desktop
