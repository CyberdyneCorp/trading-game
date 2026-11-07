// Run files and the replay viewer on the desktop (web RunImport/RunExport/ReplayScreen and the session's
// viewing mode; add-run-sharing-and-journal-stats D2, D5, D6, D7). The engine checks, seals and
// replays; storage work runs on the simulation worker (runfiles::RunFileService) and reports back here.
#include <algorithm>

#include <tradelab/c_api.h>

#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop {

namespace {

// The refusal's text in the player's language (web refusalKey): the engine's reason with its detail.
std::string refusal_text(const AppViewModel& vm, const std::string& reason, const std::string& path, const std::string& detail, int policy) {
  std::string why;
  if (reason == "policy_mismatch") why = vm.tr("runfile.reason.policy_mismatch", {{"file", detail}, {"engine", std::to_string(policy)}});
  else if (reason == "invalid_field") why = vm.tr("runfile.reason.invalid_field", {{"path", path.empty() ? "?" : path}});
  else if (vm.catalog().has("runfile.reason." + reason)) why = vm.tr("runfile.reason." + reason);
  else why = vm.tr("runfile.reason.malformed");
  return vm.tr("runfile.refused", {{"reason", why}});
}

int engine_policy() {
  const auto v = json::parse(tl_version());
  return v ? static_cast<int>((*v)["executionPolicy"].as_int()) : 0;
}

}  // namespace

void AppViewModel::attach_run_files(runfiles::RunFileCommands* commands) {
  run_files_ = commands;
  if (run_files_ != nullptr) run_files_->list_imports();
}

std::vector<const RunRecord*> AppViewModel::exportable_runs() const {
  std::vector<const RunRecord*> out;
  for (auto it = shell_.progress.runs.rbegin(); it != shell_.progress.runs.rend(); ++it) out.push_back(&*it);
  return out;
}

void AppViewModel::set_run_file_path(std::string path) {
  if (path == shell_.run_files.path) return;
  shell_.run_files.path = std::move(path);
  touch();
}

void AppViewModel::set_run_file_notes(bool on) {
  shell_.run_files.notes = on;
  touch();
}

void AppViewModel::pick_run(int index) {
  shell_.run_files.run = std::max(0, index);
  touch();
}

void AppViewModel::pick_import(int index) {
  shell_.run_files.imported = std::max(0, index);
  touch();
}

void AppViewModel::run_file_import() {
  if (run_files_ == nullptr || shell_.run_files.path.empty()) return;
  run_files_->import_file(shell_.run_files.path);
}

void AppViewModel::run_file_export() {
  const auto runs = exportable_runs();
  const auto i = static_cast<std::size_t>(shell_.run_files.run);
  if (run_files_ == nullptr || i >= runs.size()) return;
  std::string dir = shell_.run_files.path;
  if (dir.ends_with(".tlrun") || dir.ends_with(".json")) dir = dir.substr(0, dir.find_last_of("/\\") == std::string::npos ? 0 : dir.find_last_of("/\\"));
  run_files_->export_run(runs[i]->run_id, shell_.run_files.notes, dir);
}

void AppViewModel::run_file_watch_own() {
  const auto runs = exportable_runs();
  const auto i = static_cast<std::size_t>(shell_.run_files.run);
  if (run_files_ == nullptr || i >= runs.size()) return;
  shell_.run_files.watch_pending = true;
  run_files_->own_file(runs[i]->run_id);
}

void AppViewModel::run_file_watch_import() {
  const auto i = static_cast<std::size_t>(shell_.run_files.imported);
  if (i >= shell_.run_files.imports.size()) return;
  const storage::ImportRecord& rec = shell_.run_files.imports[i];
  start_watch(rec.text, true, rec.id);
}

void AppViewModel::run_file_remove_import() {
  const auto i = static_cast<std::size_t>(shell_.run_files.imported);
  if (run_files_ == nullptr || i >= shell_.run_files.imports.size()) return;
  run_files_->remove_import(shell_.run_files.imports[i].id);
  shell_.run_files.imported = 0;
}

void AppViewModel::stats_recompute() {
  if (run_files_ == nullptr || shell_.run_files.recomputing) return;
  shell_.run_files.recomputing = true;
  shell_.run_files.stats_message = tr("stats.recomputing", {{"done", "0"}, {"total", "?"}});
  run_files_->recompute();
  touch();
}

// The viewer replaces the run in the worker: a run of this session that could still resume is offered
// from storage instead (its log is there), as an earlier launch's run would be.
void AppViewModel::start_watch(std::string text, bool imported, std::string id) {
  if (resumable() && !saved_run_ && snap_) {
    saved_run_ = SavedRun{shell_.run_mission, snap_->seed, shell_.run_id, shell_.run_campaign, shell_.run_pack, shell_.run_assisted, snap_->equity};
  }
  Viewing v{imported, std::move(id), text, static_cast<std::uint8_t>(screen_)};
  if (!send([&] { engine_.watch(text); })) return;
  shell_.run_files.viewing = std::move(v);
}

void AppViewModel::watch_result(const CommandResult& r) {
  auto& files = shell_.run_files;
  if (!files.viewing) return;
  if (!r.accepted) {
    files.message = refusal_text(*this, r.reason, {}, {}, engine_policy());
    files.message_ok = false;
    files.viewing.reset();
    return;
  }
  shell_.run_id.clear();  // nothing below is recorded, settled or offered as resumable
  shell_.run_campaign.reset();
  shell_.run_assisted.reset();
  shell_.run_pack = snap_ && snap_->state["knownScenario"].as_bool(false) ? snap_->mission : std::string();
  shell_.run_settled = true;
  run_mission_ = snap_ ? snap_->mission : std::string();
  shell_.run_mission = run_mission_;
  screen_ = Screen::Game;
}

void AppViewModel::leave_replay() {
  const auto back = shell_.run_files.viewing ? static_cast<Screen>(shell_.run_files.viewing->return_to) : Screen::Menu;
  shell_.run_files.viewing.reset();
  notice_.clear();
  screen_ = back == Screen::Game || back == Screen::Debrief ? Screen::Menu : back;
  if (screen_ == Screen::Journal && run_files_ != nullptr) run_files_->list_imports();
  touch();
}

void AppViewModel::apply_run_file_message(const runfiles::RunFileEvent& e) {
  auto& files = shell_.run_files;
  const int policy = engine_policy();
  using Kind = runfiles::RunFileEvent::Kind;
  if (e.kind == Kind::Imported) {
    const auto& o = e.imported;
    files.message_ok = o.ok;
    files.message = o.ok ? tr("runfile.imported", {{"mission", mission_text(o.record.mission, "title")}, {"seed", std::to_string(o.record.seed)}})
                         : refusal_text(*this, o.reason, o.path, o.detail, policy);
  } else if (e.kind == Kind::Exported) {
    const auto& o = e.exported;
    files.message_ok = o.ok && !e.written.empty() && !o.mismatch;
    if (!o.ok && catalog_.has("runfile.block." + o.reason)) files.message = tr("runfile.block." + o.reason);
    else if (!o.ok) files.message = refusal_text(*this, o.reason, {}, o.detail, policy);
    else if (e.written.empty()) files.message = tr("desktop.runfile.writeFailed", {{"name", o.name}});
    else files.message = (o.mismatch ? tr("runfile.mismatch") + " " : std::string()) + tr("desktop.runfile.written", {{"path", e.written}});
  }
}

void AppViewModel::on_run_file_event(const runfiles::RunFileEvent& e) {
  using Kind = runfiles::RunFileEvent::Kind;
  auto& files = shell_.run_files;
  if (e.kind == Kind::Watch) {
    if (!files.watch_pending) return;
    files.watch_pending = false;
    if (e.exported.ok) start_watch(e.exported.text, false, {});
    else apply_run_file_message(runfiles::RunFileEvent{Kind::Exported, {}, {}, e.exported, {}, {}});
    return touch();
  }
  if (e.kind == Kind::Recomputed) {
    files.recomputing = false;
    for (const auto& [id, summary] : e.recomputed.summaries) {
      const auto r = std::ranges::find_if(shell_.progress.runs, [&](const RunRecord& x) { return x.run_id == id; });
      if (r != shell_.progress.runs.end()) r->summary = summary;
    }
    files.stats_message = tr("stats.recomputed", {{"done", std::to_string(e.recomputed.done)}, {"skipped", std::to_string(e.recomputed.skipped)}});
    save_progress();
    return touch();
  }
  if (e.kind == Kind::Imports || e.kind == Kind::Imported) files.imports = e.imports;
  files.imported = std::min(files.imported, std::max(0, static_cast<int>(files.imports.size()) - 1));
  apply_run_file_message(e);
  touch();
}

}  // namespace tradelab::desktop
