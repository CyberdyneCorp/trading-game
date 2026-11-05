#include "viewmodel/game/dialogs_vm.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

namespace {

std::string trim(const std::string& s) {
  const auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  const auto b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// A typed level in either locale ("95,56" or "95.56"); empty or unreadable means none.
std::optional<double> level(const std::string& v) {
  const double n = fmt::parse_decimal(v);
  if (std::isfinite(n) && n > 0) return n;
  return std::nullopt;
}

std::string two_decimals(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.2f", v);
  return buf;
}

template <class T>
T* find_pair(std::vector<std::pair<int, T>>& list, int key) {
  for (auto& [k, v] : list) {
    if (k == key) return &v;
  }
  return nullptr;
}

}  // namespace

std::optional<double> position_planned_risk(const std::vector<PlanEntry>& journal) {
  for (auto it = journal.rbegin(); it != journal.rend(); ++it) {
    if (!it->instrument.empty()) continue;
    if (it->planned_risk && *it->planned_risk > 0) return it->planned_risk;
    return std::nullopt;
  }
  return std::nullopt;
}

std::vector<std::string> journal_lines(const Texts& t, const std::vector<PlanEntry>& journal) {
  const Locale L = t.locale();
  std::vector<std::string> out;
  for (const auto& p : journal) {
    std::string line = "[" + t.tr("journal.bar", {{"step", std::to_string(p.step)}}) + "] " +
                       (p.buy ? "\xE2\x96\xB2 LONG" : "\xE2\x96\xBC SHORT") + (p.instrument.empty() ? "" : " \xC2\xB7 " + p.instrument);
    if (p.skipped) {
      out.push_back(line + " \xE2\x80\x94 " + t.tr("journal.skipped"));
      continue;
    }
    line += "  " + p.thesis + "  " + t.tr("journal.conf", {{"v", std::to_string(p.confidence)}});
    if (p.invalidation) line += " \xC2\xB7 " + t.tr("journal.inv", {{"v", fmt::fmt_num(*p.invalidation, L, 2)}});
    if (p.target) line += " \xC2\xB7 " + t.tr("journal.target", {{"v", fmt::fmt_num(*p.target, L, 2)}});
    if (p.planned_risk && *p.planned_risk > 0) line += " \xC2\xB7 " + t.tr("journal.risk", {{"v", fmt::fmt_money(*p.planned_risk, L)}});
    if (!p.lose_scenario.empty()) line += " \xC2\xB7 " + t.tr("journal.lose", {{"v", p.lose_scenario}});
    out.push_back(line);
  }
  return out;
}

void PlanDialog::open(bool buy, std::string qty, int leverage, std::optional<double> planned_risk, const std::string& stop,
                      const std::string& take_profit, std::optional<std::string> option_label) {
  buy_ = buy;
  qty_ = std::move(qty);
  leverage_ = leverage;
  risk_ = planned_risk;
  option_label_ = std::move(option_label);
  thesis.clear();
  lose_scenario.clear();
  invalidation = stop;
  target = take_profit;
  confidence = 60;
}

bool PlanDialog::can_save() const { return trim(thesis).size() >= 3; }

std::string PlanDialog::headline(const Texts& t) const {
  const Locale L = t.locale();
  const std::string risk = risk_ ? fmt::fmt_money(*risk_, L) : std::string("\xE2\x80\x94");
  return fmt::plan_headline(buy_, qty_, leverage_, option_label_, L) + " \xC2\xB7 " + t.tr("rail.plannedRisk") + ": " + risk;
}

PlanEntry PlanDialog::entry(int step, bool skipped) const {
  PlanEntry e;
  e.step = step;
  e.buy = buy_;
  e.skipped = skipped;
  e.planned_risk = risk_;
  e.instrument = option_label_.value_or("");
  if (skipped) return e;
  e.thesis = trim(thesis);
  e.invalidation = level(invalidation);
  e.target = level(target);
  e.confidence = confidence;
  if (option_label_) e.lose_scenario = trim(lose_scenario);
  return e;
}

GameCommand PlanDialog::command(const PlanEntry& e) const {
  const auto price = [](const std::optional<double>& v) { return v && *v > 0 ? two_decimals(*v) : std::string(); };
  const std::string risk = e.planned_risk && *e.planned_risk > 0 ? two_decimals(*e.planned_risk) : std::string();
  return GameCommand::plan(e.buy, e.skipped, !e.instrument.empty(), !e.lose_scenario.empty(), price(e.invalidation),
                           price(e.target), e.confidence, risk);
}

std::string CalibrationDialog::question(const Texts& t, const GameState& g) const {
  return t.tr("cal.question",
              {{"h", std::to_string(g.calibration.horizon)}, {"price", fmt::fmt_num(g.account.mark, t.locale(), 2)}});
}

GameCommand CalibrationDialog::answer(std::optional<int> pct) {
  return GameCommand::answer(pct ? *pct * 100 : 0, !pct.has_value());
}

std::vector<LessonQuestionView> LessonDialog::questions(const Texts& t, const LessonInfo& info) const {
  std::vector<LessonQuestionView> out;
  for (std::size_t i = 0; i < info.questions.size(); ++i) {
    const LessonQuestion& q = info.questions[i];
    const std::string base = "lessonq." + info.mission + "." + q.id;
    if (!t.has(base + ".prompt")) continue;
    LessonQuestionView v;
    v.index = static_cast<int>(i);
    v.id = q.id;
    v.prompt = t.tr(base + ".prompt");
    for (int c = 0; t.has(base + ".choice" + std::to_string(c)); ++c) v.choices.push_back(t.tr(base + ".choice" + std::to_string(c)));
    const bool matches = q.numeric ? v.choices.empty() : static_cast<int>(v.choices.size()) == q.choices;
    if (!matches) continue;
    if (t.has(base + ".unit")) v.unit = t.tr(base + ".unit");
    v.answered = std::find(answered.begin(), answered.end(), v.index) != answered.end();
    v.invalid = std::find(invalid.begin(), invalid.end(), v.index) != invalid.end();
    out.push_back(std::move(v));
  }
  return out;
}

void LessonDialog::choose(int question, int option) {
  if (int* c = find_pair(choice, question)) *c = option;
  else choice.emplace_back(question, option);
}

void LessonDialog::type(int question, std::string text) {
  if (std::string* s = find_pair(typed, question)) *s = std::move(text);
  else typed.emplace_back(question, std::move(text));
}

std::optional<GameCommand> LessonDialog::answer(const LessonInfo& info, int question) {
  if (question < 0 || static_cast<std::size_t>(question) >= info.questions.size()) return std::nullopt;
  if (!info.questions[static_cast<std::size_t>(question)].numeric) {
    const int* c = find_pair(choice, question);
    if (c == nullptr) return std::nullopt;
    return GameCommand::lesson(question, *c, {}, false);
  }
  const std::string* text = find_pair(typed, question);
  const auto value = numeric_answer(text != nullptr ? *text : std::string());
  std::erase(invalid, question);
  if (!value) {
    invalid.push_back(question);
    return std::nullopt;
  }
  return GameCommand::lesson(question, 0, *value, false);
}

GameCommand LessonDialog::skip(int question) { return GameCommand::lesson(question, 0, {}, true); }

std::optional<std::string> numeric_answer(const std::string& input) {
  const double v = fmt::parse_decimal(input);
  if (!std::isfinite(v) || v < 0) return std::nullopt;
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.8f", v);
  return std::string(buf);
}

std::vector<TutorialCard> tutorial_cards(const Texts& t, const std::string& mission) {
  std::vector<TutorialCard> out;
  for (int i = 0;; ++i) {
    const std::string base = "tutorial." + mission + "." + std::to_string(i);
    if (!t.has(base + ".title")) break;
    out.push_back({t.tr(base + ".title"), t.tr(base + ".body"), t.tr(base + ".art")});
  }
  return out;
}

std::vector<HelpRow> help_rows(const Texts& t, const Keymap& map) {
  std::vector<HelpRow> out;
  for (const Action a : kActions) out.push_back({a, key_label(map.key(a)), t.tr(action_label_key(a))});
  return out;
}

void Remap::start(Action a) {
  waiting = a;
  message.clear();
}

bool Remap::key(const Texts& t, Keymap& map, const std::string& key) {
  if (!waiting) return false;
  const Action a = *waiting;
  waiting.reset();
  if (key == "Escape") return true;
  const std::string label = key_label(key);
  const auto res = map.bind(a, key);
  message_ok = res.error == Keymap::BindError::None;
  if (message_ok) {
    message = t.tr("keys.saved", {{"action", t.tr(action_label_key(a))}, {"key", label}});
  } else if (res.error == Keymap::BindError::Reserved) {
    message = t.tr("keys.reserved", {{"key", label}});
  } else {
    message = t.tr("keys.conflict", {{"key", label}, {"action", t.tr(action_label_key(res.other))}});
  }
  return true;
}

PatternCard pattern_card(const Texts& t, const std::string& id) {
  const std::string base = "pattern." + id + ".";
  return {t.tr_or(base + "name", id), t.tr_or(base + "what", ""), t.tr_or(base + "means", ""), t.tr_or(base + "confirm", "")};
}

}  // namespace tradelab::desktop::game
