#pragma once

// Minimal JSON writer for the C ABI. Command inputs are never JSON; only pack manifests and import
// metadata are read, by the strict reader in json_in.hpp.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tradelab/fixed.hpp"

namespace tradelab::detail {

class JsonOut {
 public:
  JsonOut& begin_obj() { return open('{'); }
  JsonOut& end_obj() { return close('}'); }
  JsonOut& begin_arr() { return open('['); }
  JsonOut& end_arr() { return close(']'); }

  JsonOut& key(std::string_view k) {
    separate();
    quote(k);
    out_ += ':';
    after_key_ = true;
    return *this;
  }
  JsonOut& str(std::string_view v) {
    separate();
    quote(v);
    return *this;
  }
  JsonOut& num(std::int64_t v) {
    separate();
    out_ += std::to_string(v);
    return *this;
  }
  JsonOut& boolean(bool v) {
    separate();
    out_ += v ? "true" : "false";
    return *this;
  }
  // Already serialized JSON (a nested document), appended as one value.
  JsonOut& raw(std::string_view json) {
    separate();
    out_ += json;
    return *this;
  }
  JsonOut& null() {
    separate();
    out_ += "null";
    return *this;
  }
  template <typename Tag, int D>
  JsonOut& dec(Fixed<Tag, D> v) {
    return str(to_decimal_string(v));
  }

  // Convenience: key + value.
  template <typename T>
  JsonOut& field(std::string_view k, const T& v) {
    key(k);
    return value(v);
  }

  [[nodiscard]] const std::string& text() const { return out_; }

 private:
  JsonOut& value(std::string_view v) { return str(v); }
  JsonOut& value(const char* v) { return str(v); }
  JsonOut& value(const std::string& v) { return str(v); }
  JsonOut& value(bool v) { return boolean(v); }
  JsonOut& value(std::int64_t v) { return num(v); }
  JsonOut& value(std::uint64_t v) { return num(static_cast<std::int64_t>(v)); }
  JsonOut& value(std::int32_t v) { return num(v); }
  JsonOut& value(std::uint32_t v) { return num(v); }
  template <typename Tag, int D>
  JsonOut& value(Fixed<Tag, D> v) {
    return dec(v);
  }

  JsonOut& open(char c) {
    separate();
    out_ += c;
    first_.push_back(true);
    return *this;
  }
  JsonOut& close(char c) {
    out_ += c;
    first_.pop_back();
    return *this;
  }
  void separate() {
    if (after_key_) {
      after_key_ = false;
      return;
    }
    if (first_.empty()) return;
    if (!first_.back()) out_ += ',';
    first_.back() = false;
  }
  void quote(std::string_view s) {
    out_ += '"';
    for (const char ch : s) {
      if (ch == '"' || ch == '\\') {
        out_ += '\\';
        out_ += ch;
      } else if (static_cast<unsigned char>(ch) < 0x20) {
        static constexpr char kHex[] = "0123456789abcdef";
        out_ += "\\u00";
        out_ += kHex[(static_cast<unsigned char>(ch) >> 4U) & 0xFU];
        out_ += kHex[static_cast<unsigned char>(ch) & 0xFU];
      } else {
        out_ += ch;
      }
    }
    out_ += '"';
  }

  std::string out_;
  std::vector<bool> first_;
  bool after_key_ = false;
};

}  // namespace tradelab::detail
