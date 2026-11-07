// Port of apps/web/src/lib/game/coach.svelte.ts CoachModel: the guided first trade (design D6,
// guided-first-trade spec). The tour runs on the real terminal of one run: the steps before the
// debrief's there, the last when that run's debrief opens. It starts by itself on the first campaign
// terminal of the device, or when the player asks for it ("Guided tour" in the help overlay or the
// briefing). Skipping, or finishing the terminal part, marks it done on the device; leaving the
// terminal for anything but that run's debrief ends it without marking it.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "model/content.hpp"

namespace tradelab::desktop::academy {

inline constexpr std::string_view kFirstTradeTour = "first-trade";

struct Box {
  float x = 0;
  float y = 0;
  float w = 0;
  float h = 0;
  bool operator==(const Box&) const = default;
};

// Where the callout goes (CoachMarks.svelte place_): below its target when it fits, else above, else
// over the target's bottom edge; always fully inside `area` with an 8 px margin. No target: centered.
Box callout_box(const std::optional<Box>& target, float width, float height, const Box& area);

class CoachModel {
 public:
  // Steps of coach.json in order (content.json `coach`).
  void set_steps(std::vector<CoachStepDef> steps);

  [[nodiscard]] bool active() const { return index_.has_value(); }
  [[nodiscard]] std::optional<int> index() const { return index_; }
  [[nodiscard]] const CoachStepDef* step() const;
  [[nodiscard]] int count() const { return static_cast<int>(steps_.size()); }
  // Steps shown in the terminal (all but the debrief's).
  [[nodiscard]] int terminal_steps() const;
  [[nodiscard]] bool waiting_for_debrief() const { return index_ == terminal_steps(); }
  [[nodiscard]] bool last() const { return index_ == count() - 1; }
  [[nodiscard]] const std::string& run_id() const { return run_id_; }

  // "Guided tour" was chosen on the briefing or in the help: start when the terminal opens.
  bool requested = false;
  // Called once when the tour counts as done (skipped, or the terminal part finished).
  std::function<void()> on_done;

  void start(const std::string& run_id);
  // On a terminal that just opened: start when asked to, or on a first campaign terminal.
  void maybe_start(const std::string& run_id, bool first_campaign_terminal);
  void next();
  void skip();
  // The terminal closed: to this run's debrief the tour jumps to its debrief step; anywhere else it ends.
  void left_terminal(bool to_debrief);
  // A debrief opened: the waiting step shows only for the tour's own run.
  void debrief_opened(const std::string& run_id);

 private:
  void done();

  std::vector<CoachStepDef> steps_;
  std::optional<int> index_;
  std::string run_id_;
};

}  // namespace tradelab::desktop::academy
