// The web parity fixtures (tests/fixtures/web/*.json, written by scripts/gen_web_fixtures.mjs from the
// web's own helpers): loading, replaying every case through a C++ port and comparing its results
// (numbers with a 1e-9 relative tolerance, texts and flags exactly; JSON null is "none" or NaN).
#pragma once

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "model/i18n.hpp"
#include "model/json.hpp"
#include "model/storage/json_writer.hpp"

namespace tradelab::desktop::test_support {

using json::Value;

using Case = std::function<void(const Value& args, const Value& out)>;

inline json::Value fixture(const std::string& name) {
  std::ifstream in(std::string(TRADELAB_WEB_FIXTURES_DIR) + "/" + name + ".json", std::ios::binary);
  REQUIRE_MESSAGE(in.good(), "missing fixture " << name << ".json: run scripts/gen_web_fixtures.mjs");
  std::ostringstream text;
  text << in.rdbuf();
  auto v = json::parse(text.str());
  REQUIRE(v.has_value());
  return *v;
}

inline std::string dump(const Value& v) { return storage::dump(v); }

inline bool same_number(double got, const Value& want) {
  if (want.is_null()) return std::isnan(got);
  const double w = want.as_double();
  return std::abs(got - w) <= 1e-9 * std::max({1.0, std::abs(got), std::abs(w)});
}

inline void check_number(double got, const Value& want) { CHECK_MESSAGE(same_number(got, want), "got " << got); }

inline void check_optional(const std::optional<double>& got, const Value& want) {
  if (want.is_null()) {
    CHECK_FALSE_MESSAGE(got.has_value(), "got " << got.value_or(0));
  } else {
    REQUIRE(got.has_value());
    check_number(*got, want);
  }
}

inline void check_string(const std::string& got, const Value& want) { CHECK(got == want.as_string()); }

inline void check_strings(const std::vector<std::string>& got, const Value& want) {
  REQUIRE(got.size() == want.size());
  for (std::size_t i = 0; i < got.size(); ++i) CHECK(got[i] == want[i].as_string());
}

inline std::optional<double> opt(const Value& v) { return v.is_null() ? std::nullopt : std::optional<double>(v.as_double()); }
inline Locale locale(const Value& v) { return v.as_string() == "en" ? Locale::En : Locale::PtBR; }
inline std::vector<std::string> strings(const Value& v) {
  std::vector<std::string> out;
  for (const auto& s : v.items()) out.push_back(s.as_string());
  return out;
}

inline void replay(const std::string& name, const std::map<std::string, Case>& ports) {
  const Value f = fixture(name);
  int n = 0;
  for (const Value& c : f["cases"].items()) {
    const std::string fn = c["fn"].as_string();
    const auto port = ports.find(fn);
    REQUIRE_MESSAGE(port != ports.end(), name << ": no C++ port mapped for " << fn);
    INFO(name << " " << fn << " " << dump(c["args"]) << " -> " << dump(c["out"]));
    port->second(c["args"], c["out"]);
    ++n;
  }
  CHECK(n > 0);
}

}  // namespace tradelab::desktop::test_support
