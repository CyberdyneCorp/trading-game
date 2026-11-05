#include "model/content.hpp"

#include "model/json.hpp"
#include "model/resources.hpp"

namespace tradelab::desktop {

namespace {

std::vector<std::string> strings(const json::Value& v) {
  std::vector<std::string> out;
  for (const auto& item : v.items()) out.push_back(item.as_string());
  return out;
}

int to_int(const json::Value& v, int fallback = 0) { return static_cast<int>(v.as_int(fallback)); }

std::optional<LessonChart> chart_of(const json::Value& c) {
  if (c.is_null()) return std::nullopt;
  LessonChart chart;
  chart.market = c["market"].as_string();
  chart.interval = c["interval"].as_string();
  for (const auto& k : c["candles"].items()) {
    chart.candles.push_back(
        {k["o"].as_double(), k["h"].as_double(), k["l"].as_double(), k["c"].as_double(), k["v"].as_double()});
  }
  chart.highlight = to_int(c["highlight"]);
  chart.known = to_int(c["known"]);
  chart.anatomy = c["anatomy"].as_bool(false);
  return chart;
}

LessonChoice choice_of(const json::Value& c) {
  LessonChoice out;
  const std::string kind = c["kind"].as_string();
  if (kind == "pct") {
    out.kind = LessonChoice::Kind::Pct;
    out.value = c["value"].as_double();
  } else if (kind == "text") {
    out.kind = LessonChoice::Kind::Text;
    out.id = c["id"].as_string();
  } else {
    out.bar = to_int(c["bar"]);
  }
  return out;
}

AcademyLesson lesson_of(const json::Value& l) {
  AcademyLesson lesson{l["id"].as_string(), l["stage"].as_string(), {}};
  for (const auto& s : l["steps"].items()) {
    LessonStep step{s["id"].as_string(), s["phase"].as_string(), chart_of(s["chart"]), {}, -1};
    for (const auto& c : s["question"]["choices"].items()) step.choices.push_back(choice_of(c));
    step.answer = to_int(s["question"]["answer"], -1);
    lesson.steps.push_back(std::move(step));
  }
  return lesson;
}

}  // namespace

const Stage* WebContent::stage(std::string_view id) const {
  for (const auto& s : stages) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

const AcademyLesson* WebContent::lesson(std::string_view id) const {
  for (const auto& l : lessons) {
    if (l.id == id) return &l;
  }
  return nullptr;
}

std::optional<WebContent> parse_web_content(std::string_view text) {
  const auto doc = json::parse(text);
  if (!doc || doc->kind() != json::Value::Kind::Object) return std::nullopt;
  const json::Value& d = *doc;
  WebContent c;
  for (const auto& s : d["stages"].items()) {
    c.stages.push_back(
        {s["id"].as_string(), strings(s["requires"]), strings(s["missions"]), strings(s["lessons"]), strings(s["unlocks"])});
  }
  c.locked_leverage_cap = to_int(d["lockedLeverageCap"], 2);
  c.campaign_capital = d["campaignCapital"].as_string("10000");
  c.campaign_min_capital = d["campaignMinCapital"].as_string("100");
  c.campaign_history = to_int(d["campaignHistory"], 20);
  c.report_min_trades = to_int(d["reportMinTrades"], 30);
  c.after_release_bars = to_int(d["afterReleaseBars"], 10);
  c.shortcut_actions = strings(d["shortcutActions"]);
  for (const auto& a : c.shortcut_actions) c.default_keymap.emplace_back(a, d["defaultKeymap"][a].as_string());
  for (const auto& l : d["lessons"].items()) c.lessons.push_back(lesson_of(l));
  return c;
}

const WebContent& web_content() {
  static const WebContent content = parse_web_content(resource_text("web/content.json")).value_or(WebContent{});
  return content;
}

}  // namespace tradelab::desktop
