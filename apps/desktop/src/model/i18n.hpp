// Text catalogs generated from the web's i18n (scripts/gen_i18n.mjs -> resources/i18n/strings.csv,
// Raven's locale CSV: "key,pt-BR,en"). `{name}` placeholders are filled here, not by Raven, so
// viewmodels format text without a window. PT-BR is the default, en the second language.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradelab::desktop {

enum class Locale { PtBR, En };

std::string_view locale_code(Locale locale);
// "pt-BR"/"pt"/anything unknown -> PtBR; "en", "en-US", "en_GB.UTF-8" -> En (spec: en only when English).
Locale locale_from_system(std::string_view code);

using Params = std::vector<std::pair<std::string, std::string>>;

// Fills `{name}` with the matching parameter; unknown placeholders stay as written (like the web's t()).
std::string fill_placeholders(std::string_view tmpl, const Params& params);

// Parses one CSV table (RFC 4180 quoting, '#' comment lines, optional UTF-8 BOM) into rows of fields.
std::vector<std::vector<std::string>> parse_csv(std::string_view text);

class Catalog {
 public:
  // Loads a "key,pt-BR,en" table; returns false when the header is not that.
  bool load_csv(std::string_view csv);

  [[nodiscard]] Locale locale() const { return locale_; }
  void set_locale(Locale locale) { locale_ = locale; }

  // The text in the current locale, PT-BR when the locale has none, the key itself when unknown.
  [[nodiscard]] std::string tr(std::string_view key, const Params& params = {}) const;
  [[nodiscard]] bool has(std::string_view key) const { return texts_.contains(key); }
  [[nodiscard]] std::size_t size() const { return texts_.size(); }

 private:
  struct Texts {
    std::string pt;
    std::string en;
  };
  std::map<std::string, Texts, std::less<>> texts_;
  Locale locale_ = Locale::PtBR;
};

// The catalog built into the binary (resources/i18n/strings.csv), loaded once.
const Catalog& builtin_catalog();

}  // namespace tradelab::desktop
