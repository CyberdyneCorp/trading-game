// AppViewModel's Academy (add-academy-and-strategies tasks D3, D6, D7, D8; ports the matching parts of
// the web's session.svelte.ts, LessonRunner.svelte, LessonTicket.svelte, Academy.svelte,
// ReviewPrompt.svelte, Glossary.svelte and CoachMarks.svelte): lessons opened from the curriculum map,
// the Academy screen or the debrief (and back there), their ticket sandbox and typed answers, the
// completion that schedules a spaced review, the review prompt, the glossary, and the guided first trade
// driven by the screens it sees, a typed stop and an advance.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "model/content.hpp"
#include "viewmodel/academy/glossary.hpp"
#include "viewmodel/academy/review.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/curriculum.hpp"

namespace tradelab::desktop {

namespace {

// A sandbox number as the web's String(n) writes it, with the locale's decimal mark.
std::string plain_number(double v, Locale locale) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.10g", v);
  std::string s = buf;
  if (locale == Locale::PtBR) std::ranges::replace(s, '.', ',');
  return s;
}

std::string optional_text(const std::optional<double>& v, Locale locale) { return v ? plain_number(*v, locale) : std::string(); }

const TicketWidget* ticket_of(const LessonStep* step) { return step == nullptr ? nullptr : std::get_if<TicketWidget>(&step->widget); }

std::vector<std::string> lesson_order() {
  std::vector<std::string> ids;
  for (const auto& l : web_content().lessons) ids.push_back(l.id);
  return ids;
}

std::vector<std::string> academy_lessons() {
  std::vector<std::string> ids;
  for (const auto& s : web_content().stages) ids.insert(ids.end(), s.lessons.begin(), s.lessons.end());
  return ids;
}

}  // namespace

// ---- Lessons ----

void AppViewModel::open_lesson(const std::string& id, Screen from, LessonMode mode) {
  shell_.lesson = LessonRunner(web_content().lesson(id), mode);
  shell_.academy.lesson_return = static_cast<std::uint8_t>(from);
  reset_sandbox();
  screen_ = Screen::Lesson;
  touch();
}

// A fresh sandbox (and typed answer) for every step that shows one.
void AppViewModel::reset_sandbox() {
  AcademyState& a = shell_.academy;
  a.typed.clear();
  reset_lesson_lab();
  const TicketWidget* w = ticket_of(shell_.lesson.step());
  if (w == nullptr) {
    a.sandbox.reset();
    return;
  }
  a.sandbox = academy::initial_inputs(*w);
  const Locale L = catalog_.locale();
  a.sandbox_text = {plain_number(a.sandbox->qty, L), std::to_string(a.sandbox->leverage), optional_text(a.sandbox->stop, L),
                    optional_text(a.sandbox->target, L)};
}

void AppViewModel::lesson_next() {
  if (!shell_.lesson.next()) return;
  reset_sandbox();
  touch();
}

void AppViewModel::lesson_prev() {
  if (!shell_.lesson.prev()) return;
  reset_sandbox();
  touch();
}

void AppViewModel::lesson_leave() {
  screen_ = static_cast<Screen>(shell_.academy.lesson_return);
  touch();
}

// A full lesson is completed (passing a lesson-only stage) and scheduled for review; a review moves
// the schedule by its first-try answers. Then back to where the lesson was opened.
void AppViewModel::lesson_finish() {
  const LessonRunner& r = shell_.lesson;
  const AcademyLesson* l = r.lesson();
  if (l == nullptr || !r.finished()) return;
  Progress& p = shell_.progress;
  const std::string day = today();
  if (r.mode() == LessonMode::Review) {
    p.lesson_reviews = academy::apply_review(p.lesson_reviews, l->id, r.all_right_first_try(), day);
  } else {
    if (!contains(p.lessons_done, l->id)) p.lessons_done.push_back(l->id);
    p.lesson_reviews = academy::schedule_on_complete(p.lesson_reviews, l->id, day);
  }
  save_progress();
  lesson_leave();
}

void AppViewModel::lesson_set_typed(std::string text) {
  if (text == shell_.academy.typed) return;
  shell_.academy.typed = std::move(text);
  touch();
}

void AppViewModel::lesson_submit_number() {
  if (shell_.lesson.submit_number(shell_.academy.typed, catalog_.locale())) touch();
}

void AppViewModel::lesson_check_set() {
  const LessonStep* step = shell_.lesson.step();
  const TicketWidget* w = ticket_of(step);
  const auto& x = shell_.academy.sandbox;
  if (w == nullptr || !x || !step->question) return;
  const auto out = academy::sandbox_outputs(*w, *x);
  if (shell_.lesson.check_set(academy::sandbox_value(step->question->field, *x, out))) touch();
}

void AppViewModel::sandbox_set_side(bool buy) {
  const TicketWidget* w = ticket_of(shell_.lesson.step());
  auto& x = shell_.academy.sandbox;
  if (w == nullptr || !x || !w->editable("side") || x->buy == buy) return;
  x->buy = buy;
  touch();
}

// The typed text is kept as typed; the sandbox reads it with either decimal mark (an empty or invalid
// quantity is 0, leverage 1, an empty stop or target is none: LessonTicket.svelte).
void AppViewModel::sandbox_set_text(SandboxField field, std::string text) {
  static constexpr std::array<std::string_view, 4> kFields = {"qty", "leverage", "stop", "target"};
  const TicketWidget* w = ticket_of(shell_.lesson.step());
  auto& x = shell_.academy.sandbox;
  if (w == nullptr || !x || !w->editable(kFields[static_cast<std::size_t>(field)])) return;
  const double v = fmt::parse_decimal(text);
  const std::optional<double> value = std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
  AcademyState& a = shell_.academy;
  switch (field) {
    case SandboxField::Qty: x->qty = value.value_or(0); break;
    case SandboxField::Leverage: x->leverage = value && *value >= 1 ? static_cast<int>(*value) : 1; break;
    case SandboxField::Stop: x->stop = value; break;
    case SandboxField::Target: x->target = value; break;
  }
  a.sandbox_text[static_cast<std::size_t>(field)] = std::move(text);
  touch();
}

std::optional<academy::SandboxOutputs> AppViewModel::sandbox_outputs() const {
  const TicketWidget* w = ticket_of(shell_.lesson.step());
  const auto& x = shell_.academy.sandbox;
  if (w == nullptr || !x) return std::nullopt;
  return academy::sandbox_outputs(*w, *x);
}

// ---- Academy screen and the review prompt ----

void AppViewModel::open_academy() {
  screen_ = Screen::Academy;
  touch();
}

void AppViewModel::academy_select(int index) {
  if (index == shell_.academy.academy_selected) return;
  shell_.academy.academy_selected = index;
  touch();
}

void AppViewModel::academy_start_selected() {
  const auto ids = academy_lessons();
  const int i = shell_.academy.academy_selected;
  if (i >= 0 && i < static_cast<int>(ids.size())) open_lesson(ids[static_cast<std::size_t>(i)], Screen::Academy);
}

void AppViewModel::academy_due_select(int index) {
  if (index == shell_.academy.due_selected) return;
  shell_.academy.due_selected = index;
  touch();
}

void AppViewModel::academy_review_selected() {
  const auto due = reviews_due();
  const int i = shell_.academy.due_selected;
  if (i >= 0 && i < static_cast<int>(due.size())) open_lesson(due[static_cast<std::size_t>(i)], Screen::Academy, LessonMode::Review);
}

std::string AppViewModel::today() const { return academy::local_day(shell_.now()); }

std::vector<std::string> AppViewModel::reviews_due() const {
  return academy::due_reviews(shell_.progress.lesson_reviews, today(), lesson_order());
}

bool AppViewModel::review_prompt_shown() const {
  return !reviews_due().empty() && shell_.settings.review_dismissed_on != today();
}

// The dismissal lasts until the next calendar day (web ReviewPrompt.svelte).
void AppViewModel::dismiss_review_prompt() {
  shell_.settings.review_dismissed_on = today();
  save_settings();
  touch();
}

std::string AppViewModel::debrief_lesson() const {
  if (!snap_ || !snap_->debrief) return {};
  const WebContent& c = web_content();
  const std::string stage = lesson_code_stage(c, snap_->debrief->raw["lesson"].as_string());
  return stage.empty() ? std::string() : recommended_lesson(c, stage, shell_.progress.lessons_done);
}

void AppViewModel::open_debrief_lesson() {
  const std::string id = debrief_lesson();
  if (!id.empty() && screen_ == Screen::Debrief) open_lesson(id, Screen::Debrief);
}

// ---- Glossary ----

void AppViewModel::open_glossary(std::optional<std::string> term) {
  AcademyState& a = shell_.academy;
  if (screen_ != Screen::Glossary) a.glossary_return = static_cast<std::uint8_t>(screen_);
  screen_ = Screen::Glossary;
  if (term) {
    a.glossary_query.clear();
    glossary_open(*term);
  }
  touch();
}

void AppViewModel::leave_glossary() {
  screen_ = static_cast<Screen>(shell_.academy.glossary_return);
  touch();
}

void AppViewModel::glossary_search(std::string query) {
  if (query == shell_.academy.glossary_query) return;
  shell_.academy.glossary_query = std::move(query);
  touch();
}

std::vector<std::string> AppViewModel::glossary_hits() const {
  return academy::search_terms(web_content(), catalog_, shell_.academy.glossary_query);
}

void AppViewModel::glossary_select(int index) {
  const auto hits = glossary_hits();
  if (index >= 0 && index < static_cast<int>(hits.size())) glossary_open(hits[static_cast<std::size_t>(index)]);
}

// Opening a term clears its "new" mark (progress v3 glossarySeen).
void AppViewModel::glossary_open(const std::string& id) {
  if (web_content().term(id) == nullptr) return;
  shell_.academy.glossary_term = id;
  if (!contains(shell_.progress.glossary_seen, id)) {
    shell_.progress.glossary_seen.push_back(id);
    save_progress();
  }
  touch();
}

// ---- Guided first trade ----

void AppViewModel::mark_coach_done() {
  const std::string tour(academy::kFirstTradeTour);
  if (contains(shell_.progress.coach_done, tour)) return;
  shell_.progress.coach_done.push_back(tour);
  save_progress();
}

void AppViewModel::coach_next() {
  shell_.academy.coach.next();
  touch();
}

void AppViewModel::coach_skip() {
  shell_.academy.coach.skip();
  touch();
}

// From the help overlay the tour starts on this terminal now; from the briefing, when it opens.
void AppViewModel::coach_replay() {
  academy::CoachModel& coach = shell_.academy.coach;
  if (screen_ == Screen::Game) {
    if (game_->modal() == game::Modal::Help) game_->close_modal();
    coach.start(shell_.run_id);
  } else {
    coach.requested = true;
  }
  touch();
}

void AppViewModel::tick_coach() {
  AcademyState& a = shell_.academy;
  academy::CoachModel& coach = a.coach;
  if (coach.count() == 0 && !web_content().coach.empty()) {
    coach.set_steps(web_content().coach);
    coach.on_done = [this] { mark_coach_done(); };
  }
  const auto before = coach.index();
  const auto last = static_cast<Screen>(a.coach_screen);
  if (last != screen_) {
    a.coach_screen = static_cast<std::uint8_t>(screen_);
    if (last == Screen::Game) coach.left_terminal(screen_ == Screen::Debrief);
    if (screen_ == Screen::Game) {
      const bool first = shell_.run_campaign.has_value() && !shell_.run_assisted &&
                         !contains(shell_.progress.coach_done, std::string(academy::kFirstTradeTour));
      coach.maybe_start(shell_.run_id, first);
    }
    if (screen_ == Screen::Debrief) coach.debrief_opened(shell_.run_id);
  }
  if (screen_ == Screen::Game) coach_watch_terminal();
  if (coach.index() != before) touch();
}

// A typed stop or one advance completes their steps, like Next (web CoachMarks.svelte).
void AppViewModel::coach_watch_terminal() {
  AcademyState& a = shell_.academy;
  academy::CoachModel& coach = a.coach;
  const CoachStepDef* step = coach.step();
  const GameState* g = game_->state();
  if (step == nullptr || g == nullptr) {
    a.coach_seen = -1;
    return;
  }
  if (a.coach_seen != *coach.index()) {
    a.coach_seen = *coach.index();
    a.coach_stop = game_->ticket.stop;
    a.coach_bar = g->step;
    return;
  }
  const bool typed = step->completes == "stop" && game_->ticket.stop != a.coach_stop && !game_->ticket.stop.empty();
  const bool advanced = step->completes == "advance" && g->step != a.coach_bar;
  if (typed || advanced) coach.next();
}

}  // namespace tradelab::desktop
