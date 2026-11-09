#include "viewmodel/practice/drills.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <optional>
#include <set>

#include "viewmodel/academy/payoff.hpp"
#include "viewmodel/academy/review.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/order_rules.hpp"
#include "viewmodel/game/sizing.hpp"

namespace tradelab::desktop::practice {

namespace {

using Params = std::vector<std::pair<std::string, std::string>>;
using Kind = json::Value::Kind;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ---- decimal parameters on their grid ----

int decimals_of(std::string_view s) {
  const auto dot = s.find('.');
  return dot == std::string_view::npos ? 0 : static_cast<int>(s.size() - dot - 1);
}

std::int64_t pow10(int d) {
  std::int64_t p = 1;
  for (int i = 0; i < d; ++i) p *= 10;
  return p;
}

std::int64_t integer_of(std::string_view s) { return s.empty() || s == "-" ? 0 : std::strtoll(std::string(s).c_str(), nullptr, 10); }

// A decimal string as integer units at `d` decimals ("1.5", 2 -> 150), as the web computes it.
std::int64_t units(std::string_view s, int d) {
  const auto dot = s.find('.');
  const std::string_view whole = s.substr(0, dot);
  std::string frac(dot == std::string_view::npos ? std::string_view{} : s.substr(dot + 1));
  frac.resize(static_cast<std::size_t>(std::max<int>(d, static_cast<int>(frac.size()))), '0');
  return integer_of(whole) * pow10(d) + integer_of(frac);
}

// Integer units at `d` decimals as a canonical decimal string (no trailing zeros).
std::string from_units(std::int64_t u, int d) {
  const std::int64_t a = u < 0 ? -u : u;
  const std::int64_t scale = pow10(d);
  std::string out = (u < 0 ? "-" : "") + std::to_string(a / scale);
  if (d == 0) return out;
  std::string frac = std::to_string(a % scale);
  frac.insert(0, static_cast<std::size_t>(d) - std::min(frac.size(), static_cast<std::size_t>(d)), '0');
  while (!frac.empty() && frac.back() == '0') frac.pop_back();
  return frac.empty() ? out : out + "." + frac;
}

std::string lookup(const Params& params, std::string_view name) {
  for (const auto& [k, v] : params) {
    if (k == name) return v;
  }
  return {};
}

std::string draw_range(const DrillParam& p, Mulberry32& next) {
  const int d = std::max({decimals_of(p.lo), decimals_of(p.hi), decimals_of(p.step)});
  const std::int64_t lo = units(p.lo, d);
  const std::int64_t step = units(p.step, d);
  const auto count = static_cast<int>((units(p.hi, d) - lo) / step + 1);
  return from_units(lo + pick(next, count) * step, d);
}

std::string draw_param(const DrillParam& p, const Params& done, Mulberry32& next) {
  switch (p.kind) {
    case DrillParam::Kind::Range: return draw_range(p, next);
    case DrillParam::Kind::List: return p.of[static_cast<std::size_t>(pick(next, static_cast<int>(p.of.size())))];
    case DrillParam::Kind::Add:
    case DrillParam::Kind::Sub: break;
  }
  const std::string a = lookup(done, p.a);
  const std::string b = lookup(done, p.b);
  const int d = std::max(decimals_of(a), decimals_of(b));
  const std::int64_t sign = p.kind == DrillParam::Kind::Add ? 1 : -1;
  return from_units(units(a, d) + sign * units(b, d), d);
}

// ---- answers through the lesson checks (content/checks.ts) ----

// The web's Number(text): NaN for an empty or malformed text.
double number_of(const std::string& s) {
  if (s.empty()) return kNaN;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  return end != nullptr && *end == '\0' ? v : kNaN;
}

// A check field: a "$name" reference to a parameter, or a literal number.
double arg(const json::Value& v, const Params& params) {
  if (v.kind() == Kind::String) {
    const std::string s = v.as_string();
    return s.starts_with('$') ? number_of(lookup(params, s.substr(1))) : kNaN;
  }
  return v.kind() == Kind::Number ? v.as_double() : kNaN;
}

std::vector<PayoffLeg> legs_of(const json::Value& legs, const Params& params) {
  std::vector<PayoffLeg> out;
  for (const json::Value& l : legs.items()) {
    out.push_back({l["kind"].as_string(), l["side"].as_string() == "buy", arg(l["qty"], params), arg(l["strike"], params),
                   arg(l["price"], params)});
  }
  return out;
}

// stops.ts trailTrigger through checks.ts trailOf: the decimals as JS toFixed writes them.
double trail_of(const json::Value& c, const Params& p) {
  const bool sell = c["side"].as_string() == "sell";
  const std::string rate = c.has("ratePct") ? fmt::to_fixed_text(arg(c["ratePct"], p) / 100, 10) : std::string();
  const std::string distance = c.has("distance") ? fmt::to_fixed_text(arg(c["distance"], p), 8) : std::string();
  const std::string trigger = game::trail_trigger(sell, fmt::to_fixed_text(arg(c["close"], p), 8), distance, rate,
                                                  fmt::to_fixed_text(arg(c["tick"], p), 8));
  return number_of(trigger);
}

double liquidation_of(const json::Value& c, const Params& p) {
  const double entry = arg(c["entry"], p);
  const game::Costs costs{1, 0, arg(c["mmr"], p), 0, 0, {}};  // ticketSandbox.ts sandboxCosts
  const auto side = c["side"].as_string() == "buy" ? game::Side::Buy : game::Side::Sell;
  return game::liquidation_price(side, 1, entry, entry / arg(c["leverage"], p), costs).value_or(kNaN);
}

// The trade-arithmetic kinds the drill templates use (checks.ts scalar), in the web's operation order.
double trade_scalar(const std::string& kind, const json::Value& c, const Params& p) {
  const auto a = [&](std::string_view key) { return arg(c[key], p); };
  if (kind == "size") return a("equity") * a("riskPct") / 100 / std::abs(a("entry") - a("stop"));
  if (kind == "r") return std::abs(a("target") - a("entry")) / std::abs(a("entry") - a("stop"));
  if (kind == "funding") return a("notional") * a("ratePct") / 100;
  if (kind == "liquidation") return liquidation_of(c, p);
  if (kind == "trail") return trail_of(c, p);
  return kNaN;
}

// The options and strategy kinds (checks.ts scalar and strategyScalar).
double scalar(const std::string& kind, const json::Value& c, const Params& p) {
  const auto a = [&](std::string_view key) { return arg(c[key], p); };
  if (kind == "payoff") return academy::pnl_at(legs_of(c["legs"], p), a("at"));
  if (kind == "hedge") return std::abs(a("deltaPerUnit") * a("units"));
  if (kind == "scalp") return 0.5 * a("gamma") * a("move") * a("move") * a("units");
  if (kind == "calendar") return (a("far") - a("near")) * a("qty");
  return trade_scalar(kind, c, p);
}

// checks.ts roundTo: Math.round is half up.
double round_to(double v, int digits) {
  const double scale = std::pow(10.0, digits);
  return std::floor(v * scale + 0.5) / scale;
}

}  // namespace

std::string DrillItem::param(std::string_view name) const { return lookup(params, name); }

const DrillTemplate* drill_template(const WebContent& content, std::string_view id) {
  const auto it = std::ranges::find_if(content.drills, [&](const DrillTemplate& t) { return t.id == id; });
  return it == content.drills.end() ? nullptr : &*it;
}

std::vector<const DrillTemplate*> templates_of(const WebContent& content, std::string_view skill) {
  std::vector<const DrillTemplate*> out;
  for (const DrillTemplate& t : content.drills) {
    if (t.skill == skill) out.push_back(&t);
  }
  return out;
}

std::vector<std::string> skills_for_lessons(const WebContent& content, const std::vector<std::string>& lessons) {
  std::vector<std::string> out;
  const auto reviews = [&](const DrillTemplate* t) {
    return std::ranges::any_of(t->lessons, [&](const std::string& l) { return std::ranges::find(lessons, l) != lessons.end(); });
  };
  for (const std::string& skill : content.drill_skills) {
    if (std::ranges::any_of(templates_of(content, skill), reviews)) out.push_back(skill);
  }
  return out;
}

std::vector<std::pair<std::string, std::string>> draw_params(const DrillTemplate& t, Mulberry32& next) {
  Params out;
  for (const DrillParam& p : t.params) out.emplace_back(p.name, draw_param(p, out, next));
  return out;
}

double answer_of(const DrillTemplate& t, const std::vector<std::pair<std::string, std::string>>& params) {
  const json::Value& spec = t.answer;
  const std::string kind = spec["kind"].as_string();
  if (kind == "choice") {
    const double index = number_of(lookup(params, spec["from"].as_string()));
    const auto& answers = spec["answers"].items();
    if (!(index >= 0) || index >= static_cast<double>(answers.size())) return kNaN;
    return answers[static_cast<std::size_t>(index)].as_double();
  }
  const int digits = t.digits.value_or(2);
  if (kind == "breakeven") {
    const auto list = academy::breakevens(legs_of(spec["legs"], params));
    const auto index = static_cast<std::size_t>(spec["index"].as_int(0));
    return index < list.size() ? round_to(list[index], digits) : kNaN;
  }
  return round_to(scalar(kind, spec, params), digits);
}

DrillItem make_item(const DrillTemplate& t, Mulberry32& next) {
  DrillItem item;
  item.template_id = t.id;
  item.skill = t.skill;
  item.lessons = t.lessons;
  item.params = draw_params(t, next);
  item.answer = answer_of(t, item.params);
  item.digits = t.digits.value_or(2);
  item.tolerance = t.tolerance.value_or(0.5 * std::pow(10.0, -item.digits));
  item.choice = t.answer["kind"].as_string() == "choice";
  return item;
}

double parse_answer(std::string_view text) {
  std::string s;
  for (const char c : text) {
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') s.push_back(c);
  }
  if (const auto comma = s.find(','); comma != std::string::npos) s[comma] = '.';
  const std::size_t from = !s.empty() && s[0] == '-' ? 1 : 0;
  const auto dot = s.find('.', from);
  const auto digits = [](std::string_view d) { return !d.empty() && std::ranges::all_of(d, [](char c) { return c >= '0' && c <= '9'; }); };
  const std::string_view body(s);
  const bool ok = dot == std::string::npos ? digits(body.substr(from)) : digits(body.substr(from, dot - from)) && digits(body.substr(dot + 1));
  return ok ? number_of(s) : kNaN;
}

bool is_right(const DrillItem& item, double given) {
  if (item.choice) return given == item.answer;
  return std::isfinite(given) && std::abs(given - item.answer) <= item.tolerance + 1e-9;
}

DrillText item_text(const DrillItem& item, const DrillTemplate& t, const Catalog& catalog) {
  const Locale locale = catalog.locale();
  const std::string base = "drill." + t.id;
  Params values;
  for (const auto& [k, v] : item.params) values.emplace_back(k, fmt::fmt_num(number_of(v), locale, decimals_of(v)));
  DrillText out;
  for (int k = 0; k < t.choices; ++k) out.choices.push_back(catalog.tr(base + ".choice." + std::to_string(k)));
  if (item.choice) {
    const std::string from = t.answer["from"].as_string();
    const std::string chosen = item.param(from);
    if (t.cases > 0) std::ranges::replace_if(values, [&](const auto& kv) { return kv.first == from; }, std::pair{from, catalog.tr(base + ".case." + chosen)});
    const auto index = static_cast<std::size_t>(item.answer);
    out.answer = std::isfinite(item.answer) && index < out.choices.size() ? out.choices[index] : std::string();
  } else {
    out.answer = fmt::fmt_num(item.answer, locale, item.digits);
  }
  values.emplace_back("answer", out.answer);
  out.prompt = catalog.tr(base + ".prompt", values);
  out.explain = catalog.tr(base + ".explain", values);
  return out;
}

SkillMap skill_stats(const std::vector<DrillSession>& history) {
  SkillMap out;
  for (const DrillSession& s : history) {
    if (!s.skills) continue;
    for (const auto& [k, v] : *s.skills) {
      out[k].n += v.n;
      out[k].right += v.right;
    }
  }
  return out;
}

std::uint32_t drill_seed(const std::string& date, int session) {
  return fnv1a32("drill-v1:" + date + ":" + std::to_string(session));
}

int session_index(const std::vector<DrillSession>& history, const std::string& date) {
  return static_cast<int>(std::ranges::count_if(history, [&](const DrillSession& s) { return s.date == date; }));
}

namespace {

void push_unique(std::vector<std::string>& out, const std::vector<std::string>& skills, std::size_t limit) {
  for (const std::string& s : skills) {
    if (out.size() < limit && std::ranges::find(out, s) == out.end()) out.push_back(s);
  }
}

// Lessons whose review is due on the session's date (review.ts dueReviews, any order).
std::vector<std::string> due_lessons(const LessonReviews& reviews, const std::string& date) {
  std::vector<std::string> out;
  for (const auto& [id, e] : reviews) {
    if (e.box < kReviewRetired && e.due <= date) out.push_back(id);
  }
  return out;
}

std::vector<std::string> due_skills(const WebContent& content, const SessionInput& input) {
  std::vector<std::string> due;
  for (const std::string& s : content.drill_skills) {
    const auto it = input.drill_reviews.find(s);
    if (it != input.drill_reviews.end() && it->second.due <= input.now) due.push_back(s);
  }
  std::ranges::stable_sort(due, {}, [&](const std::string& s) { return input.drill_reviews.find(s)->second.due; });
  return due;
}

std::vector<std::string> weakest_skills(const WebContent& content, const SessionInput& input, const std::vector<std::uint32_t>& rank) {
  const SkillMap stats = skill_stats(input.history);
  const auto rate = [&](const std::string& s) {
    const auto it = stats.find(s);
    return it != stats.end() && it->second.n > 0 ? static_cast<double>(it->second.right) / it->second.n : 0.5;
  };
  std::vector<std::size_t> order(content.drill_skills.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) {
    const double ra = rate(content.drill_skills[a]);
    const double rb = rate(content.drill_skills[b]);
    return ra != rb ? ra < rb : rank[a] < rank[b];
  });
  std::vector<std::string> out;
  for (const std::size_t i : order) out.push_back(content.drill_skills[i]);
  return out;
}

}  // namespace

std::vector<std::string> choose_skills(const WebContent& content, const SessionInput& input, const std::vector<std::uint32_t>& rank) {
  std::vector<std::string> out;
  push_unique(out, skills_for_lessons(content, due_lessons(input.lesson_reviews, input.date)), kDrillFromLessons);
  push_unique(out, due_skills(content, input), kDrillItems);
  push_unique(out, weakest_skills(content, input, rank), kDrillItems);
  return out;
}

std::vector<DrillItem> build_session(const WebContent& content, const SessionInput& input) {
  Mulberry32 next(drill_seed(input.date, input.session));
  std::vector<std::uint32_t> rank;
  for (std::size_t i = 0; i < content.drill_skills.size(); ++i) rank.push_back(next());
  std::vector<DrillItem> items;
  for (const std::string& skill : choose_skills(content, input, rank)) {
    const auto ts = templates_of(content, skill);
    if (ts.empty()) continue;
    const DrillTemplate* t = ts[static_cast<std::size_t>(pick(next, static_cast<int>(ts.size())))];
    items.push_back(make_item(*t, next));
  }
  return items;
}

namespace {

// Whether any answered item of `skill` was wrong; none when every one of them was skipped.
std::optional<bool> any_wrong(const std::vector<DrillItem>& items, const std::vector<ItemResult>& results, const std::string& skill) {
  std::optional<bool> out;
  for (std::size_t k = 0; k < items.size() && k < results.size(); ++k) {
    if (items[k].skill != skill || results[k] == ItemResult::Skipped) continue;
    out = out.value_or(false) || results[k] == ItemResult::Wrong;
  }
  return out;
}

}  // namespace

std::int64_t day_start(const std::string& day) {
  std::tm t{};
  if (std::sscanf(day.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return 0;
  t.tm_year -= 1900;
  t.tm_mon -= 1;
  t.tm_isdst = -1;
  return static_cast<std::int64_t>(std::mktime(&t)) * 1000;
}

std::map<std::string, DrillReview, std::less<>> schedule_skills(std::map<std::string, DrillReview, std::less<>> reviews,
                                                                const std::vector<DrillItem>& items,
                                                                const std::vector<ItemResult>& results, const std::string& date) {
  const auto before = reviews;
  std::vector<std::string> seen;
  for (const DrillItem& it : items) {
    if (std::ranges::find(seen, it.skill) == seen.end()) seen.push_back(it.skill);
  }
  for (const std::string& skill : seen) {
    const auto wrong = any_wrong(items, results, skill);
    if (!wrong) continue;  // every item of the skill skipped
    const auto old = before.find(skill);
    const int box = *wrong ? 1 : std::min(static_cast<int>(academy::kReviewDays.size()), (old == before.end() ? 1 : old->second.box) + 1);
    reviews[skill] = {box, day_start(academy::add_days(date, academy::kReviewDays[static_cast<std::size_t>(box - 1)]))};
  }
  return reviews;
}

DrillSession session_record(const std::vector<DrillItem>& items, const std::vector<ItemResult>& results, const std::string& date) {
  DrillSession s{date, static_cast<int>(items.size()), 0, SkillMap{}};
  for (std::size_t k = 0; k < items.size() && k < results.size(); ++k) {
    if (results[k] == ItemResult::Skipped) continue;
    SkillStats& st = (*s.skills)[items[k].skill];
    st.n += 1;
    st.right += results[k] == ItemResult::Right ? 1 : 0;
    s.first_try += results[k] == ItemResult::Right ? 1 : 0;
  }
  return s;
}

Progress finish_session(Progress p, const std::vector<DrillItem>& items, const std::vector<ItemResult>& results, const std::string& date) {
  p.drill_reviews = schedule_skills(std::move(p.drill_reviews), items, results, date);
  p.drill_history.push_back(session_record(items, results, date));
  if (p.drill_history.size() > kDrillHistoryKeep) {
    p.drill_history.erase(p.drill_history.begin(), p.drill_history.end() - static_cast<std::ptrdiff_t>(kDrillHistoryKeep));
  }
  return p;
}

}  // namespace tradelab::desktop::practice
