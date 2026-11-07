#include "model/storage/profile_store.hpp"

#include <algorithm>
#include <system_error>

#include "model/progress_json.hpp"
#include "model/storage/json_writer.hpp"

namespace tradelab::desktop::storage {

namespace {

constexpr const char* kDisplayKey = "display";
constexpr const char* kKeymapKey = "keymap";
constexpr const char* kAudioKey = "audio";
constexpr const char* kLayoutKey = "layout";
constexpr const char* kLegacyFinishedKey = "legacyFinished";  // finished missions from prefs.json

json::Value parse_or_null(const std::optional<std::string>& text) {
  if (!text) return {};
  auto v = json::parse(*text);
  return v ? *v : json::Value{};
}

std::vector<std::string> strings_of(const json::Value& v) {
  std::vector<std::string> out;
  for (const json::Value& item : v.items()) {
    if (item.kind() == json::Value::Kind::String) out.push_back(item.as_string());
  }
  return out;
}

void add_missing(std::vector<std::string>& to, const std::vector<std::string>& from) {
  for (const std::string& s : from) {
    if (std::ranges::find(to, s) == to.end()) to.push_back(s);
  }
}

RunStatus status_of(const std::string& s) {
  if (s == "finished") return RunStatus::Finished;
  if (s == "bankrupt") return RunStatus::Bankrupt;
  return s == "left" ? RunStatus::Left : RunStatus::Active;
}

desktop::RunRecord shell_record(const RunRecord& r) {
  desktop::RunRecord out;
  out.run_id = r.run_id;
  out.mission_id = r.mission_id;
  out.seed = r.seed;
  out.created_at = r.created_at;
  if (r.campaign) out.campaign = progress_json::link_from(parse_or_null(r.campaign));
  out.status = status_of(r.status);
  out.intel = r.options.news_lead > 0 || r.options.calendar;
  out.assisted = r.assisted.has_value();
  out.pack_digest = r.pack_digest.value_or("");
  out.equity = r.equity.value_or("");
  out.journal = progress_json::journal_from(parse_or_null(r.journal));
  return out;
}

std::string list_json(const std::vector<std::string>& items) {
  JsonWriter w;
  w.begin_array();
  for (const std::string& s : items) w.string(s);
  return w.end_array().text();
}

}  // namespace

ProfileStore::ProfileStore(const std::filesystem::path& db_file) : store_(db_file.string()) {}

template <class Fn>
bool ProfileStore::guarded(Fn&& fn) {
  try {
    fn();
    return true;
  } catch (const Error&) {
    durable_ = false;
    return false;
  }
}

bool ProfileStore::import_prefs_file(const std::filesystem::path& file) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return false;
  PrefsFile legacy(file.string());
  const GamePrefs old = legacy.load();
  const bool ok = guarded([&] {
    Progress p = store_.progress();
    add_missing(p.tutorials_seen, old.tutorials_seen);
    p.advisor_points = old.advisor_points;
    add_missing(p.settled_runs, old.settled_runs);
    add_missing(p.perks_introduced, old.perks_introduced);
    store_.save_progress(p);
    if (!old.keymap.empty() && !store_.setting(kKeymapKey)) store_.set_setting(kKeymapKey, progress_json::keymap(old.keymap));
    if (!store_.setting(kAudioKey)) store_.set_setting(kAudioKey, audio_prefs_json(old.audio));
    if (!old.layout.empty() && !store_.setting(kLayoutKey)) store_.set_setting(kLayoutKey, old.layout);
    std::vector<std::string> finished = strings_of(parse_or_null(store_.meta(kLegacyFinishedKey)));
    add_missing(finished, old.finished);
    store_.set_meta(kLegacyFinishedKey, list_json(finished));
  });
  if (!ok) return false;
  std::filesystem::rename(file, std::filesystem::path(file.string() + ".imported"), ec);
  return true;
}

std::vector<std::string> ProfileStore::finished() {
  std::vector<std::string> out;
  guarded([&] {
    out = store_.finished_missions();
    add_missing(out, strings_of(parse_or_null(store_.meta(kLegacyFinishedKey))));
  });
  return out;
}

desktop::Progress ProfileStore::load_progress() {
  desktop::Progress out;
  guarded([&] {
    const Progress p = store_.progress();
    out.lessons_done = p.lessons_done;
    out.lesson_reviews = progress_json::reviews_from(parse_or_null(p.lesson_reviews));
    out.coach_done = p.coach_done;
    out.glossary_seen = p.glossary_seen;
    out.assessed = p.assessed;
    if (p.campaign) out.campaign = progress_json::campaign_from(parse_or_null(p.campaign));
    for (const std::string& c : p.campaign_history) {
      if (auto run = progress_json::campaign_from(parse_or_null(c))) out.campaign_history.push_back(std::move(*run));
    }
    auto runs = store_.runs();  // newest first; the shell keeps them oldest first
    std::ranges::reverse(runs);
    for (const RunRecord& r : runs) {
      stored_[r.run_id] = StoredColumns{r.journal, r.campaign.value_or(""), r.status};
      out.runs.push_back(shell_record(r));
    }
  });
  out.completed = finished();
  return out;
}

// Only what the shell owns: the journal, the campaign link and a run it left without starting another
// (ending a campaign); digest, status and equity of a run in play are the worker's.
RunPatch ProfileStore::shell_patch(const desktop::RunRecord& r, const StoredColumns& stored) {
  RunPatch patch;
  const std::string journal = progress_json::journal(r.journal);
  if (journal != stored.journal) patch.journal = journal;
  const std::string campaign = r.campaign ? progress_json::link(*r.campaign) : std::string();
  if (!campaign.empty() && campaign != stored.campaign) patch.campaign = campaign;
  if (r.status == RunStatus::Left && stored.status == "active") {
    patch.status = "left";
    if (!r.equity.empty()) patch.equity = r.equity;
  }
  return patch;
}

void ProfileStore::save_run(const desktop::RunRecord& r) {
  auto it = stored_.find(r.run_id);
  if (it == stored_.end()) {
    const auto row = store_.run(r.run_id);
    if (!row) return;  // a run the store does not know (no storage when it started)
    it = stored_.emplace(r.run_id, StoredColumns{row->journal, row->campaign.value_or(""), row->status}).first;
  }
  const RunPatch patch = shell_patch(r, it->second);
  if (!patch.journal && !patch.campaign && !patch.status) return;
  store_.update_run(r.run_id, patch);
  if (patch.journal) it->second.journal = *patch.journal;
  if (patch.campaign) it->second.campaign = *patch.campaign;
  if (patch.status) it->second.status = *patch.status;
}

void ProfileStore::save_progress(const desktop::Progress& p) {
  guarded([&] {
    Progress row = store_.progress();
    row.lessons_done = p.lessons_done;
    row.lesson_reviews = progress_json::reviews(p.lesson_reviews);
    row.coach_done = p.coach_done;
    row.glossary_seen = p.glossary_seen;
    row.assessed = p.assessed;
    row.campaign = p.campaign ? std::optional<std::string>(progress_json::campaign(*p.campaign)) : std::nullopt;
    row.campaign_history.clear();
    for (const CampaignRun& c : p.campaign_history) row.campaign_history.push_back(progress_json::campaign(c));
    store_.save_progress(row);
    for (const desktop::RunRecord& r : p.runs) save_run(r);
  });
}

std::optional<Settings> ProfileStore::load_settings() {
  std::optional<Settings> out;
  guarded([&] {
    const auto display = store_.setting(kDisplayKey);
    const auto keys = store_.setting(kKeymapKey);
    if (!display && !keys) return;
    out = progress_json::settings_from(parse_or_null(display));
    out->keymap = progress_json::keymap_from(parse_or_null(keys));
  });
  return out;
}

void ProfileStore::save_settings(const Settings& s) {
  guarded([&] {
    store_.set_setting(kDisplayKey, progress_json::settings(s));
    store_.set_setting(kKeymapKey, progress_json::keymap(s.keymap));
  });
}

GamePrefs ProfileStore::load_prefs() {
  GamePrefs out;
  guarded([&] {
    const Progress p = store_.progress();
    out.tutorials_seen = p.tutorials_seen;
    out.advisor_points = static_cast<int>(p.advisor_points);
    out.settled_runs = p.settled_runs;
    out.perks_introduced = p.perks_introduced;
    out.keymap = progress_json::keymap_from(parse_or_null(store_.setting(kKeymapKey)));
    if (const auto audio = store_.setting(kAudioKey)) out.audio = audio_prefs_from_json(*audio);
    out.layout = store_.setting(kLayoutKey).value_or("");
  });
  out.finished = finished();
  return out;
}

bool ProfileStore::save_prefs(const GamePrefs& prefs) {
  return guarded([&] {
    Progress p = store_.progress();
    p.tutorials_seen = prefs.tutorials_seen;
    p.advisor_points = prefs.advisor_points;
    p.settled_runs = prefs.settled_runs;
    p.perks_introduced = prefs.perks_introduced;
    store_.save_progress(p);
    store_.set_setting(kKeymapKey, progress_json::keymap(prefs.keymap));
    store_.set_setting(kAudioKey, audio_prefs_json(prefs.audio));
    store_.set_setting(kLayoutKey, prefs.layout);
  });
}

}  // namespace tradelab::desktop::storage
