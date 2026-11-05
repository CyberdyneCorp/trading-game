#include "model/game_prefs.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "model/json.hpp"

namespace tradelab::desktop {

namespace {

std::string json_string(std::string_view s) {
  std::string out = "\"";
  for (const char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out + "\"";
}

std::string list(const std::vector<std::string>& items) {
  std::string out = "[";
  for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + json_string(items[i]);
  return out + "]";
}

std::vector<std::string> strings_of(const json::Value& v) {
  std::vector<std::string> out;
  for (const auto& item : v.items()) {
    if (item.kind() == json::Value::Kind::String) out.push_back(item.as_string());
  }
  return out;
}

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr ? v : "";
}

}  // namespace

std::string prefs_to_json(const GamePrefs& p) {
  std::string keys = "{";
  bool first = true;
  for (const auto& [action, key] : p.keymap) {
    keys += (first ? "" : ",") + json_string(action) + ":" + json_string(key);
    first = false;
  }
  keys += "}";
  return "{\"version\":1,\"keymap\":" + keys + ",\"tutorialsSeen\":" + list(p.tutorials_seen) +
         ",\"advisorPoints\":" + std::to_string(p.advisor_points) + ",\"settledRuns\":" + list(p.settled_runs) +
         ",\"finished\":" + list(p.finished) + ",\"perksIntroduced\":" + list(p.perks_introduced) + "}\n";
}

GamePrefs prefs_from_json(std::string_view text) {
  GamePrefs p;
  const auto v = json::parse(text);
  if (!v || v->kind() != json::Value::Kind::Object) return p;
  const json::Value& keys = (*v)["keymap"];
  if (keys.kind() == json::Value::Kind::Object) {
    for (const char* action : {"advance", "advance5", "buy", "sell", "close", "patterns", "auto", "mute", "options",
                               "journal", "help"}) {
      if (keys[action].kind() == json::Value::Kind::String) p.keymap[action] = keys[action].as_string();
    }
  }
  p.tutorials_seen = strings_of((*v)["tutorialsSeen"]);
  p.advisor_points = static_cast<int>((*v)["advisorPoints"].as_int(3));
  p.settled_runs = strings_of((*v)["settledRuns"]);
  p.finished = strings_of((*v)["finished"]);
  p.perks_introduced = strings_of((*v)["perksIntroduced"]);
  return p;
}

std::string default_prefs_path() {
  namespace fs = std::filesystem;
  if (const std::string dir = env("TRADELAB_DATA_DIR"); !dir.empty()) return (fs::path(dir) / "prefs.json").string();
#if defined(__APPLE__)
  return (fs::path(env("HOME")) / "Library/Application Support/TradeLab/prefs.json").string();
#else
  const std::string xdg = env("XDG_DATA_HOME");
  const fs::path base = xdg.empty() ? fs::path(env("HOME")) / ".local/share" : fs::path(xdg);
  return (base / "tradelab/prefs.json").string();
#endif
}

GamePrefs PrefsFile::load() const {
  std::ifstream in(path_, std::ios::binary);
  if (!in) return {};
  std::ostringstream text;
  text << in.rdbuf();
  return prefs_from_json(text.str());
}

bool PrefsFile::save(const GamePrefs& prefs) const {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path target(path_);
  if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
  const fs::path tmp = target.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << prefs_to_json(prefs);
    if (!out.flush()) return false;
  }
  fs::rename(tmp, target, ec);
  return !ec;
}

}  // namespace tradelab::desktop
