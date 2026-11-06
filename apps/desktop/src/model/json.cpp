#include "model/json.hpp"

#include <charconv>
#include <cstdlib>

namespace tradelab::desktop::json {

namespace {

const std::vector<Value>& empty_items() {
  static const std::vector<Value> empty;
  return empty;
}

const Value& null_value() {
  static const Value null;
  return null;
}

void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

class Parser {
 public:
  explicit Parser(std::string_view text) : s_(text) {}

  std::optional<Value> document() {
    auto v = value(0);
    skip_ws();
    if (!v || pos_ != s_.size()) return std::nullopt;
    return v;
  }

 private:
  static constexpr int kMaxDepth = 64;

  std::optional<Value> value(int depth) {
    if (depth > kMaxDepth) return std::nullopt;
    skip_ws();
    if (pos_ >= s_.size()) return std::nullopt;
    switch (s_[pos_]) {
      case '{': return object(depth);
      case '[': return array(depth);
      case '"': {
        auto str = string();
        if (!str) return std::nullopt;
        return Value::make_string(std::move(*str));
      }
      case 't': return literal("true", Value::make_bool(true));
      case 'f': return literal("false", Value::make_bool(false));
      case 'n': return literal("null", Value{});
      default: return number();
    }
  }

  std::optional<Value> literal(std::string_view word, Value v) {
    if (s_.substr(pos_, word.size()) != word) return std::nullopt;
    pos_ += word.size();
    return v;
  }

  std::optional<Value> number() {
    const std::size_t start = pos_;
    if (peek('-')) ++pos_;
    const std::size_t digits = pos_;
    while (pos_ < s_.size() && is_number_char(s_[pos_])) ++pos_;
    if (pos_ == digits) return std::nullopt;
    return Value::make_number(std::string(s_.substr(start, pos_ - start)));
  }

  static bool is_number_char(char c) {
    return (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-';
  }

  std::optional<std::string> string() {
    ++pos_;  // opening quote
    std::string out;
    while (pos_ < s_.size()) {
      const char c = s_[pos_++];
      if (c == '"') return out;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (!escape(out)) return std::nullopt;
    }
    return std::nullopt;
  }

  bool escape(std::string& out) {
    if (pos_ >= s_.size()) return false;
    const char e = s_[pos_++];
    switch (e) {
      case '"': out += '"'; return true;
      case '\\': out += '\\'; return true;
      case '/': out += '/'; return true;
      case 'b': out += '\b'; return true;
      case 'f': out += '\f'; return true;
      case 'n': out += '\n'; return true;
      case 'r': out += '\r'; return true;
      case 't': out += '\t'; return true;
      case 'u': return unicode(out);
      default: return false;
    }
  }

  bool hex4(std::uint32_t& cp) {
    if (pos_ + 4 > s_.size()) return false;
    const auto* first = s_.data() + pos_;
    auto [ptr, ec] = std::from_chars(first, first + 4, cp, 16);
    if (ec != std::errc{} || ptr != first + 4) return false;
    pos_ += 4;
    return true;
  }

  bool unicode(std::string& out) {
    std::uint32_t cp = 0;
    if (!hex4(cp)) return false;
    const bool high_surrogate = cp >= 0xD800 && cp <= 0xDBFF;
    if (high_surrogate && s_.substr(pos_, 2) == "\\u") {
      pos_ += 2;
      std::uint32_t low = 0;
      if (!hex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
      cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
    }
    append_utf8(out, cp);
    return true;
  }

  std::optional<Value> array(int depth) {
    ++pos_;
    std::vector<Value> items;
    skip_ws();
    if (peek(']')) {
      ++pos_;
      return Value::make_array(std::move(items));
    }
    while (true) {
      auto v = value(depth + 1);
      if (!v) return std::nullopt;
      items.push_back(std::move(*v));
      skip_ws();
      if (peek(']')) {
        ++pos_;
        return Value::make_array(std::move(items));
      }
      if (!peek(',')) return std::nullopt;
      ++pos_;
    }
  }

  std::optional<Value> object(int depth) {
    ++pos_;
    std::map<std::string, Value, std::less<>> fields;
    skip_ws();
    if (peek('}')) {
      ++pos_;
      return Value::make_object(std::move(fields));
    }
    while (true) {
      skip_ws();
      if (!peek('"')) return std::nullopt;
      auto key = string();
      skip_ws();
      if (!key || !peek(':')) return std::nullopt;
      ++pos_;
      auto v = value(depth + 1);
      if (!v) return std::nullopt;
      fields.insert_or_assign(std::move(*key), std::move(*v));
      skip_ws();
      if (peek('}')) {
        ++pos_;
        return Value::make_object(std::move(fields));
      }
      if (!peek(',')) return std::nullopt;
      ++pos_;
    }
  }

  void skip_ws() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) ++pos_;
  }

  [[nodiscard]] bool peek(char c) const { return pos_ < s_.size() && s_[pos_] == c; }

  std::string_view s_;
  std::size_t pos_ = 0;
};

}  // namespace

Value Value::make_bool(bool b) {
  Value v;
  v.kind_ = Kind::Bool;
  v.bool_ = b;
  return v;
}

Value Value::make_number(std::string text) {
  Value v;
  v.kind_ = Kind::Number;
  v.text_ = std::move(text);
  return v;
}

Value Value::make_string(std::string text) {
  Value v;
  v.kind_ = Kind::String;
  v.text_ = std::move(text);
  return v;
}

Value Value::make_array(std::vector<Value> items) {
  Value v;
  v.kind_ = Kind::Array;
  v.items_ = std::make_shared<const std::vector<Value>>(std::move(items));
  return v;
}

Value Value::make_object(std::map<std::string, Value, std::less<>> fields) {
  Value v;
  v.kind_ = Kind::Object;
  v.fields_ = std::make_shared<const std::map<std::string, Value, std::less<>>>(std::move(fields));
  return v;
}

bool Value::as_bool(bool fallback) const { return kind_ == Kind::Bool ? bool_ : fallback; }

std::int64_t Value::as_int(std::int64_t fallback) const {
  if (kind_ != Kind::Number) return fallback;
  std::int64_t out = 0;
  auto [ptr, ec] = std::from_chars(text_.data(), text_.data() + text_.size(), out);
  return ec == std::errc{} && ptr == text_.data() + text_.size() ? out : fallback;
}

double Value::as_double(double fallback) const {
  if (kind_ != Kind::Number && kind_ != Kind::String) return fallback;
  if (text_.empty()) return fallback;
  char* end = nullptr;
  const double out = std::strtod(text_.c_str(), &end);
  return end == text_.c_str() + text_.size() ? out : fallback;
}

std::string Value::as_string(std::string_view fallback) const {
  if (kind_ == Kind::String || kind_ == Kind::Number) return text_;
  return std::string(fallback);
}

const std::vector<Value>& Value::items() const {
  return kind_ == Kind::Array && items_ ? *items_ : empty_items();
}

const Value& Value::operator[](std::size_t i) const {
  const auto& list = items();
  return i < list.size() ? list[i] : null_value();
}

const Value& Value::operator[](std::string_view key) const {
  if (kind_ != Kind::Object || !fields_) return null_value();
  auto it = fields_->find(key);
  return it == fields_->end() ? null_value() : it->second;
}

bool Value::has(std::string_view key) const { return kind_ == Kind::Object && fields_ && fields_->contains(key); }

const std::map<std::string, Value, std::less<>>& Value::fields() const {
  static const std::map<std::string, Value, std::less<>> empty;
  return kind_ == Kind::Object && fields_ ? *fields_ : empty;
}

std::optional<Value> parse(std::string_view text) { return Parser(text).document(); }

}  // namespace tradelab::desktop::json
