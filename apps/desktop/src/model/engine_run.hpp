// Engine adapter: one mission run through the engine's C ABI (tradelab/c_api.h, linked statically from
// tradelab::engine), the same surface the web (WASM) and Python use. Not thread-safe: EngineHost's
// worker is the only caller (the C ABI's returned strings and run-less buffers are not shared).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/game_command.hpp"
#include "model/json.hpp"
#include "model/snapshot.hpp"

struct tl_run;

namespace tradelab::desktop {

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

// The built-in missions (tl_missions), in engine order.
std::vector<MissionInfo> engine_missions();

// Builds a snapshot from tl_state JSON (and tl_debrief JSON once the run has ended).
Snapshot snapshot_from_state(const json::Value& state);
Debrief debrief_from_json(const json::Value& debrief);

class EngineRun {
 public:
  // nullptr for an unknown mission id.
  // Start options (tl_run_create_ex): advisor allowance, intel perks, campaign capital and leverage gate.
  static std::unique_ptr<EngineRun> create(const std::string& mission, std::uint32_t seed,
                                           const RunOptions& options = {});
  ~EngineRun();
  EngineRun(const EngineRun&) = delete;
  EngineRun& operator=(const EngineRun&) = delete;

  [[nodiscard]] std::uint32_t seed() const { return seed_; }
  [[nodiscard]] std::string state_json() const;
  [[nodiscard]] std::string debrief_json() const;
  // Advances up to `steps` bars; returns whether the run is still active afterwards.
  bool advance(int steps);
  // Market order; returns the engine's result {"accepted","reason",...}.
  CommandResult place_market(Side side, const std::string& qty);
  [[nodiscard]] Snapshot snapshot() const;

  // Report-only option analysis (add-academy-and-strategies design D4): none changes the run, its log or
  // digests. The run-less valuation (Academy lab) answers in the engine's shared buffer, so like every
  // engine call these run on EngineHost's worker only.
  static std::string option_whatif(const std::string& request_json);
  [[nodiscard]] std::string run_whatif(const std::string& request_json) const;
  [[nodiscard]] std::string combo_preview(const std::string& legs_json) const;

  // Storage (model/storage): takes ownership of a run built by another tl_* constructor (pack, restore).
  // The mission's lesson questions are read as create() does.
  static std::unique_ptr<EngineRun> adopt(tl_run* run, std::uint32_t seed);
  [[nodiscard]] tl_run* handle() const { return run_; }
  // Client ids: one per non-advance command, 1, 2, ... (a replay hands out the same ids).
  std::uint32_t take_client_id() { return next_client_id_++; }
  void set_next_client_id(std::uint32_t id) { next_client_id_ = id; }
  // A run on a pack with its own mission texts carries them in every snapshot.
  void set_pack_texts(std::vector<PackText> texts) { pack_texts_ = std::move(texts); }

 private:
  EngineRun(tl_run* run, std::uint32_t seed) : run_(run), seed_(seed) {}
  void load_lesson(const std::string& mission);
  tl_run* run_;
  std::uint32_t seed_;
  LessonInfo lesson_;
  std::uint32_t next_client_id_ = 1;
  std::vector<PackText> pack_texts_;
};

}  // namespace tradelab::desktop
