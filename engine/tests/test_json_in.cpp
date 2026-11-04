#include <doctest/doctest.h>

#include <string>

#include "../src/json_in.hpp"

using namespace tradelab::detail;

TEST_CASE("json: RFC 8785 canonical form sorts keys and strips whitespace") {
  const auto p = parse_json(R"( { "b" : [1, true, null], "a" : { "y": "x", "x": -3 }, "c": "line\nbreak \u00e9 \/" } )");
  REQUIRE(p.ok);
  CHECK(canonical_json(p.value) == "{\"a\":{\"x\":-3,\"y\":\"x\"},\"b\":[1,true,null],\"c\":\"line\\nbreak \xC3\xA9 /\"}");
}

TEST_CASE("json: keys sort by UTF-16 code units, not UTF-8 bytes") {
  // U+FF61 is one UTF-16 unit (0xFF61); U+1F600 is a surrogate pair starting 0xD83D, so it sorts first
  // even though its UTF-8 lead byte (0xF0) is larger than U+FF61's (0xEF).
  const auto p = parse_json("{\"\xEF\xBD\xA1\":1,\"\xF0\x9F\x98\x80\":2}");
  REQUIRE(p.ok);
  CHECK(canonical_json(p.value) == "{\"\xF0\x9F\x98\x80\":2,\"\xEF\xBD\xA1\":1}");
}

TEST_CASE("json: escapes follow JCS (short forms, lowercase \\u00xx)") {
  const auto p = parse_json(R"(["\u0001\b\t\"\\", "\ud83d\ude00"])");
  REQUIRE(p.ok);
  CHECK(canonical_json(p.value) == "[\"\\u0001\\b\\t\\\"\\\\\",\"\xF0\x9F\x98\x80\"]");
}

TEST_CASE("json: rejects input outside the strict subset") {
  const char* const bad[] = {
      "{\"a\":1,\"a\":2}",       // duplicate key
      "[1.5]",                     // fractions travel as strings
      "[1e3]",                     // exponent
      "[01]",                      // leading zero
      "[-0]",                      // no canonical form
      "[9007199254740992]",        // beyond 2^53 - 1
      "[\"\\ud800\"]",             // lone surrogate
      "[\"\xC0\xAF\"]",            // overlong UTF-8
      "[\"a\nb\"]",                // raw control character
      "{\"a\":1} x",               // trailing data
      "[1,]",                      // trailing comma
      "",                          // empty
  };
  for (const char* text : bad) CHECK_MESSAGE(!parse_json(text).ok, text);
}

TEST_CASE("json: limits on depth, size, strings and members") {
  JsonLimits lim;
  lim.max_depth = 3;
  CHECK(parse_json("[[[1]]]", lim).ok);
  CHECK(parse_json("[[[[1]]]]", lim).error == "depth");
  lim = {};
  lim.max_bytes = 8;
  CHECK(parse_json("[1,2,3,4,5]", lim).error == "too_large");
  lim = {};
  lim.max_string = 3;
  CHECK(parse_json("[\"abcd\"]", lim).error == "string_too_long");
  lim = {};
  lim.max_members = 2;
  CHECK(parse_json("[1,2,3]", lim).error == "too_many_members");
  CHECK(parse_json(std::string(100'000, '['), {}).error == "too_large");
}
