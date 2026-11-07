#include "run_file.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "tradelab/c_api.h"

namespace tradelab::detail {
namespace {

using Kind = JsonValue::Kind;

const JsonValue kNull{};

const JsonValue& member(const JsonValue& obj, std::string_view key) {
  const JsonValue* v = obj.find(key);
  return v != nullptr ? *v : kNull;
}

// ---- applier: one C ABI call per action (the golden tests and tl_run_file_verify share it) ----

// NULL for an absent or null member: the C ABI's "not set".
const char* opt_text(const JsonValue& obj, std::string_view key) {
  const JsonValue& v = member(obj, key);
  return v.is(Kind::String) ? v.text.c_str() : nullptr;
}

std::int32_t int_of(const JsonValue& obj, std::string_view key) { return static_cast<std::int32_t>(member(obj, key).integer); }
std::uint32_t uint_of(const JsonValue& obj, std::string_view key) { return static_cast<std::uint32_t>(member(obj, key).integer); }
std::int32_t flag(const JsonValue& obj, std::string_view key) { return member(obj, key).boolean ? 1 : 0; }

constexpr std::array<std::string_view, 2> kSides = {"buy", "sell"};
constexpr std::array<std::string_view, 5> kOrderTypes = {"market", "limit", "stop_market", "take_profit", "trailing_stop"};
constexpr std::array<std::string_view, 2> kOptionTypes = {"market", "limit"};
constexpr std::array<std::string_view, 3> kStatuses = {"active", "finished", "bankrupt"};

// -1 for an unknown name: the engine rejects it.
std::int32_t code_of(const JsonValue& obj, std::string_view key, std::span<const std::string_view> names) {
  const std::string& name = member(obj, key).text;
  const auto it = std::find(names.begin(), names.end(), name);
  return it == names.end() ? -1 : static_cast<std::int32_t>(it - names.begin());
}

std::int32_t side(const JsonValue& a) { return code_of(a, "side", kSides); }

const char* place_order(tl_run* run, std::uint32_t cid, const JsonValue& a) {
  const std::int32_t type = code_of(a, "type", kOrderTypes);
  const char* rate = opt_text(a, "trailRate");
  if (type == 4 && rate != nullptr && *rate != '\0') {
    return tl_place_trailing_rate(run, cid, side(a), opt_text(a, "qty"), flag(a, "ro"), rate);
  }
  return tl_place_order_ex(run, cid, type, side(a), opt_text(a, "qty"), opt_text(a, "price"), flag(a, "ro"),
                           opt_text(a, "stop"), opt_text(a, "tp"), opt_text(a, "trail"));
}

const char* place_option(tl_run* run, std::uint32_t cid, const JsonValue& a) {
  return tl_place_option_order(run, cid, uint_of(a, "series"), side(a), code_of(a, "type", kOptionTypes), opt_text(a, "qty"),
                               opt_text(a, "premium"));
}

const char* record_plan(tl_run* run, std::uint32_t cid, const JsonValue& a) {
  return tl_record_plan(run, cid, side(a), flag(a, "skipped"), flag(a, "option"), flag(a, "lose"), opt_text(a, "inv"),
                        opt_text(a, "target"), int_of(a, "conf"), opt_text(a, "risk"));
}

const char* answer_lesson(tl_run* run, std::uint32_t cid, const JsonValue& a) {
  return tl_answer_lesson(run, cid, uint_of(a, "q"), uint_of(a, "choice"), opt_text(a, "value"), flag(a, "skipped"));
}

using Handler = const char* (*)(tl_run*, std::uint32_t, const JsonValue&);

const std::map<std::string_view, Handler>& handlers() {
  static const std::map<std::string_view, Handler> table = {
      {"lev", [](tl_run* r, std::uint32_t c, const JsonValue& a) { return tl_set_leverage(r, c, int_of(a, "lev")); }},
      {"order", place_order},
      {"opt", place_option},
      {"margin", [](tl_run* r, std::uint32_t c, const JsonValue& a) { return tl_adjust_margin(r, c, opt_text(a, "amount")); }},
      {"plan", record_plan},
      {"ans", [](tl_run* r, std::uint32_t c, const JsonValue& a) { return tl_answer_question(r, c, int_of(a, "pBp"), flag(a, "skipped")); }},
      {"lesson", answer_lesson},
      {"cancel", [](tl_run* r, std::uint32_t c, const JsonValue& a) { return tl_cancel_order(r, c, uint_of(a, "id")); }},
      {"move", [](tl_run* r, std::uint32_t c, const JsonValue& a) { return tl_move_order(r, c, uint_of(a, "id"), opt_text(a, "price")); }},
      {"close", [](tl_run* r, std::uint32_t c, const JsonValue&) { return tl_close_position(r, c); }},
      {"advice", [](tl_run* r, std::uint32_t c, const JsonValue&) { return tl_request_advice(r, c); }},
  };
  return table;
}

// ---- schema (design D1) ----

enum class F : std::uint8_t {
  Bool,
  Int,        // [lo, hi]
  Dec,        // unsigned decimal string
  DecOrNull,
  SignedDec,
  Side,
  OrderType,
  OptionType,
  Status,
  Tag,        // 1..32 of [A-Za-z0-9_-]
  Text,       // any string of at most hi bytes
  Mission,    // 1..64 of [a-z0-9._-]
  Hex,        // 64 lowercase hex
  HexOrNull,
  Object,     // checked by its own spec
  Array,
};

struct Field {
  std::string_view key;
  F type;
  bool required = true;
  std::int64_t lo = 0;
  std::int64_t hi = 0;
};

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// [-]digits{1,12}[.digits{1,8}]: the decimal strings the clients write (prices, quantities, amounts).
bool decimal_text(std::string_view s, bool allow_sign) {
  if (allow_sign && !s.empty() && s.front() == '-') s.remove_prefix(1);
  const std::size_t dot = s.find('.');
  const std::string_view whole = s.substr(0, dot);
  const std::string_view frac = dot == std::string_view::npos ? std::string_view{} : s.substr(dot + 1);
  const bool whole_ok = !whole.empty() && whole.size() <= 12 && std::all_of(whole.begin(), whole.end(), is_digit);
  const bool frac_ok = dot == std::string_view::npos || (!frac.empty() && frac.size() <= 8 && std::all_of(frac.begin(), frac.end(), is_digit));
  return whole_ok && frac_ok;
}

bool charset(std::string_view s, std::size_t max, bool (*ok)(char)) {
  return !s.empty() && s.size() <= max && std::all_of(s.begin(), s.end(), ok);
}

bool tag_char(char c) { return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-'; }
bool mission_char(char c) { return is_digit(c) || (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-'; }
bool hex_char(char c) { return is_digit(c) || (c >= 'a' && c <= 'f'); }

bool in(std::string_view s, std::span<const std::string_view> names) { return std::find(names.begin(), names.end(), s) != names.end(); }

bool string_ok(const std::string& s, const Field& f) {
  switch (f.type) {
    case F::Dec:
    case F::DecOrNull: return decimal_text(s, false);
    case F::SignedDec: return decimal_text(s, true);
    case F::Side: return in(s, kSides);
    case F::OrderType: return in(s, kOrderTypes);
    case F::OptionType: return in(s, kOptionTypes);
    case F::Status: return in(s, kStatuses);
    case F::Tag: return charset(s, 32, tag_char);
    case F::Text: return s.size() <= static_cast<std::size_t>(f.hi);
    case F::Mission: return charset(s, 64, mission_char);
    case F::Hex:
    case F::HexOrNull: return s.size() == 64 && std::all_of(s.begin(), s.end(), hex_char);
    default: return false;
  }
}

bool value_ok(const JsonValue& v, const Field& f) {
  if (v.is(Kind::Null)) return f.type == F::DecOrNull || f.type == F::HexOrNull;
  switch (f.type) {
    case F::Bool: return v.is(Kind::Bool);
    case F::Int: return v.is(Kind::Int) && v.integer >= f.lo && v.integer <= f.hi;
    case F::Object: return v.is(Kind::Object);
    case F::Array: return v.is(Kind::Array);
    default: return v.is(Kind::String) && string_ok(v.text, f);
  }
}

// The first member of `obj` that is unknown, missing or of the wrong type; nullopt when none. (Not an
// empty string for "none": "" is a valid JSON key, and an unknown "" member must be refused.)
std::optional<std::string> bad_member(const JsonValue& obj, std::span<const Field> spec) {
  for (const JsonMember& m : obj.members) {
    const bool known = std::any_of(spec.begin(), spec.end(), [&m](const Field& f) { return f.key == m.key; });
    if (!known) return m.key;
  }
  for (const Field& f : spec) {
    const JsonValue* v = obj.find(f.key);
    if (v == nullptr ? f.required : !value_ok(*v, f)) return std::string(f.key);
  }
  return std::nullopt;
}

constexpr std::int64_t kU32 = 0xFFFFFFFFLL;

constexpr std::array kTop = {
    Field{"format", F::Text, true, 0, 32}, Field{"version", F::Int, true, 1, 1}, Field{"mission", F::Mission},
    Field{"pack", F::HexOrNull},           Field{"seed", F::Int, true, 0, kU32}, Field{"options", F::Object},
    Field{"engine", F::Object},            Field{"labels", F::Object},           Field{"actions", F::Array},
    Field{"journal", F::Array},            Field{"result", F::Object},
};
constexpr std::array kOptions = {
    Field{"adviceAllowance", F::Int, true, 0, 1000}, Field{"newsLead", F::Int, true, 0, 255}, Field{"calendar", F::Bool},
    Field{"startingBalance", F::Dec, false},         Field{"leverageCap", F::Int, false, 1, 1000},
};
constexpr std::array kEngine = {
    Field{"executionPolicy", F::Int, true, 1, 1'000'000}, Field{"abi", F::Int, true, 0, 1'000'000}, Field{"version", F::Text, true, 0, 32},
};
constexpr std::array kLabels = {Field{"campaign", F::Bool}, Field{"assisted", F::Bool}};
constexpr std::array kResult = {Field{"status", F::Status}, Field{"digest", F::Hex}, Field{"eventsDigest", F::Hex}};
constexpr std::array kJournal = {
    Field{"step", F::Int, true, 0, 1'000'000}, Field{"side", F::Side},           Field{"thesis", F::Text, true, 0, 1000},
    Field{"invalidation", F::DecOrNull},       Field{"target", F::DecOrNull},    Field{"plannedRisk", F::DecOrNull},
    Field{"confidence", F::Int, true, 0, 100}, Field{"skipped", F::Bool},        Field{"loseScenario", F::Text, false, 0, 1000},
    Field{"instrument", F::Text, false, 0, 64}, Field{"combo", F::Tag, false},
};

// Action kinds (replay.py): the members each one has. `combo`/`strategy` tag strategy-builder legs.
constexpr Field kKind{"k", F::Text, true, 0, 16};
constexpr Field kCombo{"combo", F::Tag, false};
constexpr Field kStrategy{"strategy", F::Tag, false};
constexpr std::array kLev = {kKind, Field{"lev", F::Int, true, 1, 1000}};
constexpr std::array kOrder = {
    kKind, Field{"type", F::OrderType}, Field{"side", F::Side}, Field{"qty", F::Dec}, Field{"price", F::DecOrNull},
    Field{"ro", F::Bool}, Field{"stop", F::DecOrNull, false}, Field{"tp", F::DecOrNull, false}, Field{"trail", F::DecOrNull, false},
    Field{"trailRate", F::Dec, false}, kCombo, kStrategy,
};
constexpr std::array kOpt = {
    kKind, Field{"series", F::Int, true, 1, kU32}, Field{"side", F::Side}, Field{"type", F::OptionType}, Field{"qty", F::Dec},
    Field{"premium", F::DecOrNull}, kCombo, kStrategy,
};
constexpr std::array kMargin = {kKind, Field{"amount", F::SignedDec}};
constexpr std::array kPlan = {
    kKind, Field{"side", F::Side}, Field{"skipped", F::Bool}, Field{"option", F::Bool}, Field{"lose", F::Bool},
    Field{"inv", F::DecOrNull}, Field{"target", F::DecOrNull}, Field{"conf", F::Int, true, 0, 100}, Field{"risk", F::DecOrNull},
};
constexpr std::array kAns = {kKind, Field{"pBp", F::Int, true, 0, 10'000}, Field{"skipped", F::Bool}};
constexpr std::array kLesson = {
    kKind, Field{"q", F::Int, true, 0, 255}, Field{"choice", F::Int, true, 0, 255}, Field{"value", F::DecOrNull}, Field{"skipped", F::Bool},
};
constexpr std::array kCancel = {kKind, Field{"id", F::Int, true, 0, kU32}, kCombo, kStrategy};
constexpr std::array kMove = {kKind, Field{"id", F::Int, true, 0, kU32}, Field{"price", F::Dec}};
constexpr std::array kBare = {kKind};
constexpr std::array kAdv = {kKind, Field{"n", F::Int, true, 1, kMaxAdvance}};

std::span<const Field> action_spec(std::string_view kind) {
  static const std::map<std::string_view, std::span<const Field>> specs = {
      {"lev", kLev},     {"order", kOrder}, {"opt", kOpt},        {"margin", kMargin}, {"plan", kPlan},  {"ans", kAns},
      {"lesson", kLesson}, {"cancel", kCancel}, {"move", kMove}, {"close", kBare},    {"advice", kBare}, {"adv", kAdv},
  };
  const auto it = specs.find(kind);
  return it == specs.end() ? std::span<const Field>{} : it->second;
}

struct Checker {
  RunFileProblem problem;

  bool ok() const { return problem.reason.empty(); }

  void fail(std::string path) {
    if (ok()) problem = {"invalid_field", std::move(path)};
  }

  void object(const JsonValue& obj, std::span<const Field> spec, const std::string& path) {
    if (!ok()) return;
    if (!obj.is(Kind::Object)) return fail(path);
    const std::optional<std::string> bad = bad_member(obj, spec);
    if (bad) fail(path.empty() ? *bad : path + "." + *bad);
  }

  void list(const JsonValue& arr, std::size_t max, std::span<const Field> (*spec)(const JsonValue&), const std::string& path) {
    if (!ok()) return;
    if (arr.items.size() > max) return fail(path);
    for (std::size_t i = 0; i < arr.items.size() && ok(); ++i) {
      const JsonValue& item = arr.items[i];
      const std::string at = path + "[" + std::to_string(i) + "]";
      const std::span<const Field> fields = item.is(Kind::Object) ? spec(item) : std::span<const Field>{};
      if (fields.empty()) fail(at + (item.is(Kind::Object) ? ".k" : ""));
      else object(item, fields, at);
    }
  }
};

std::span<const Field> spec_of_action(const JsonValue& a) {
  const JsonValue& k = member(a, "k");
  return k.is(Kind::String) ? action_spec(k.text) : std::span<const Field>{};
}

std::span<const Field> spec_of_entry(const JsonValue&) { return kJournal; }

}  // namespace

JsonLimits run_file_limits() {
  return JsonLimits{.max_bytes = kRunFileMaxBytes, .max_depth = 8, .max_string = 2048, .max_members = 2 * kRunFileMaxActions};
}

bool is_advance(const JsonValue& action) { return member(action, "k").text == "adv"; }

std::string apply_action(tl_run* run, std::uint32_t cid, const JsonValue& action) {
  const auto it = handlers().find(member(action, "k").text);
  if (it == handlers().end()) return "unknown_action_kind";
  const JsonParse result = parse_json(it->second(run, cid, action));
  if (!result.ok) return "unparsable_result";
  return member(result.value, "accepted").boolean ? "" : member(result.value, "reason").text;
}

RunFileProblem check_run_file(const JsonValue& root) {
  if (!root.is(Kind::Object) || member(root, "format").text != "tradelab-run") return {"not_a_run_file", ""};
  const JsonValue& version = member(root, "version");
  if (!version.is(Kind::Int) || version.integer != 1) return {"unsupported_version", "version"};
  Checker c;
  c.object(root, kTop, "");
  c.object(member(root, "options"), kOptions, "options");
  c.object(member(root, "engine"), kEngine, "engine");
  c.object(member(root, "labels"), kLabels, "labels");
  c.object(member(root, "result"), kResult, "result");
  c.list(member(root, "actions"), kRunFileMaxActions, spec_of_action, "actions");
  c.list(member(root, "journal"), kRunFileMaxJournal, spec_of_entry, "journal");
  return c.problem;
}

}  // namespace tradelab::detail
