#include "model/i18n.hpp"

#include "model/resources.hpp"

namespace tradelab::desktop {

std::string_view locale_code(Locale locale) { return locale == Locale::En ? "en" : "pt-BR"; }

Locale locale_from_system(std::string_view code) {
  const bool english = code.size() >= 2 && (code[0] == 'e' || code[0] == 'E') && (code[1] == 'n' || code[1] == 'N') &&
                       (code.size() == 2 || code[2] == '-' || code[2] == '_' || code[2] == '.');
  return english ? Locale::En : Locale::PtBR;
}

std::string fill_placeholders(std::string_view tmpl, const Params& params) {
  std::string out;
  out.reserve(tmpl.size());
  std::size_t i = 0;
  while (i < tmpl.size()) {
    const std::size_t open = tmpl.find('{', i);
    const std::size_t close = open == std::string_view::npos ? open : tmpl.find('}', open);
    if (close == std::string_view::npos) {
      out.append(tmpl.substr(i));
      break;
    }
    out.append(tmpl.substr(i, open - i));
    const std::string_view name = tmpl.substr(open + 1, close - open - 1);
    const std::string* value = nullptr;
    for (const auto& [k, v] : params) {
      if (k == name) value = &v;
    }
    out.append(value ? std::string_view(*value) : tmpl.substr(open, close - open + 1));
    i = close + 1;
  }
  return out;
}

namespace {

class CsvReader {
 public:
  explicit CsvReader(std::string_view text) : s_(text) {
    if (s_.starts_with("\xEF\xBB\xBF")) s_.remove_prefix(3);
  }

  std::vector<std::vector<std::string>> rows() {
    std::vector<std::vector<std::string>> out;
    while (pos_ < s_.size()) {
      if (at_line_start_comment()) {
        skip_line();
        continue;
      }
      auto row = record();
      if (!(row.size() == 1 && row[0].empty())) out.push_back(std::move(row));
    }
    return out;
  }

 private:
  [[nodiscard]] bool at_line_start_comment() const { return s_[pos_] == '#'; }

  void skip_line() {
    const std::size_t nl = s_.find('\n', pos_);
    pos_ = nl == std::string_view::npos ? s_.size() : nl + 1;
  }

  std::vector<std::string> record() {
    std::vector<std::string> fields(1);
    while (pos_ < s_.size()) {
      const char c = s_[pos_++];
      if (c == '"' && fields.back().empty()) {
        quoted(fields.back());
      } else if (c == ',') {
        fields.emplace_back();
      } else if (c == '\n') {
        break;
      } else if (c != '\r') {
        fields.back() += c;
      }
    }
    return fields;
  }

  void quoted(std::string& field) {
    while (pos_ < s_.size()) {
      const char c = s_[pos_++];
      if (c != '"') {
        field += c;
      } else if (pos_ < s_.size() && s_[pos_] == '"') {
        field += '"';
        ++pos_;
      } else {
        return;
      }
    }
  }

  std::string_view s_;
  std::size_t pos_ = 0;
};

}  // namespace

std::vector<std::vector<std::string>> parse_csv(std::string_view text) { return CsvReader(text).rows(); }

bool Catalog::load_csv(std::string_view csv) {
  const auto rows = parse_csv(csv);
  if (rows.empty() || rows[0] != std::vector<std::string>{"key", "pt-BR", "en"}) return false;
  for (std::size_t i = 1; i < rows.size(); ++i) {
    const auto& row = rows[i];
    if (row.size() < 3) continue;
    texts_.insert_or_assign(row[0], Texts{row[1], row[2]});
  }
  return true;
}

std::string Catalog::tr(std::string_view key, const Params& params) const {
  auto it = texts_.find(key);
  if (it == texts_.end()) return std::string(key);
  const std::string& text = locale_ == Locale::En && !it->second.en.empty() ? it->second.en : it->second.pt;
  return params.empty() ? text : fill_placeholders(text, params);
}

const Catalog& builtin_catalog() {
  static const Catalog catalog = [] {
    Catalog c;
    c.load_csv(resource_text("i18n/strings.csv"));
    return c;
  }();
  return catalog;
}

}  // namespace tradelab::desktop
