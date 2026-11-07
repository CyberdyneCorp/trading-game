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

std::optional<double> opt_double(const json::Value& v) {
  if (v.kind() != json::Value::Kind::Number) return std::nullopt;
  return v.as_double();
}

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
  for (const auto& l : c["levels"].items()) chart.levels.push_back({l["kind"].as_string(), l["price"].as_double()});
  return chart;
}

TicketWidget ticket_of(const json::Value& w) {
  TicketWidget t;
  t.market = w["market"].as_string();
  t.equity = w["equity"].as_double();
  t.entry = w["entry"].as_double();
  t.buy = w["side"].as_string() != "sell";
  t.qty = w["qty"].as_double();
  t.leverage = to_int(w["leverage"], 1);
  t.max_leverage = to_int(w["maxLeverage"], 1);
  t.stop = opt_double(w["stop"]);
  t.target = opt_double(w["target"]);
  t.mmr = w["mmr"].as_double();
  t.lot = w["lot"].as_double(1);
  t.edit = strings(w["edit"]);
  return t;
}

TableWidget table_of(const json::Value& w) {
  TableWidget t;
  t.market = w["market"].as_string();
  for (const auto& c : w["columns"].items()) {
    TableColumn col{c["id"].as_string(), std::nullopt, c["suffix"].as_string()};
    if (c.has("digits")) col.digits = to_int(c["digits"]);
    t.columns.push_back(std::move(col));
  }
  for (const auto& r : w["rows"].items()) {
    std::vector<TableCell> row;
    for (const auto& v : r.items()) {
      const bool number = v.kind() == json::Value::Kind::Number;
      row.push_back({number, number ? v.as_double() : 0.0, number ? std::string() : v.as_string()});
    }
    t.rows.push_back(std::move(row));
  }
  return t;
}

PayoffWidget payoff_of(const json::Value& w) {
  PayoffWidget p;
  p.market = w["market"].as_string();
  for (const auto& l : w["legs"].items()) {
    p.legs.push_back({l["kind"].as_string(), l["side"].as_string() != "sell", l["qty"].as_double(), l["strike"].as_double(),
                      l["price"].as_double()});
  }
  p.lo = w["lo"].as_double();
  p.hi = w["hi"].as_double();
  p.hide_analytics = w["hideAnalytics"].as_bool(false);
  return p;
}

LessonWidget widget_of(const json::Value& w) {
  const std::string kind = w["kind"].as_string();
  if (kind == "ticket") return ticket_of(w);
  if (kind == "table") return table_of(w);
  if (kind == "payoff") return payoff_of(w);
  return std::monostate{};
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

std::optional<StepQuestion> question_of(const json::Value& q) {
  if (q.is_null()) return std::nullopt;
  StepQuestion out;
  const std::string type = q["type"].as_string("choice");
  out.tolerance = q["tolerance"].as_double();
  if (type == "number") {
    out.type = StepQuestion::Type::Number;
    out.value = q["answer"].as_double();
    out.unit = q["unit"].as_string();
  } else if (type == "set") {
    out.type = StepQuestion::Type::Set;
    out.value = q["target"].as_double();
    out.field = q["field"].as_string();
  } else {
    for (const auto& c : q["choices"].items()) out.choices.push_back(choice_of(c));
    out.answer = to_int(q["answer"], -1);
  }
  return out;
}

AcademyLesson lesson_of(const json::Value& l) {
  AcademyLesson lesson{l["id"].as_string(), l["stage"].as_string(), {}};
  for (const auto& s : l["steps"].items()) {
    lesson.steps.push_back({s["id"].as_string(), s["phase"].as_string(), chart_of(s["chart"]), widget_of(s["widget"]),
                            question_of(s["question"]), strings(s["terms"])});
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

bool TicketWidget::editable(std::string_view field) const {
  for (const auto& f : edit) {
    if (f == field) return true;
  }
  return false;
}

const GlossaryEntry* WebContent::term(std::string_view id) const {
  for (const auto& t : glossary) {
    if (t.id == id) return &t;
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
  for (const auto& t : d["glossary"].items()) c.glossary.push_back({t["id"].as_string(), t["stage"].as_string(), strings(t["related"])});
  for (const auto& k : d["coach"].items()) c.coach.push_back({k["id"].as_string(), k["target"].as_string(), k["completes"].as_string()});
  for (const auto& [code, term] : d["findingTerms"].fields()) c.finding_terms.emplace(code, term.as_string());
  return c;
}

const WebContent& web_content() {
  static const WebContent content = parse_web_content(resource_text("web/content.json")).value_or(WebContent{});
  return content;
}

}  // namespace tradelab::desktop
