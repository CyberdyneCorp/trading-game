// Port of apps/web/src/lib/content/glossary.ts (web parity fixture glossary.json): the shared glossary
// (add-academy-and-strategies design D7). The structure (id, stage, related) is content.json's; the
// texts are the catalog rows glossary.<id>.term / .aliases ("; "-joined) / .short / .long / .example.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/content.hpp"
#include "model/i18n.hpp"

namespace tradelab::desktop::academy {

// Lowercase without accents and trimmed, so "liquidacao" finds "Liquidação" (Latin-1 letters).
std::string fold(std::string_view s);

struct TermText {
  std::string term;
  std::vector<std::string> aliases;
  std::string short_text;
  std::string long_text;
  std::string example;  // "" = none
};

// The term's texts in the catalog's locale (empty when the catalog has no such term).
TermText term_text(const Catalog& catalog, std::string_view id);

// Ids of the terms whose name or aliases contain the query (accents and case ignored), sorted by
// name in the catalog's locale (accents and case ignored).
std::vector<std::string> search_terms(const WebContent& content, const Catalog& catalog, std::string_view query);

// The glossary term a debrief finding is about (glossary.ts FINDING_TERMS, in content.json); none for
// the others.
std::optional<std::string> finding_term(const WebContent& content, std::string_view code);

}  // namespace tradelab::desktop::academy
