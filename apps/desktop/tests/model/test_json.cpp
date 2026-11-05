#include <doctest/doctest.h>

#include "model/json.hpp"

using tradelab::desktop::json::parse;

TEST_CASE("json: engine-shaped documents") {
  auto v = parse(R"({"a":"10.50000000","n":-3,"b":true,"z":null,"arr":[1,"x",[2]],"o":{"k":"v"}})");
  REQUIRE(v);
  CHECK(v->operator[]("a").as_string() == "10.50000000");
  CHECK(v->operator[]("a").as_double() == doctest::Approx(10.5));
  CHECK(v->operator[]("n").as_int() == -3);
  CHECK(v->operator[]("b").as_bool());
  CHECK(v->operator[]("z").is_null());
  CHECK(v->operator[]("arr").size() == 3);
  CHECK(v->operator[]("arr")[2][0].as_int() == 2);
  CHECK(v->operator[]("o")["k"].as_string() == "v");
  CHECK(v->operator[]("missing")["deeper"].is_null());
  CHECK(v->operator[]("missing").as_int(7) == 7);
}

TEST_CASE("json: strings and escapes") {
  auto v = parse(R"(["a\"b\\c\/\n", "é—", "😀"])");
  REQUIRE(v);
  CHECK((*v)[0].as_string() == "a\"b\\c/\n");
  CHECK((*v)[1].as_string() == "\xC3\xA9\xE2\x80\x94");
  CHECK((*v)[2].as_string() == "\xF0\x9F\x98\x80");
}

TEST_CASE("json: refuses malformed input") {
  CHECK_FALSE(parse(""));
  CHECK_FALSE(parse("{"));
  CHECK_FALSE(parse("[1,]"));
  CHECK_FALSE(parse("{\"a\" 1}"));
  CHECK_FALSE(parse("tru"));
  CHECK_FALSE(parse("1 2"));
  CHECK_FALSE(parse("\"open"));
  std::string deep(200, '[');
  CHECK_FALSE(parse(deep));
}
