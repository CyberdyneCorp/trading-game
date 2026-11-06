// Pack catalog (port of apps/web/src/lib/game/catalog.ts): the same static JSON manifest the web reads
// (schemaVersion 1, `packs` list; apps/web/static/catalog/catalog.json). The manifest is only a list and
// never grants trust: every downloaded pack is checked against its size and SHA-256, then verified by the
// engine against the pinned publisher keys before it is installed.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tradelab::desktop::packs {

constexpr int kCatalogSchema = 1;

struct LocalText {
  std::string pt_br;
  std::string en;
};

struct CatalogEntry {
  std::string id;
  std::string url;     // absolute: the entry's `file` resolved against the manifest URL
  std::string digest;  // SHA-256 of the whole pack (lowercase hex)
  std::int64_t size = 0;
  std::string scenario_id;
  std::string key_id;
  int interval_seconds = 0;
  int bars = 0;
  int leverage_cap = 0;
  LocalText title;
  LocalText summary;
  std::optional<std::string> mode;
};

struct Catalog {
  std::vector<CatalogEntry> entries;
};

// A wrong schema version or shape refuses the whole catalog (nullopt); a malformed entry is skipped, so
// one bad row never hides the rest. Duplicate digests keep the first entry.
std::optional<Catalog> parse_catalog(std::string_view json_text, const std::string& base_url);

// `ref` resolved against `base` (absolute URL of the manifest): absolute URLs are kept, "/x" is taken from
// the base's origin, anything else from the base's folder.
std::string resolve_url(const std::string& base, const std::string& ref);

// Text in the player's language ("pt-BR" or "en"), else the other one.
std::string localized(const LocalText& text, std::string_view locale);

}  // namespace tradelab::desktop::packs
