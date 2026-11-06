// State of the shell screens kept by AppViewModel (menu panels, campaign, curriculum map, journal,
// Academy lessons, settings), apart from the run in play. Plain data; the rules live in shell.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "model/progress.hpp"
#include "viewmodel/keymap.hpp"
#include "viewmodel/lesson_runner.hpp"

namespace tradelab::desktop {

// What the CRT overlay draws: everything off without the master toggle or under reduced motion.
struct CrtState {
  bool overlay = false;
  bool scanlines = false;
  bool bloom = false;
  bool noise = false;
  bool operator==(const CrtState&) const = default;
};
CrtState effective_crt(const CrtSettings& s, bool reduce_motion);

// Text scale steps offered in Settings (Raven scales every text and control with it).
inline constexpr float kTextScaleMin = 1.0f;
inline constexpr float kTextScaleMax = 2.0f;
inline constexpr float kTextScaleStep = 0.25f;
float clamp_text_scale(float scale);

struct ShellState {
  MemoryProgressRepository own_progress;
  MemorySettingsRepository own_settings;
  ProgressRepository* progress_repo = &own_progress;
  SettingsRepository* settings_repo = &own_settings;
  Progress progress;
  Settings settings;
  Keymap keymap;
  bool reduce_motion = false;
  std::function<std::int64_t()> now;  // ms since the epoch (fixed in tests)

  // The run being started or played.
  std::string run_id;
  std::string run_mission;
  std::optional<CampaignLink> run_campaign;
  std::optional<AssistedLink> run_assisted;  // an assisted practice attempt: never counts for progress
  std::string run_pack;                      // digest of the pack it plays ("" = built-in mission)
  bool run_settled = false;
  int run_counter = 0;
  // A start or rewind on its way to the engine: the links and journal of the attempt it makes.
  std::optional<AssistedLink> pending_assisted;
  std::string pending_pack;
  std::vector<PlanEntry> pending_journal;
  bool rewind_pending = false;

  int tutorial_index = 0;
  std::string assist_turn = "1";

  bool confirming_end = false;  // campaign: the second, explicit click
  int journal_filter = 0;       // 0 = every mission, else journal_missions()[i - 1]
  int lesson_selected = 0;
  LessonRunner lesson;
  int key_row = 0;
  bool capturing_key = false;
  std::string key_message;
  bool key_message_ok = true;
};

}  // namespace tradelab::desktop
