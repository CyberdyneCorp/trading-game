// Terminal dialogs (web PlanDialog, CalibrationDialog, LessonQuiz, ConfirmDialog, HelpOverlay +
// ShortcutSettings, Tutorial, RunJournal, PatternSheet card): their state and texts. Opening, closing
// and the commands they lead to are in GameViewModel.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/game_command.hpp"
#include "model/game_state.hpp"
#include "viewmodel/game/shortcuts.hpp"
#include "viewmodel/game/sizing.hpp"
#include "viewmodel/game/texts.hpp"

namespace tradelab::desktop::game {

// A recorded plan (web analysis.ts PlanEntry); the free text stays on the device.
struct PlanEntry {
  int step = 0;
  bool buy = true;
  std::string thesis;
  std::optional<double> invalidation;
  std::optional<double> target;
  int confidence = 0;
  std::optional<double> planned_risk;
  bool skipped = false;
  std::string lose_scenario;
  std::string instrument;  // option series label; "" = perpetual
};

// The planned risk of the open perpetual position: the last perpetual plan's (journal.ts).
std::optional<double> position_planned_risk(const std::vector<PlanEntry>& journal);
std::vector<std::string> journal_lines(const Texts& t, const std::vector<PlanEntry>& journal);

class PlanDialog {
 public:
  std::string thesis;
  std::string invalidation;
  std::string target;
  std::string lose_scenario;
  int confidence = 60;  // 50..95 by 5

  void open(bool buy, std::string qty, int leverage, std::optional<double> planned_risk, const std::string& stop,
            const std::string& take_profit, std::optional<std::string> option_label);
  [[nodiscard]] bool can_save() const;
  [[nodiscard]] std::string headline(const Texts& t) const;
  [[nodiscard]] bool option() const { return option_label_.has_value(); }
  // The entry and the engine command (free text never reaches the engine).
  [[nodiscard]] PlanEntry entry(int step, bool skipped) const;
  [[nodiscard]] GameCommand command(const PlanEntry& e) const;

 private:
  bool buy_ = true;
  std::string qty_;
  int leverage_ = 1;
  std::optional<double> risk_;
  std::optional<std::string> option_label_;
};

class CalibrationDialog {
 public:
  int pct = 50;  // 5..95 by 5
  [[nodiscard]] std::string question(const Texts& t, const GameState& g) const;
  [[nodiscard]] static GameCommand answer(std::optional<int> pct);
};

struct LessonQuestionView {
  int index = 0;
  std::string id;
  std::string prompt;
  std::vector<std::string> choices;  // empty: numeric
  std::string unit;
  bool answered = false;
  bool invalid = false;
};

class LessonDialog {
 public:
  // Questions the catalog can show (texts whose option count matches the engine's).
  [[nodiscard]] std::vector<LessonQuestionView> questions(const Texts& t, const LessonInfo& info) const;
  std::vector<int> answered;
  std::vector<std::pair<int, int>> choice;          // question index -> chosen option
  std::vector<std::pair<int, std::string>> typed;   // question index -> typed number
  std::vector<int> invalid;
  void choose(int question, int option);
  void type(int question, std::string text);
  // The engine command for an answer, or nullopt (nothing chosen, or an invalid number, now flagged).
  std::optional<GameCommand> answer(const LessonInfo& info, int question);
  static GameCommand skip(int question);
};

// A numeric answer as typed -> the engine decimal ("0.50000000"), or nullopt when invalid.
std::optional<std::string> numeric_answer(const std::string& input);

struct TutorialCard {
  std::string title;
  std::string body;
  std::string art;
};
std::vector<TutorialCard> tutorial_cards(const Texts& t, const std::string& mission);

struct HelpRow {
  Action action = Action::Advance;
  std::string key;
  std::string label;
};
std::vector<HelpRow> help_rows(const Texts& t, const Keymap& map);

// Key capture for remapping (ShortcutSettings): pick an action, press its new key.
class Remap {
 public:
  std::optional<Action> waiting;
  std::string message;
  bool message_ok = false;
  void start(Action a);
  // A key arrived while waiting; Escape stops. Returns true when it was consumed.
  bool key(const Texts& t, Keymap& map, const std::string& key);
};

struct PatternCard {
  std::string name, what, means, confirm;
};
PatternCard pattern_card(const Texts& t, const std::string& id);

}  // namespace tradelab::desktop::game
