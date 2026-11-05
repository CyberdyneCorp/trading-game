// Minimal JSON reader for the engine's C ABI output (tl_state, tl_debrief, tl_missions).
// Numbers keep their source text: engine amounts are decimal strings or integers and are only
// formatted for display, never turned into state.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tradelab::desktop::json {

class Value {
 public:
  enum class Kind : std::uint8_t { Null, Bool, Number, String, Array, Object };

  Value() = default;
  static Value make_bool(bool b);
  static Value make_number(std::string text);
  static Value make_string(std::string text);
  static Value make_array(std::vector<Value> items);
  static Value make_object(std::map<std::string, Value, std::less<>> fields);

  [[nodiscard]] Kind kind() const { return kind_; }
  [[nodiscard]] bool is_null() const { return kind_ == Kind::Null; }

  // Typed reads with a fallback when the value is missing or of another kind.
  [[nodiscard]] bool as_bool(bool fallback = false) const;
  [[nodiscard]] std::int64_t as_int(std::int64_t fallback = 0) const;
  [[nodiscard]] double as_double(double fallback = 0.0) const;
  // Strings as they are; numbers as their source text; anything else -> fallback.
  [[nodiscard]] std::string as_string(std::string_view fallback = {}) const;

  [[nodiscard]] const std::vector<Value>& items() const;
  [[nodiscard]] std::size_t size() const { return items().size(); }
  [[nodiscard]] const Value& operator[](std::size_t i) const;
  // Object field, or a shared null value when absent (chains safely: v["a"]["b"]).
  [[nodiscard]] const Value& operator[](std::string_view key) const;
  [[nodiscard]] bool has(std::string_view key) const;

 private:
  Kind kind_ = Kind::Null;
  bool bool_ = false;
  std::string text_;
  std::shared_ptr<const std::vector<Value>> items_;
  std::shared_ptr<const std::map<std::string, Value, std::less<>>> fields_;
};

// Parses a complete JSON document; std::nullopt on any syntax error or trailing garbage.
std::optional<Value> parse(std::string_view text);

}  // namespace tradelab::desktop::json
