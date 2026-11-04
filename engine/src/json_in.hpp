#pragma once

// Strict JSON reader for untrusted pack manifests and import metadata, plus RFC 8785 (JCS)
// canonical output. Deliberately a subset: numbers must be integers within +-(2^53 - 1) (money is
// always a decimal string), strings must be valid UTF-8, duplicate keys are rejected and depth,
// length and member counts are bounded. Under that subset JCS number formatting is plain decimal
// digits, so canonicalization needs no floating point.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradelab::detail {

struct JsonLimits {
  std::size_t max_bytes = std::size_t{64} << 10U;
  std::uint32_t max_depth = 16;
  std::size_t max_string = 1024;   // bytes, after unescaping
  std::size_t max_members = 256;   // per object or array
};

struct JsonValue {
  enum class Kind : std::uint8_t { Null, Bool, Int, String, Array, Object };
  Kind kind = Kind::Null;
  bool boolean = false;
  std::int64_t integer = 0;
  std::string text;
  std::vector<JsonValue> items;                            // Array
  std::vector<std::pair<std::string, JsonValue>> members;  // Object, in document order

  // Object lookup; nullptr when absent or when this is not an object.
  [[nodiscard]] const JsonValue* find(std::string_view key) const;
  [[nodiscard]] bool is(Kind k) const { return kind == k; }
};

struct JsonParse {
  bool ok = false;
  std::string error;  // short code: "syntax", "depth", "too_large", "duplicate_key", "number", "utf8", ...
  std::size_t offset = 0;
  JsonValue value;
};

JsonParse parse_json(std::string_view text, const JsonLimits& limits = {});

// RFC 8785 canonical form: members sorted by UTF-16 code units, no whitespace, minimal escapes.
std::string canonical_json(const JsonValue& v);

}  // namespace tradelab::detail
