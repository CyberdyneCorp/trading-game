// Small JSON writer for what storage keeps as text (actions, progress, run headers): compact output,
// object fields in call order. Values read back with model/json.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "model/json.hpp"

namespace tradelab::desktop::storage {

class JsonWriter {
 public:
  JsonWriter& begin_object();
  JsonWriter& end_object();
  JsonWriter& begin_array();
  JsonWriter& end_array();
  JsonWriter& key(std::string_view k);

  JsonWriter& string(std::string_view s);
  JsonWriter& integer(std::int64_t n);
  JsonWriter& boolean(bool b);
  JsonWriter& null();
  // Already-serialized JSON (e.g. an opaque value kept as text).
  JsonWriter& raw(std::string_view json);

  JsonWriter& field(std::string_view k, std::string_view s) { return key(k).string(s); }
  JsonWriter& field(std::string_view k, const char* s) { return key(k).string(s); }
  JsonWriter& field(std::string_view k, const std::string& s) { return key(k).string(s); }
  JsonWriter& field(std::string_view k, std::int64_t n) { return key(k).integer(n); }
  JsonWriter& field(std::string_view k, int n) { return key(k).integer(n); }
  JsonWriter& field(std::string_view k, std::uint32_t n) { return key(k).integer(n); }
  JsonWriter& field(std::string_view k, bool b) { return key(k).boolean(b); }
  // A missing optional string is written as null.
  JsonWriter& field(std::string_view k, const std::optional<std::string>& s);

  [[nodiscard]] const std::string& text() const { return out_; }

 private:
  void separate();
  std::string out_;
  bool first_ = true;
};

void append_json_string(std::string& out, std::string_view s);

// Compact JSON text of a parsed value (object keys in key order; numbers as their source text).
std::string dump(const json::Value& v);

}  // namespace tradelab::desktop::storage
