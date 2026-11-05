#include "model/packs/catalog.hpp"

#include <algorithm>

#include "model/json.hpp"
#include "model/packs/pack_library.hpp"

namespace tradelab::desktop::packs {

namespace {

using Kind = json::Value::Kind;

bool is_str(const json::Value& v, std::size_t max = 200) {
  return v.kind() == Kind::String && !v.as_string().empty() && v.as_string().size() <= max;
}

// A positive integer (no fraction, no exponent), as JSON number text.
std::optional<std::int64_t> pos_int(const json::Value& v) {
  if (v.kind() != Kind::Number) return std::nullopt;
  const std::string t = v.as_string();
  if (t.empty() || t.size() > 15 || !std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; })) {
    return std::nullopt;
  }
  const std::int64_t n = v.as_int();
  return n > 0 ? std::optional<std::int64_t>(n) : std::nullopt;
}

bool is_hex64(const json::Value& v) {
  const std::string t = v.kind() == Kind::String ? v.as_string() : std::string();
  return t.size() == 64 && std::all_of(t.begin(), t.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool is_mode(const std::string& m) {
  return !m.empty() && m.size() <= 20 && std::all_of(m.begin(), m.end(), [](char c) { return c >= 'a' && c <= 'z'; });
}

std::optional<LocalText> local_text(const json::Value& v) {
  LocalText out;
  if (is_str(v["pt-BR"], 400)) out.pt_br = v["pt-BR"].as_string();
  if (is_str(v["en"], 400)) out.en = v["en"].as_string();
  if (out.pt_br.empty() && out.en.empty()) return std::nullopt;
  return out;
}

bool has_strings(const json::Value& v) {
  return is_str(v["id"], 80) && is_str(v["file"], 300) && is_str(v["scenarioId"], 80) && is_str(v["keyId"], 80);
}

struct Numbers {
  std::int64_t size, interval, bars, leverage;
};

std::optional<Numbers> numbers(const json::Value& v) {
  const auto size = pos_int(v["size"]);
  const auto interval = pos_int(v["intervalSeconds"]);
  const auto bars = pos_int(v["bars"]);
  const auto leverage = pos_int(v["leverageCap"]);
  if (!size || !interval || !bars || !leverage) return std::nullopt;
  if (*size > static_cast<std::int64_t>(kPackMaxBytes)) return std::nullopt;
  return Numbers{*size, *interval, *bars, *leverage};
}

std::optional<CatalogEntry> entry(const json::Value& v, const std::string& base) {
  if (v.kind() != Kind::Object || !has_strings(v) || !is_hex64(v["digest"])) return std::nullopt;
  const auto n = numbers(v);
  const auto title = local_text(v["title"]);
  if (!n || !title) return std::nullopt;
  CatalogEntry e;
  e.id = v["id"].as_string();
  e.url = resolve_url(base, v["file"].as_string());
  e.digest = v["digest"].as_string();
  e.size = n->size;
  e.scenario_id = v["scenarioId"].as_string();
  e.key_id = v["keyId"].as_string();
  e.interval_seconds = static_cast<int>(n->interval);
  e.bars = static_cast<int>(n->bars);
  e.leverage_cap = static_cast<int>(n->leverage);
  e.title = *title;
  e.summary = local_text(v["summary"]).value_or(LocalText{});
  if (const std::string mode = v["mode"].as_string(); v["mode"].kind() == Kind::String && is_mode(mode)) e.mode = mode;
  return e;
}

bool has_scheme(const std::string& s) {
  const auto colon = s.find(':');
  if (colon == std::string::npos || colon == 0) return false;
  return std::all_of(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(colon), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
  });
}

}  // namespace

std::string resolve_url(const std::string& base, const std::string& ref) {
  if (has_scheme(ref)) return ref;
  const auto scheme_end = base.find("://");
  if (ref.starts_with("//")) return base.substr(0, base.find(':') + 1) + ref;
  if (ref.starts_with("/")) {
    const auto path_start = scheme_end == std::string::npos ? 0 : base.find('/', scheme_end + 3);
    return (path_start == std::string::npos ? base : base.substr(0, path_start)) + ref;
  }
  const std::string folder = base.substr(0, base.rfind('/') + 1);
  return folder + (ref.starts_with("./") ? ref.substr(2) : ref);
}

std::optional<Catalog> parse_catalog(std::string_view json_text, const std::string& base_url) {
  const auto doc = json::parse(json_text);
  if (!doc || doc->kind() != Kind::Object) return std::nullopt;
  const json::Value& schema = (*doc)["schemaVersion"];
  if (schema.kind() != Kind::Number || schema.as_int() != kCatalogSchema || (*doc)["packs"].kind() != Kind::Array) {
    return std::nullopt;
  }
  Catalog out;
  for (const json::Value& p : (*doc)["packs"].items()) {
    auto e = entry(p, base_url);
    if (!e) continue;
    const bool dup = std::any_of(out.entries.begin(), out.entries.end(), [&](const CatalogEntry& x) { return x.digest == e->digest; });
    if (!dup) out.entries.push_back(std::move(*e));
  }
  return out;
}

std::string localized(const LocalText& text, std::string_view locale) {
  if (locale == "en" && !text.en.empty()) return text.en;
  return text.pt_br.empty() ? text.en : text.pt_br;
}

}  // namespace tradelab::desktop::packs
