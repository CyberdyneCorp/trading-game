// The C ABI's JSON output conforms to the IDL (engine/idl/tradelab.idl): every object has exactly
// the IDL's keys (optional ones may be absent), values have the declared types, enum strings are
// declared names and decimals are decimal strings. The web client's TypeScript types are generated
// from the same IDL, so this is what keeps them true to the engine.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "idl_schema.hpp"
#include "../src/json_in.hpp"
#include "pack_fixtures.hpp"
#include "tradelab/c_api.h"

using tradelab::detail::JsonParse;
using tradelab::detail::JsonValue;
using Kind = tradelab::detail::JsonValue::Kind;

namespace {

constexpr tradelab::detail::JsonLimits kBig{std::size_t{64} << 20U, 32, std::size_t{16} << 20U, std::size_t{1} << 20U};

JsonValue parse(std::string_view text) {
  JsonParse p = tradelab::detail::parse_json(text, kBig);
  INFO("parse error " << p.error << " at " << p.offset);
  REQUIRE(p.ok);
  return p.value;
}

const JsonValue& schema() {
  static const JsonValue s = parse(tradelab::test::kIdlSchema);
  return s;
}

bool is_decimal(const std::string& s) {
  std::size_t i = s.size() > 1 && s[0] == '-' ? 1 : 0;
  const std::size_t dot = s.find('.');
  const auto digits = [&](std::size_t from, std::size_t to) {
    return to > from && std::all_of(s.begin() + static_cast<std::ptrdiff_t>(from), s.begin() + static_cast<std::ptrdiff_t>(to),
                                    [](char c) { return c >= '0' && c <= '9'; });
  };
  if (dot == std::string::npos) return digits(i, s.size());
  return digits(i, dot) && digits(dot + 1, s.size());
}

class Validator {
 public:
  std::vector<std::string> errors;

  void check(const JsonValue& v, const JsonValue& type, const std::string& path) {
    if (type.is(Kind::String)) {
      check_name(v, type.text, path);
    } else if (const JsonValue* of = type.find("array")) {
      check_array(v, *of, path);
    } else if (const JsonValue* alts = type.find("or")) {
      check_or(v, *alts, path);
    } else if (const JsonValue* lit = type.find("lit")) {
      const bool same = lit->kind == v.kind && lit->text == v.text && lit->integer == v.integer;
      if (!same) fail(path, "is not the literal the IDL declares");
    }
  }

  void check_root(const JsonValue& v, const std::string& type_name) { check_name(v, type_name, type_name); }

 private:
  void fail(const std::string& path, std::string_view what) {
    if (errors.size() < 20) errors.push_back(path + " " + std::string(what));
  }

  void expect(const JsonValue& v, Kind k, const std::string& path, std::string_view what) {
    if (!v.is(k)) fail(path, what);
  }

  void check_name(const JsonValue& v, const std::string& name, const std::string& path) {
    if (name == "string") return expect(v, Kind::String, path, "is not a string");
    if (name == "int") return expect(v, Kind::Int, path, "is not an integer");
    if (name == "bool") return expect(v, Kind::Bool, path, "is not a boolean");
    if (name == "null") return expect(v, Kind::Null, path, "is not null");
    if (name == "Decimal") {
      if (!v.is(Kind::String) || !is_decimal(v.text)) fail(path, "is not a decimal string");
      return;
    }
    const JsonValue* decl = schema().find(name);
    REQUIRE_MESSAGE(decl != nullptr, "type not in the IDL: " << name);
    check_decl(v, *decl, path);
  }

  void check_decl(const JsonValue& v, const JsonValue& decl, const std::string& path) {
    if (const JsonValue* names = decl.find("enum")) return check_enum(v, *names, path);
    if (const JsonValue* fields = decl.find("fields")) return check_struct(v, *fields, path);
    if (const JsonValue* t = decl.find("type")) return check(v, *t, path);
    check_tuple(v, decl, path);
  }

  void check_enum(const JsonValue& v, const JsonValue& names, const std::string& path) {
    const bool known = v.is(Kind::String) &&
                       std::any_of(names.items.begin(), names.items.end(), [&](const JsonValue& n) { return n.text == v.text; });
    if (!known) fail(path, "is not a declared enum name: " + v.text);
  }

  void check_struct(const JsonValue& v, const JsonValue& fields, const std::string& path) {
    if (!v.is(Kind::Object)) return fail(path, "is not an object");
    for (const JsonValue& f : fields.items) {
      const std::string& name = f.find("name")->text;
      const JsonValue* got = v.find(name);
      if (got != nullptr) check(*got, *f.find("type"), path + "." + name);
      else if (!f.find("optional")->boolean) fail(path + "." + name, "is missing");
    }
    for (const auto& m : v.members) {
      const bool declared = std::any_of(fields.items.begin(), fields.items.end(), [&](const JsonValue& f) { return f.find("name")->text == m.key; });
      if (!declared) fail(path + "." + m.key, "is not in the IDL");
    }
  }

  void check_tuple(const JsonValue& v, const JsonValue& decl, const std::string& path) {
    const std::vector<JsonValue>& types = decl.find("tuple")->items;
    const auto required = static_cast<std::size_t>(decl.find("required")->integer);
    if (!v.is(Kind::Array) || v.items.size() < required || v.items.size() > types.size()) return fail(path, "is not the declared tuple");
    for (std::size_t i = 0; i < v.items.size(); ++i) check(v.items[i], types[i], path + "[" + std::to_string(i) + "]");
  }

  void check_array(const JsonValue& v, const JsonValue& of, const std::string& path) {
    if (!v.is(Kind::Array)) return fail(path, "is not an array");
    for (std::size_t i = 0; i < v.items.size(); ++i) check(v.items[i], of, path + "[" + std::to_string(i) + "]");
  }

  void check_or(const JsonValue& v, const JsonValue& alts, const std::string& path) {
    for (const JsonValue& alt : alts.items) {
      Validator trial;
      trial.check(v, alt, path);
      if (trial.errors.empty()) return;
    }
    fail(path, "matches none of the IDL alternatives");
  }
};

std::vector<std::string> violations(std::string_view json, const std::string& type) {
  Validator val;
  val.check_root(parse(json), type);
  return val.errors;
}

void conforms(std::string_view json, const std::string& type) {
  const std::vector<std::string> errs = violations(json, type);
  std::string all;
  for (const std::string& e : errs) all += e + "\n";
  INFO(type << " violations:\n" << all);
  CHECK(errs.empty());
}

// A command result must conform; returns it for convenience.
std::string command(const char* json) {
  conforms(json, "CommandResult");
  return json;
}

std::uint32_t first_option_series(const std::string& state) {
  const JsonValue s = parse(state);
  const JsonValue& chain = *s.find("options")->find("chain");
  return chain.items.empty() ? 0 : static_cast<std::uint32_t>(chain.items[0].find("id")->integer);
}

// Exercises every command kind so the state carries orders, plans, advice and option positions.
void play_some(tl_run* run, std::uint32_t& id) {
  command(tl_request_advice(run, ++id));
  command(tl_set_leverage(run, ++id, 2));
  command(tl_record_plan(run, ++id, 0, 0, 0, 1, "1", "1000000", 60, "10"));
  command(tl_place_order_ex(run, ++id, 0, 0, "0.1", nullptr, 0, "1", "1000000", nullptr));
  command(tl_place_order(run, ++id, 1, 0, "0.1", "1", 0));
  command(tl_place_trailing_rate(run, ++id, 1, "0.1", 1, "0.02"));
  command(tl_answer_question(run, ++id, 5000, 0));
  command(tl_adjust_margin(run, ++id, "1"));
  command(tl_cancel_order(run, ++id, 999));
  command(tl_move_order(run, ++id, 999, "1"));
  const std::uint32_t series = first_option_series(tl_state(run));
  if (series != 0) command(tl_place_option_order(run, ++id, series, 0, 0, "1", nullptr));
}

}  // namespace

TEST_CASE("IDL conformance: version, missions and errors") {
  conforms(tl_version(), "EngineVersion");
  const JsonValue missions = parse(tl_missions());
  REQUIRE(missions.is(Kind::Array));
  for (const JsonValue& m : missions.items) {
    Validator v;
    v.check_root(m, "MissionInfo");
    CHECK(v.errors.empty());
  }
  conforms(tl_mission_reveal("no-such-mission", 1), "EngineError");
  tl_run* run = tl_run_create("m01-false-breakout", 1);
  REQUIRE(run != nullptr);
  conforms(tl_debrief(run), "EngineError");
  conforms(tl_checkpoint(run), "Checkpoint");
  tl_run_destroy(run);
}

TEST_CASE("IDL conformance: state, debrief and reveal of every mission") {
  const JsonValue missions = parse(tl_missions());
  std::string debriefs;  // to check the optional shapes were exercised
  for (const JsonValue& m : missions.items) {
    const std::string id_text = m.find("id")->text;
    CAPTURE(id_text);
    tl_run* run = tl_run_create_ex(id_text.c_str(), 7, 3, 2, 1, nullptr, 0);
    REQUIRE(run != nullptr);
    conforms(tl_state(run), "GameState");
    std::uint32_t id = 0;
    play_some(run, id);
    conforms(tl_advance(run, 12), "GameState");
    command(tl_close_position(run, ++id));
    conforms(tl_advance(run, 100000), "GameState");
    const std::string debrief = tl_debrief(run);
    conforms(debrief, "DebriefData");
    debriefs += debrief;
    conforms(tl_mission_reveal(id_text.c_str(), 7), "MissionReveal");
    tl_run_destroy(run);
  }
  CHECK(debriefs.find("\"kind\":\"call\"") != std::string::npos);  // an option trade row
  CHECK(debriefs.find("\"safeLeverage\":") != std::string::npos);    // a perpetual trade row
  CHECK(debriefs.find("\"greeks\":{") != std::string::npos);
  CHECK(debriefs.find("\"regimes\":[{") != std::string::npos);
}

TEST_CASE("IDL conformance: CSV import, packs and pack runs") {
  const std::string seed_hex(64, 'a');
  const std::string meta = R"({"asset":"BTC","pair":"BTC/USDT","venue":"test","volumeUnit":"base","intervalSeconds":3600,"timestampUnit":"s","leverageCap":3})";
  const std::string csv = tradelab::test::make_csv(160);
  conforms(tl_csv_validate(csv.c_str(), meta.c_str()), "CsvReport");
  conforms(tl_csv_validate("timestamp,open\nx,y\n", meta.c_str()), "CsvReport");
  conforms(tl_csv_validate(csv.c_str(), "{}"), "CsvMetaError");
  conforms(tl_pack_build_csv(csv.c_str(), "{}", seed_hex.c_str()), "CsvPackBuild");
  conforms(tl_device_key("zz"), "EngineError");

  conforms(tl_pack_build_csv(csv.c_str(), meta.c_str(), seed_hex.c_str()), "CsvPackBuild");
  const std::vector<std::uint8_t> pack(tl_blob_data(), tl_blob_data() + tl_blob_size());
  const std::string key_json = tl_device_key(seed_hex.c_str());
  conforms(key_json, "DeviceKey");
  const JsonValue key = parse(key_json);
  const std::string keys = R"([{"id":")" + key.find("id")->text + R"(","key":")" + key.find("publicKey")->text + "\"}]";
  const auto size = static_cast<std::uint32_t>(pack.size());
  conforms(tl_pack_validate(pack.data(), size, keys.c_str(), 0), "PackCheck");
  conforms(tl_pack_validate(pack.data(), size, "[]", 0), "PackCheck");

  tl_run* run = tl_run_create_pack(pack.data(), size, keys.c_str(), 3);
  REQUIRE(run != nullptr);
  std::uint32_t id = 0;
  play_some(run, id);
  conforms(tl_advance(run, 100000), "GameState");
  conforms(tl_debrief(run), "DebriefData");
  tl_run_destroy(run);
}

TEST_CASE("IDL conformance checker rejects output that drifts from the IDL") {
  tl_run* run = tl_run_create("m01-false-breakout", 1);
  REQUIRE(run != nullptr);
  const std::string state = tl_state(run);
  tl_run_destroy(run);
  const auto drifted = [&](std::string_view from, std::string_view to) {
    std::string s = state;
    const std::size_t at = s.find(from);
    REQUIRE(at != std::string::npos);
    return violations(s.replace(at, from.size(), to), "GameState");
  };
  CHECK_FALSE(violations(state, "GameState").size() > 0);
  CHECK(drifted(R"("status":"active")", R"("status":"paused")").size() == 1);         // undeclared enum name
  CHECK(drifted(R"("alias":)", R"("aliasName":)").size() == 2);                        // missing + unknown key
  CHECK(drifted(R"("leverage":)", R"("leverage":"1","x":)").size() == 2);            // wrong type + unknown key
  CHECK_FALSE(drifted(R"("wallet":")", R"("wallet":"x)").empty());                    // not a decimal
}
