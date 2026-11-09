// Port of apps/web/src/lib/game/drills.ts (names kept; web parity fixture drills.json): daily drills
// (add-practice-and-advanced-strategies D15). Six items in about two minutes, fed by the spaced review.
// Items come from the drill templates (resources/web/content.json "drills", texts drill.<id>.*) with
// mulberry32 seeded by FNV-1a 32 of "drill-v1:" + local date + ":" + session index; decimal parameters
// are integer multiples of their step, never floating-point products. Answers are the lesson check
// functions' (content/checks.ts) for the kinds the templates use, with the web's double arithmetic in
// the same order and its rounding (Math.round is half up: floor(x + 0.5)).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "model/content.hpp"
#include "model/i18n.hpp"
#include "model/progress.hpp"
#include "viewmodel/practice/prng.hpp"

namespace tradelab::desktop::practice {

inline constexpr int kDrillItems = 6;
inline constexpr int kDrillSeconds = 120;     // the soft timer: shown, never failing
inline constexpr int kDrillFromLessons = 3;   // at most this many items from lessons whose review is due

struct DrillItem {
  std::string template_id;
  std::string skill;
  std::vector<std::string> lessons;
  // Parameters as canonical decimal strings, in the template's order.
  std::vector<std::pair<std::string, std::string>> params;
  double answer = 0;  // NaN when the check cannot compute it; a choice index for choice items
  int digits = 2;
  double tolerance = 0.005;
  bool choice = false;

  [[nodiscard]] std::string param(std::string_view name) const;
};

// The template of an item or an id (nullptr when the content has none).
const DrillTemplate* drill_template(const WebContent& content, std::string_view id);
std::vector<const DrillTemplate*> templates_of(const WebContent& content, std::string_view skill);
// Skills whose templates review one of these lessons, in DRILL_SKILLS order.
std::vector<std::string> skills_for_lessons(const WebContent& content, const std::vector<std::string>& lessons);

// Draws a template's parameters in its order (sums and differences draw nothing).
std::vector<std::pair<std::string, std::string>> draw_params(const DrillTemplate& t, Mulberry32& next);
// The right answer for these parameters (NaN when the check cannot compute it).
double answer_of(const DrillTemplate& t, const std::vector<std::pair<std::string, std::string>>& params);
DrillItem make_item(const DrillTemplate& t, Mulberry32& next);

// An answer as typed: a number with a comma or a dot; NaN otherwise.
double parse_answer(std::string_view text);
bool is_right(const DrillItem& item, double given);

// The prompt, explanation and choices of an item in the catalog's locale, its numbers formatted for it.
struct DrillText {
  std::string prompt;
  std::string explain;
  std::vector<std::string> choices;
  std::string answer;
};
DrillText item_text(const DrillItem& item, const DrillTemplate& t, const Catalog& catalog);

// First-try results per skill over the kept sessions.
SkillMap skill_stats(const std::vector<DrillSession>& history);
std::uint32_t drill_seed(const std::string& date, int session);
// The day's next session index: the sessions already kept for that date.
int session_index(const std::vector<DrillSession>& history, const std::string& date);

struct SessionInput {
  std::string date;
  std::int64_t now = 0;  // ms since the epoch
  int session = 0;
  LessonReviews lesson_reviews;
  std::map<std::string, DrillReview, std::less<>> drill_reviews;
  std::vector<DrillSession> history;
};

// The skills of a session, in order: up to 3 tied to lessons whose review is due, then skills due in the
// drill schedule (earliest first), then the lowest first-try rate (unseen 50%), ties in the drawn `rank`.
std::vector<std::string> choose_skills(const WebContent& content, const SessionInput& input, const std::vector<std::uint32_t>& rank);
// A session's items: the skills' order drawn first (one output per skill), then each item's template and
// parameters.
std::vector<DrillItem> build_session(const WebContent& content, const SessionInput& input);

enum class ItemResult : std::uint8_t { Right, Wrong, Skipped };

// Midnight (local) of a day, as epoch ms.
std::int64_t day_start(const std::string& day);
// All right on the first try moves a skill up a box (1, 3, 7, 21 days; unseen = box 1); any wrong sends
// it to box 1; skipped items change nothing.
std::map<std::string, DrillReview, std::less<>> schedule_skills(std::map<std::string, DrillReview, std::less<>> reviews,
                                                                const std::vector<DrillItem>& items,
                                                                const std::vector<ItemResult>& results, const std::string& date);
DrillSession session_record(const std::vector<DrillItem>& items, const std::vector<ItemResult>& results, const std::string& date);
// Progress after a finished session (drill answers never complete a lesson review).
Progress finish_session(Progress p, const std::vector<DrillItem>& items, const std::vector<ItemResult>& results, const std::string& date);

}  // namespace tradelab::desktop::practice
