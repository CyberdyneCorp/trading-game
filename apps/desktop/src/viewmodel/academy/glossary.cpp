#include "viewmodel/academy/glossary.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace tradelab::desktop::academy {

namespace {

// Base letters of U+00C0..U+00FF (Latin-1 Supplement letters), lowercase; 0 = not a letter to fold.
constexpr std::array<char, 64> kLatin1 = {
    'a', 'a', 'a', 'a', 'a', 'a', 0,   'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',  // C0..CF
    0,   'n', 'o', 'o', 'o', 'o', 'o', 0,   0,   'u', 'u', 'u', 'u', 'y', 0,   0,    // D0..DF
    'a', 'a', 'a', 'a', 'a', 'a', 0,   'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',  // E0..EF
    0,   'n', 'o', 'o', 'o', 'o', 'o', 0,   0,   'u', 'u', 'u', 'u', 'y', 0,   'y'};  // F0..FF

bool space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

std::string trim(const std::string& s) {
  const auto first = std::ranges::find_if_not(s, space);
  const auto last = std::find_if_not(s.rbegin(), s.rend(), space).base();
  return first < last ? std::string(first, last) : std::string();
}

std::vector<std::string> split_aliases(const std::string& joined) {
  std::vector<std::string> out;
  std::size_t from = 0;
  while (from <= joined.size()) {
    const std::size_t at = joined.find("; ", from);
    if (at == std::string::npos) {
      out.push_back(joined.substr(from));
      break;
    }
    out.push_back(joined.substr(from, at - from));
    from = at + 2;
  }
  return out;
}

bool contains_folded(const std::string& text, const std::string& q) { return fold(text).find(q) != std::string::npos; }

bool matches(const TermText& t, const std::string& q) {
  if (q.empty() || contains_folded(t.term, q)) return true;
  return std::ranges::any_of(t.aliases, [&q](const std::string& a) { return contains_folded(a, q); });
}

// The folded base letter of the two-byte UTF-8 sequence at s[i] (0xC3 0x80..0xBF), 0 for anything else.
char latin1_base(std::string_view s, std::size_t i) {
  if (i + 1 >= s.size() || static_cast<unsigned char>(s[i]) != 0xC3) return 0;
  const auto next = static_cast<unsigned char>(s[i + 1]);
  return next >= 0x80 && next <= 0xBF ? kLatin1[next - 0x80] : 0;
}

}  // namespace

std::string fold(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char base = latin1_base(s, i);
    if (base != 0) {
      out += base;
      ++i;
    } else {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    }
  }
  return trim(out);
}

TermText term_text(const Catalog& catalog, std::string_view id) {
  const std::string base = "glossary." + std::string(id);
  TermText t;
  if (!catalog.has(base + ".term")) return t;
  t.term = catalog.tr(base + ".term");
  const std::string aliases = catalog.has(base + ".aliases") ? catalog.tr(base + ".aliases") : std::string();
  if (!aliases.empty()) t.aliases = split_aliases(aliases);
  t.short_text = catalog.tr(base + ".short");
  t.long_text = catalog.tr(base + ".long");
  if (catalog.has(base + ".example")) t.example = catalog.tr(base + ".example");
  return t;
}

std::vector<std::string> search_terms(const WebContent& content, const Catalog& catalog, std::string_view query) {
  const std::string q = fold(query);
  std::vector<std::pair<std::string, std::string>> hits;  // folded name, id
  for (const auto& g : content.glossary) {
    const TermText t = term_text(catalog, g.id);
    if (matches(t, q)) hits.emplace_back(fold(t.term), g.id);
  }
  std::ranges::stable_sort(hits, [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<std::string> out;
  out.reserve(hits.size());
  for (auto& h : hits) out.push_back(std::move(h.second));
  return out;
}

std::optional<std::string> finding_term(const WebContent& content, std::string_view code) {
  const auto it = content.finding_terms.find(code);
  if (it == content.finding_terms.end()) return std::nullopt;
  return it->second;
}

}  // namespace tradelab::desktop::academy
