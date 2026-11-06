#include "model/game_prefs.hpp"

#include <algorithm>
#include <array>
#include <charconv>
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

std::string number(double v) {
  std::array<char, 32> buf{};
  const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), v);
  return {buf.data(), r.ptr};
}

std::string audio_json(const AudioPrefs& a) {
  return std::string("{\"muted\":") + (a.muted ? "true" : "false") + ",\"music\":" + number(a.music) +
         ",\"sfx\":" + number(a.sfx) + "}";
}

double volume(const json::Value& v, double fallback) {
  return v.kind() == json::Value::Kind::Number ? std::clamp(v.as_double(fallback), 0.0, 1.0) : fallback;
}

AudioPrefs audio_of(const json::Value& v) {
  AudioPrefs a;
  if (v.kind() != json::Value::Kind::Object) return a;
  a.muted = v["muted"].as_bool(false);
  a.music = volume(v["music"], a.music);
  a.sfx = volume(v["sfx"], a.sfx);
  return a;
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
         ",\"finished\":" + list(p.finished) + ",\"perksIntroduced\":" + list(p.perks_introduced) +
         ",\"audio\":" + audio_json(p.audio) + ",\"layout\":" + json_string(p.layout) + "}\n";
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
  p.audio = audio_of((*v)["audio"]);
  if ((*v)["layout"].kind() == json::Value::Kind::String) p.layout = (*v)["layout"].as_string();
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
