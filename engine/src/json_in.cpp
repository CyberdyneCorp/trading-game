#include "json_in.hpp"

#include <algorithm>

namespace tradelab::detail {
namespace {

constexpr std::int64_t kMaxSafeInt = 9'007'199'254'740'991;  // 2^53 - 1 (I-JSON, RFC 8785)

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0U | (cp >> 6U));
    out += static_cast<char>(0x80U | (cp & 0x3FU));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0U | (cp >> 12U));
    out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
    out += static_cast<char>(0x80U | (cp & 0x3FU));
  } else {
    out += static_cast<char>(0xF0U | (cp >> 18U));
    out += static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU));
    out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
    out += static_cast<char>(0x80U | (cp & 0x3FU));
  }
}

// Decodes one UTF-8 sequence at s[i]; returns its length (0 = invalid) and the code point.
std::size_t utf8_decode(std::string_view s, std::size_t i, std::uint32_t* cp) {
  const auto b0 = static_cast<std::uint8_t>(s[i]);
  if (b0 < 0x80) {
    *cp = b0;
    return 1;
  }
  std::size_t len = 0;
  std::uint32_t min = 0;
  if ((b0 & 0xE0U) == 0xC0U) { len = 2; min = 0x80; *cp = b0 & 0x1FU; }
  else if ((b0 & 0xF0U) == 0xE0U) { len = 3; min = 0x800; *cp = b0 & 0x0FU; }
  else if ((b0 & 0xF8U) == 0xF0U) { len = 4; min = 0x10000; *cp = b0 & 0x07U; }
  else return 0;
  if (i + len > s.size()) return 0;
  for (std::size_t k = 1; k < len; ++k) {
    const auto b = static_cast<std::uint8_t>(s[i + k]);
    if ((b & 0xC0U) != 0x80U) return 0;
    *cp = (*cp << 6U) | (b & 0x3FU);
  }
  const bool surrogate = *cp >= 0xD800 && *cp <= 0xDFFF;
  return (*cp < min || *cp > 0x10FFFF || surrogate) ? 0 : len;
}

class Parser {
 public:
  Parser(std::string_view text, const JsonLimits& limits) : s_(text), lim_(limits) {}

  JsonParse run() {
    JsonParse out;
    if (s_.size() > lim_.max_bytes) return fail(out, "too_large");
    skip_ws();
    if (!value(out.value, 0)) return fail(out, error_ == nullptr ? "syntax" : error_);
    skip_ws();
    if (pos_ != s_.size()) return fail(out, "trailing_data");
    out.ok = true;
    return out;
  }

 private:
  JsonParse& fail(JsonParse& out, const char* code) {
    out.ok = false;
    out.error = code;
    out.offset = pos_;
    out.value = JsonValue{};
    return out;
  }

  bool error(const char* code) {
    if (error_ == nullptr) error_ = code;
    return false;
  }

  void skip_ws() {
    while (pos_ < s_.size() && is_ws(s_[pos_])) ++pos_;
  }

  bool consume(char c) {
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool literal(std::string_view word) {
    if (s_.substr(pos_, word.size()) != word) return error("syntax");
    pos_ += word.size();
    return true;
  }

  bool value(JsonValue& v, std::uint32_t depth) {
    if (depth > lim_.max_depth) return error("depth");
    if (pos_ >= s_.size()) return error("syntax");
    switch (s_[pos_]) {
      case '{': return object(v, depth);
      case '[': return array(v, depth);
      case '"': v.kind = JsonValue::Kind::String; return string(v.text);
      case 't': v.kind = JsonValue::Kind::Bool; v.boolean = true; return literal("true");
      case 'f': v.kind = JsonValue::Kind::Bool; v.boolean = false; return literal("false");
      case 'n': v.kind = JsonValue::Kind::Null; return literal("null");
      default: return number(v);
    }
  }

  bool number(JsonValue& v) {
    const bool negative = consume('-');
    if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') return error("syntax");
    if (s_[pos_] == '0' && pos_ + 1 < s_.size() && s_[pos_ + 1] >= '0' && s_[pos_ + 1] <= '9') return error("number");
    std::int64_t n = 0;
    while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') {
      n = n * 10 + (s_[pos_++] - '0');
      if (n > kMaxSafeInt) return error("number");
    }
    // Fractions and exponents are outside the subset: decimals travel as strings.
    if (pos_ < s_.size() && (s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E')) return error("number");
    if (negative && n == 0) return error("number");  // "-0" has no canonical form
    v.kind = JsonValue::Kind::Int;
    v.integer = negative ? -n : n;
    return true;
  }

  bool unicode_escape(std::uint32_t* unit) {
    if (pos_ + 4 > s_.size()) return error("syntax");
    std::uint32_t u = 0;
    for (int k = 0; k < 4; ++k) {
      const int h = hex_value(s_[pos_++]);
      if (h < 0) return error("syntax");
      u = (u << 4U) | static_cast<std::uint32_t>(h);
    }
    *unit = u;
    return true;
  }

  bool escaped_code_point(std::string& out) {
    std::uint32_t hi = 0;
    if (!unicode_escape(&hi)) return false;
    if (hi >= 0xDC00 && hi <= 0xDFFF) return error("utf8");
    if (hi < 0xD800 || hi > 0xDBFF) {
      append_utf8(out, hi);
      return true;
    }
    std::uint32_t lo = 0;
    if (!consume('\\') || !consume('u') || !unicode_escape(&lo)) return error("utf8");
    if (lo < 0xDC00 || lo > 0xDFFF) return error("utf8");
    append_utf8(out, 0x10000 + ((hi - 0xD800) << 10U) + (lo - 0xDC00));
    return true;
  }

  bool escape(std::string& out) {
    if (pos_ >= s_.size()) return error("syntax");
    const char c = s_[pos_++];
    switch (c) {
      case '"': case '\\': case '/': out += c; return true;
      case 'b': out += '\b'; return true;
      case 'f': out += '\f'; return true;
      case 'n': out += '\n'; return true;
      case 'r': out += '\r'; return true;
      case 't': out += '\t'; return true;
      case 'u': return escaped_code_point(out);
      default: return error("syntax");
    }
  }

  bool raw_char(std::string& out) {
    std::uint32_t cp = 0;
    const std::size_t len = utf8_decode(s_, pos_, &cp);
    if (len == 0) return error("utf8");
    if (cp < 0x20) return error("syntax");  // control characters must be escaped
    out.append(s_.substr(pos_, len));
    pos_ += len;
    return true;
  }

  bool string(std::string& out) {
    ++pos_;  // opening quote
    out.clear();
    while (pos_ < s_.size() && s_[pos_] != '"') {
      const bool ok = s_[pos_] == '\\' ? (++pos_, escape(out)) : raw_char(out);
      if (!ok) return false;
      if (out.size() > lim_.max_string) return error("string_too_long");
    }
    return consume('"') || error("syntax");
  }

  bool array(JsonValue& v, std::uint32_t depth) {
    ++pos_;
    v.kind = JsonValue::Kind::Array;
    skip_ws();
    if (consume(']')) return true;
    do {
      if (v.items.size() >= lim_.max_members) return error("too_many_members");
      skip_ws();
      v.items.emplace_back();
      if (!value(v.items.back(), depth + 1)) return false;
      skip_ws();
    } while (consume(','));
    return consume(']') || error("syntax");
  }

  bool member(JsonValue& v, std::uint32_t depth) {
    skip_ws();
    if (pos_ >= s_.size() || s_[pos_] != '"') return error("syntax");
    std::string key;
    if (!string(key)) return false;
    if (v.find(key) != nullptr) return error("duplicate_key");
    skip_ws();
    if (!consume(':')) return error("syntax");
    skip_ws();
    v.members.emplace_back(std::move(key), JsonValue{});
    return value(v.members.back().second, depth + 1);
  }

  bool object(JsonValue& v, std::uint32_t depth) {
    ++pos_;
    v.kind = JsonValue::Kind::Object;
    skip_ws();
    if (consume('}')) return true;
    do {
      if (v.members.size() >= lim_.max_members) return error("too_many_members");
      if (!member(v, depth)) return false;
      skip_ws();
    } while (consume(','));
    return consume('}') || error("syntax");
  }

  std::string_view s_;
  JsonLimits lim_;
  std::size_t pos_ = 0;
  const char* error_ = nullptr;
};

std::vector<std::uint16_t> utf16_units(std::string_view s) {
  std::vector<std::uint16_t> out;
  std::size_t i = 0;
  while (i < s.size()) {
    std::uint32_t cp = 0;
    const std::size_t len = utf8_decode(s, i, &cp);
    i += len == 0 ? 1 : len;  // keys were validated on parse; be total anyway
    if (cp >= 0x10000) {
      out.push_back(static_cast<std::uint16_t>(0xD800 + ((cp - 0x10000) >> 10U)));
      out.push_back(static_cast<std::uint16_t>(0xDC00 + ((cp - 0x10000) & 0x3FFU)));
    } else {
      out.push_back(static_cast<std::uint16_t>(cp));
    }
  }
  return out;
}

void write_string(std::string& out, std::string_view s) {
  static constexpr char kHex[] = "0123456789abcdef";
  out += '"';
  for (const char c : s) {
    const auto u = static_cast<std::uint8_t>(c);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (u < 0x20) {
          out += "\\u00";
          out += kHex[u >> 4U];
          out += kHex[u & 0xFU];
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

void write_value(std::string& out, const JsonValue& v);

void write_object(std::string& out, const JsonValue& v) {
  std::vector<std::pair<std::vector<std::uint16_t>, const std::pair<std::string, JsonValue>*>> sorted;
  sorted.reserve(v.members.size());
  for (const auto& m : v.members) sorted.emplace_back(utf16_units(m.first), &m);
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  out += '{';
  for (std::size_t i = 0; i < sorted.size(); ++i) {
    if (i > 0) out += ',';
    write_string(out, sorted[i].second->first);
    out += ':';
    write_value(out, sorted[i].second->second);
  }
  out += '}';
}

void write_value(std::string& out, const JsonValue& v) {
  switch (v.kind) {
    case JsonValue::Kind::Null: out += "null"; break;
    case JsonValue::Kind::Bool: out += v.boolean ? "true" : "false"; break;
    case JsonValue::Kind::Int: out += std::to_string(v.integer); break;
    case JsonValue::Kind::String: write_string(out, v.text); break;
    case JsonValue::Kind::Array:
      out += '[';
      for (std::size_t i = 0; i < v.items.size(); ++i) {
        if (i > 0) out += ',';
        write_value(out, v.items[i]);
      }
      out += ']';
      break;
    case JsonValue::Kind::Object: write_object(out, v); break;
  }
}

}  // namespace

const JsonValue* JsonValue::find(std::string_view key) const {
  if (kind != Kind::Object) return nullptr;
  for (const auto& m : members) {
    if (m.first == key) return &m.second;
  }
  return nullptr;
}

JsonParse parse_json(std::string_view text, const JsonLimits& limits) { return Parser(text, limits).run(); }

std::string canonical_json(const JsonValue& v) {
  std::string out;
  write_value(out, v);
  return out;
}

}  // namespace tradelab::detail
