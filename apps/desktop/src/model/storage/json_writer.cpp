#include "model/storage/json_writer.hpp"

namespace tradelab::desktop::storage {

void append_json_string(std::string& out, std::string_view s) {
  static constexpr char kHex[] = "0123456789abcdef";
  out += '"';
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += ch;
    } else if (c < 0x20) {
      out += "\\u00";
      out += kHex[c >> 4];
      out += kHex[c & 0xF];
    } else {
      out += ch;
    }
  }
  out += '"';
}

void JsonWriter::separate() {
  if (!first_) out_ += ',';
  first_ = false;
}

JsonWriter& JsonWriter::begin_object() {
  separate();
  out_ += '{';
  first_ = true;
  return *this;
}

JsonWriter& JsonWriter::end_object() {
  out_ += '}';
  first_ = false;
  return *this;
}

JsonWriter& JsonWriter::begin_array() {
  separate();
  out_ += '[';
  first_ = true;
  return *this;
}

JsonWriter& JsonWriter::end_array() {
  out_ += ']';
  first_ = false;
  return *this;
}

JsonWriter& JsonWriter::key(std::string_view k) {
  separate();
  append_json_string(out_, k);
  out_ += ':';
  first_ = true;  // the value that follows takes no comma
  return *this;
}

JsonWriter& JsonWriter::string(std::string_view s) {
  separate();
  append_json_string(out_, s);
  return *this;
}

JsonWriter& JsonWriter::integer(std::int64_t n) {
  separate();
  out_ += std::to_string(n);
  return *this;
}

JsonWriter& JsonWriter::boolean(bool b) {
  separate();
  out_ += b ? "true" : "false";
  return *this;
}

JsonWriter& JsonWriter::null() {
  separate();
  out_ += "null";
  return *this;
}

JsonWriter& JsonWriter::raw(std::string_view json) {
  separate();
  out_ += json;
  return *this;
}

JsonWriter& JsonWriter::field(std::string_view k, const std::optional<std::string>& s) {
  key(k);
  return s ? string(*s) : null();
}

namespace {

void dump_to(std::string& out, const json::Value& v);

void dump_array(std::string& out, const json::Value& v) {
  out += '[';
  bool first = true;
  for (const json::Value& item : v.items()) {
    if (!first) out += ',';
    first = false;
    dump_to(out, item);
  }
  out += ']';
}

void dump_object(std::string& out, const json::Value& v) {
  out += '{';
  bool first = true;
  for (const auto& [key, item] : v.fields()) {
    if (!first) out += ',';
    first = false;
    append_json_string(out, key);
    out += ':';
    dump_to(out, item);
  }
  out += '}';
}

void dump_to(std::string& out, const json::Value& v) {
  using Kind = json::Value::Kind;
  switch (v.kind()) {
    case Kind::Null: out += "null"; break;
    case Kind::Bool: out += v.as_bool() ? "true" : "false"; break;
    case Kind::Number: out += v.as_string(); break;
    case Kind::String: append_json_string(out, v.as_string()); break;
    case Kind::Array: dump_array(out, v); break;
    case Kind::Object: dump_object(out, v); break;
  }
}

}  // namespace

std::string dump(const json::Value& v) {
  std::string out;
  dump_to(out, v);
  return out;
}

}  // namespace tradelab::desktop::storage
