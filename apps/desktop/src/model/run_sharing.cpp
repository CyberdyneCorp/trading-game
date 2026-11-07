#include "model/run_sharing.hpp"

#include <algorithm>
#include <filesystem>
#include <span>
#include <fstream>
#include <sstream>

#include "model/engine_host.hpp"
#include "model/storage/json_writer.hpp"
#include "model/run_summary.hpp"

namespace tradelab::desktop::runfiles {

namespace {

// The pack a catalog-pack file needs, when installed: bytes and the trusted keys.
struct PackArg {
  std::vector<std::uint8_t> bytes;
  std::string keys = "[]";
};

std::optional<PackArg> installed_pack(storage::Storage& s, const std::string& digest) {
  if (digest.empty()) return std::nullopt;
  auto bytes = s.packs().bytes(digest);
  if (!bytes) return std::nullopt;
  return PackArg{std::move(*bytes), s.packs().trusted_keys_json()};
}

json::Value call_with_pack(json::Value (*fn)(const std::string&, std::span<const std::uint8_t>, const std::string&), const std::string& text,
                           const std::optional<PackArg>& pack) {
  return pack ? fn(text, pack->bytes, pack->keys) : fn(text, {}, "[]");
}

void refusal_from(const json::Value& v, std::string& reason, std::string& path, std::string& detail) {
  reason = v["reason"].as_string("malformed");
  path = v["path"].as_string();
  detail = v["detail"].as_string();
}

// The run's sealed file, or why it cannot be exported.
ExportOutcome seal_own(storage::Storage& s, const std::string& run_id, const EngineStamp& stamp, bool notes, json::Value* verdict) {
  ExportOutcome out;
  const auto run = s.store().stored_run(run_id);
  if (!run) {
    out.reason = "unknown_run";
    return out;
  }
  if (const auto block = export_block(run->record, run->actions.size(), s.store().packs())) {
    out.reason = *block;
    return out;
  }
  const std::string draft = run_file_draft(run->record, run->actions, stamp, notes);
  const json::Value v = call_with_pack(seal_run_file, draft, installed_pack(s, run->record.pack_digest.value_or("")));
  if (!v["ok"].as_bool()) {
    std::string path;
    refusal_from(v, out.reason, path, out.detail);
    return out;
  }
  out.ok = true;
  out.name = run_file_name(run->record.mission_id, run->record.seed);
  out.text = v["run"].as_string() + "\n";
  out.mismatch = !run->record.digest.empty() && run->record.digest != v["digest"].as_string();
  if (verdict != nullptr) *verdict = v;
  return out;
}

}  // namespace

ImportOutcome import_text(storage::Storage& s, const std::string& text) {
  ImportOutcome out;
  if (text.size() > kRunFileMaxBytes) {
    out.reason = "too_large";
    return out;
  }
  json::Value v = verify_run_file(text);
  if (!v["ok"].as_bool() && v["reason"].as_string() == "pack_required") {
    if (auto pack = installed_pack(s, v["pack"].as_string())) v = verify_run_file(text, pack->bytes, pack->keys);
  }
  if (!v["ok"].as_bool()) {
    refusal_from(v, out.reason, out.path, out.detail);
    return out;
  }
  const std::string pack = v["pack"].as_string();
  out.record = {v["fileDigest"].as_string(), v["run"].as_string(), v["mission"].as_string(), static_cast<std::uint32_t>(v["seed"].as_int()),
                pack.empty() ? std::nullopt : std::optional<std::string>(pack), v["status"].as_string("active"), storage::now_ms()};
  s.store().save_import(out.record);
  out.ok = true;
  return out;
}

ExportOutcome export_run(storage::Storage& s, const std::string& run_id, const EngineStamp& stamp, bool include_notes) {
  return seal_own(s, run_id, stamp, include_notes, nullptr);
}

ExportOutcome own_file(storage::Storage& s, const std::string& run_id, const EngineStamp& stamp) {
  return seal_own(s, run_id, stamp, true, nullptr);
}

RecomputeOutcome recompute_summaries(storage::Storage& s, const EngineStamp& stamp) {
  RecomputeOutcome out;
  std::vector<std::string> have;
  for (const auto& [id, json] : s.store().summaries()) have.push_back(id);
  for (const storage::RunRecord& r : s.store().runs()) {
    const bool ended = r.status == "finished" || r.status == "bankrupt";
    if (!ended || r.assisted || std::find(have.begin(), have.end(), r.run_id) != have.end()) continue;
    json::Value v;
    const ExportOutcome sealed = seal_own(s, r.run_id, stamp, false, &v);
    const auto summary = sealed.ok && !sealed.mismatch ? summarize(v["debrief"]) : std::nullopt;
    if (!summary) {
      out.skipped += 1;
      continue;
    }
    const std::string text = summary_json(*summary);
    s.store().set_summary(r.run_id, text);
    out.summaries.emplace_back(r.run_id, text);
    out.done += 1;
  }
  return out;
}

std::optional<std::string> read_run_file(const std::string& path, std::string* why) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    *why = "unreadable";
    return std::nullopt;
  }
  if (size > kRunFileMaxBytes) {
    *why = "too_large";
    return std::nullopt;
  }
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  if (!in && !in.eof()) {
    *why = "unreadable";
    return std::nullopt;
  }
  return text.str();
}

// ---- service: every job on the worker, reports on the UI thread ----

RunFileService::RunFileService(EngineHost& host, EngineStamp stamp, Post post, Sink sink)
    : host_(host), stamp_(std::move(stamp)), post_(std::move(post)), shared_(std::make_shared<Shared>()) {
  shared_->sink = std::move(sink);
}

RunFileService::~RunFileService() {
  std::lock_guard lock(shared_->mu);
  shared_->closed = true;
}

void RunFileService::report(RunFileEvent e) const {
  post_([shared = shared_, e = std::move(e)] {
    Sink sink;
    {
      std::lock_guard lock(shared->mu);
      if (shared->closed) return;
      sink = shared->sink;
    }
    if (sink) sink(e);
  });
}

void RunFileService::list_imports() {
  host_.post_storage_job([this](storage::Storage& s) {
    RunFileEvent e;
    e.imports = s.store().imports();
    report(std::move(e));
  });
}

void RunFileService::import_file(const std::string& path) {
  host_.post_storage_job([this, path](storage::Storage& s) {
    RunFileEvent e;
    e.kind = RunFileEvent::Kind::Imported;
    std::string why;
    const auto text = read_run_file(path, &why);
    if (text) e.imported = import_text(s, *text);
    else e.imported.reason = why;
    e.imports = s.store().imports();
    report(std::move(e));
  });
}

void RunFileService::export_run(const std::string& run_id, bool include_notes, const std::string& dir) {
  host_.post_storage_job([this, run_id, include_notes, dir](storage::Storage& s) {
    RunFileEvent e;
    e.kind = RunFileEvent::Kind::Exported;
    e.exported = runfiles::export_run(s, run_id, stamp_, include_notes);
    if (e.exported.ok) {
      std::error_code ec;
      const std::filesystem::path folder = dir.empty() ? s.dir() / "exports" : std::filesystem::path(dir);
      std::filesystem::create_directories(folder, ec);
      const std::filesystem::path file = folder / e.exported.name;
      std::ofstream out(file, std::ios::binary | std::ios::trunc);
      out << e.exported.text;
      if (out.good()) e.written = file.string();
    }
    report(std::move(e));
  });
}

void RunFileService::remove_import(const std::string& id) {
  host_.post_storage_job([this, id](storage::Storage& s) {
    s.store().delete_import(id);
    RunFileEvent e;
    e.imports = s.store().imports();
    report(std::move(e));
  });
}

void RunFileService::own_file(const std::string& run_id) {
  host_.post_storage_job([this, run_id](storage::Storage& s) {
    RunFileEvent e;
    e.kind = RunFileEvent::Kind::Watch;
    e.exported = runfiles::own_file(s, run_id, stamp_);
    report(std::move(e));
  });
}

void RunFileService::recompute() {
  host_.post_storage_job([this](storage::Storage& s) {
    RunFileEvent e;
    e.kind = RunFileEvent::Kind::Recomputed;
    e.recomputed = recompute_summaries(s, stamp_);
    report(std::move(e));
  });
}

}  // namespace tradelab::desktop::runfiles
