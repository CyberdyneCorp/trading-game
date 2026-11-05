// Lesson rubrics (task 4.5): parsed once from the embedded engine/rubrics/lessons.json.

#include "tradelab/lessons.hpp"

#include <algorithm>

#include "json_in.hpp"

namespace tradelab {
namespace {

using detail::JsonValue;
using Kind = JsonValue::Kind;

constexpr std::string_view kLessonsJson =
#include "lessons_data.inc"
    ;

constexpr std::int64_t kMaxPoints = 10;
constexpr std::int64_t kMaxChoices = 6;

const JsonValue* field(const JsonValue& obj, std::string_view key, Kind kind) {
  const JsonValue* v = obj.find(key);
  return v != nullptr && v->is(kind) ? v : nullptr;
}

bool read_money(const JsonValue& obj, std::string_view key, Money& out) {
  const JsonValue* v = field(obj, key, Kind::String);
  if (v == nullptr) return false;
  const Result<Money> m = parse_decimal<Money>(v->text);
  if (!m.ok() || m.value().raw < 0) return false;
  out = m.value();
  return true;
}

bool read_choice(const JsonValue& q, LessonQuestion& out) {
  const JsonValue* n = field(q, "choices", Kind::Int);
  const JsonValue* a = field(q, "answer", Kind::Int);
  if (n == nullptr || a == nullptr || n->integer < 2 || n->integer > kMaxChoices || a->integer < 0 || a->integer >= n->integer) {
    return false;
  }
  out.choices = static_cast<std::uint32_t>(n->integer);
  out.answer = static_cast<std::uint32_t>(a->integer);
  return true;
}

bool read_question(const JsonValue& q, LessonQuestion& out) {
  const JsonValue* id = field(q, "id", Kind::String);
  const JsonValue* kind = field(q, "kind", Kind::String);
  const JsonValue* points = field(q, "points", Kind::Int);
  if (id == nullptr || id->text.empty() || kind == nullptr || points == nullptr) return false;
  if (points->integer < 1 || points->integer > kMaxPoints) return false;
  out.id = id->text;
  out.points = static_cast<std::uint32_t>(points->integer);
  if (kind->text == "choice") return read_choice(q, out);
  out.numeric = kind->text == "numeric";
  return out.numeric && read_money(q, "answer", out.value) && read_money(q, "tolerance", out.tolerance);
}

bool unique_ids(const std::vector<LessonQuestion>& qs) {
  for (std::size_t i = 0; i < qs.size(); ++i) {
    for (std::size_t k = i + 1; k < qs.size(); ++k) {
      if (qs[i].id == qs[k].id) return false;
    }
  }
  return true;
}

bool read_rubric(const JsonValue& r, Rubric& out) {
  const JsonValue* mission = field(r, "mission", Kind::String);
  const JsonValue* reviewed = field(r, "reviewed", Kind::Bool);
  const JsonValue* reviewer = field(r, "reviewer", Kind::String);
  const JsonValue* questions = field(r, "questions", Kind::Array);
  if (mission == nullptr || reviewed == nullptr || reviewer == nullptr || questions == nullptr || questions->items.empty()) return false;
  out.mission = mission->text;
  out.reviewed = reviewed->boolean;
  out.reviewer = reviewer->text;
  // A reviewed rubric names its reviewer: the flag is a human's sign-off, not a default.
  if (out.reviewed && out.reviewer.empty()) return false;
  for (const JsonValue& q : questions->items) {
    LessonQuestion lq;
    if (!q.is(Kind::Object) || !read_question(q, lq)) return false;
    out.questions.push_back(lq);
  }
  return unique_ids(out.questions);
}

}  // namespace

bool parse_rubrics(std::string_view json, std::vector<Rubric>& out) {
  out.clear();
  const detail::JsonParse parsed = detail::parse_json(json);
  if (!parsed.ok || !parsed.value.is(Kind::Object)) return false;
  const JsonValue* version = field(parsed.value, "version", Kind::Int);
  const JsonValue* list = field(parsed.value, "rubrics", Kind::Array);
  if (version == nullptr || version->integer != 1 || list == nullptr) return false;
  for (const JsonValue& r : list->items) {
    Rubric rubric;
    if (!r.is(Kind::Object) || !read_rubric(r, rubric)) return false;
    const bool duplicate = std::any_of(out.begin(), out.end(), [&rubric](const Rubric& x) { return x.mission == rubric.mission; });
    if (duplicate) return false;
    out.push_back(std::move(rubric));
  }
  return true;
}

const std::vector<Rubric>& rubrics() {
  static const std::vector<Rubric> all = [] {
    std::vector<Rubric> out;
    if (!parse_rubrics(kLessonsJson, out)) out.clear();
    return out;
  }();
  return all;
}

const Rubric* rubric_for(std::string_view mission) {
  for (const Rubric& r : rubrics()) {
    if (r.mission == mission) return &r;
  }
  return nullptr;
}

bool lesson_correct(const LessonQuestion& q, const LessonAnswer& a) {
  if (a.skipped) return false;
  if (!q.numeric) return a.choice == q.answer;
  const std::int64_t diff = a.value.raw > q.value.raw ? a.value.raw - q.value.raw : q.value.raw - a.value.raw;
  return diff <= q.tolerance.raw;
}

}  // namespace tradelab
