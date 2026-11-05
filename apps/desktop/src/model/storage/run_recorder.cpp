#include "model/storage/run_recorder.hpp"

#include <tradelab/c_api.h>

#include <algorithm>

#include "model/storage/fault.hpp"

namespace tradelab::desktop::storage {

namespace {

json::Value parse_or_null(const char* text) {
  if (text == nullptr) return {};
  auto v = json::parse(text);
  return v ? *v : json::Value{};
}

json::Value parse_action(const std::string& text) {
  auto v = json::parse(text);
  return v ? *v : json::Value{};
}

// Client id after the first `n` logged actions (one per non-advance action; web restoreOrStart).
std::uint32_t client_id_after(const std::vector<std::string>& log, std::int64_t n) {
  std::uint32_t id = 1;
  for (std::int64_t i = 0; i < n; ++i) {
    if (!is_advance(parse_action(log[static_cast<std::size_t>(i)]))) ++id;
  }
  return id;
}

std::string state_digest(const EngineRun& run) {
  return parse_or_null(tl_state(run.handle()))["digest"].as_string();
}

tl_run* create_mission(const RunRecord& r) {
  const RunOptions& o = r.options;
  const char* balance = o.starting_balance.empty() ? nullptr : o.starting_balance.c_str();
  return tl_run_create_ex(r.mission_id.c_str(), r.seed, o.advice_allowance, o.news_lead, o.calendar ? 1 : 0, balance,
                          o.leverage_cap);
}

}  // namespace

EngineVersion current_engine_version() {
  const json::Value v = parse_or_null(tl_version());
  return EngineVersion{v["engine"].as_string(), static_cast<int>(v["executionPolicy"].as_int())};
}

std::string resume_notice_name(ResumeNoticeKind kind) {
  switch (kind) {
    case ResumeNoticeKind::None: return {};
    case ResumeNoticeKind::EngineChanged: return "engine_changed";
    case ResumeNoticeKind::PolicyChanged: return "policy_changed";
    case ResumeNoticeKind::DigestMismatch: return "digest_mismatch";
    case ResumeNoticeKind::PackMissing: return "pack_missing";
    case ResumeNoticeKind::Damaged: return "damaged";
  }
  return {};
}

std::string ResumeNotice::i18n_key() const {
  switch (kind) {
    case ResumeNoticeKind::EngineChanged: return "engine.pinMissing";
    case ResumeNoticeKind::PackMissing: return "pack.missing";
    default: return {};
  }
}

RunRecorder::RunRecorder(Storage& storage) : storage_(storage), engine_(current_engine_version()) {}

template <class Fn>
void RunRecorder::guarded(Fn&& fn) {
  try {
    fn();
  } catch (const Error&) {
    durable_ = false;
  }
}

void RunRecorder::begin(const RunRecord& record) {
  run_id_ = record.run_id;
  next_seq_ = 0;
  guarded([&] {
    storage_.store().create_run(record);
    storage_.store().prune_checkpoints(record.run_id);
  });
}

std::unique_ptr<EngineRun> RunRecorder::start(const std::string& mission, std::uint32_t seed, const RunOptions& options) {
  RunRecord r;
  r.run_id = new_run_id();
  r.mission_id = mission;
  r.seed = seed;
  r.options = options;
  auto run = EngineRun::adopt(create_mission(r), seed);
  if (!run) return nullptr;
  r.created_at = now_ms();
  r.execution_policy = engine_.policy;
  r.engine = engine_.engine;
  const Snapshot s = run->snapshot();
  r.digest = s.digest;
  r.equity = s.equity;
  begin(r);
  return run;
}

std::unique_ptr<EngineRun> RunRecorder::start_pack(const std::string& digest, std::uint32_t seed, std::string* reason) {
  auto set_reason = [&](const char* why) {
    if (reason != nullptr) *reason = why;
  };
  const auto entry = storage_.store().pack(digest);
  const auto bytes = storage_.packs().bytes(digest);
  if (!entry || !bytes) {
    set_reason("pack_missing");
    return nullptr;
  }
  const std::string keys = storage_.packs().trusted_keys_json();
  auto run = EngineRun::adopt(
      tl_run_create_pack(bytes->data(), static_cast<uint32_t>(bytes->size()), keys.c_str(), seed), seed);
  if (!run) {
    set_reason("pack_refused");
    return nullptr;
  }
  RunRecord r;
  r.run_id = new_run_id();
  r.mission_id = entry->scenario_id;
  r.seed = seed;
  r.created_at = now_ms();
  r.execution_policy = engine_.policy;
  r.engine = engine_.engine;
  r.pack_digest = digest;
  const Snapshot s = run->snapshot();
  r.digest = s.digest;
  r.equity = s.equity;
  begin(r);
  return run;
}

bool RunRecorder::commit(const Action& action) {
  if (!recording()) return false;
  fault(FaultPoint::BeforeCommit);
  bool ok = false;
  guarded([&] {
    storage_.store().append_action(run_id_, next_seq_, action.json);
    ok = true;
  });
  ++next_seq_;  // in memory too: the next action keeps its place in the log
  fault(FaultPoint::AfterCommit);
  return ok;
}

ApplyResult RunRecorder::perform(EngineRun& run, const Action& action) {
  commit(action);
  return apply_action(run, action);
}

void RunRecorder::settle(EngineRun& run, const Snapshot& state) {
  fault(FaultPoint::AfterApply);
  if (!recording()) return;
  guarded([&] {
    RunPatch patch;
    patch.digest = state.digest;
    patch.status = state.status;
    patch.equity = state.equity;
    storage_.store().update_run(run_id_, patch);
    if (next_seq_ == 0 || next_seq_ % kCheckpointEvery != 0) return;
    const json::Value cp = parse_or_null(tl_checkpoint(run.handle()));
    storage_.store().save_checkpoint(Checkpoint{run_id_, next_seq_, cp["checkpoint"].as_string(), state.digest,
                                                engine_.engine, std::nullopt});
  });
}

std::unique_ptr<EngineRun> RunRecorder::restore_checkpoint(const RunRecord& r, const std::vector<std::string>& log,
                                                           const std::optional<std::vector<std::uint8_t>>& pack,
                                                           std::int64_t* restored) {
  std::optional<Checkpoint> cp;
  guarded([&] { cp = storage_.store().checkpoint(r.run_id); });
  // Only valid on the engine that wrote it, and never past the committed log.
  if (!cp || cp->engine != engine_.engine || cp->actions > static_cast<std::int64_t>(log.size())) return nullptr;
  tl_run* raw = nullptr;
  if (pack) {
    const std::string keys = storage_.packs().trusted_keys_json();
    raw = tl_run_restore_pack(pack->data(), static_cast<uint32_t>(pack->size()), keys.c_str(), r.seed, cp->hex.c_str());
  } else {
    raw = tl_run_restore(r.mission_id.c_str(), r.seed, cp->hex.c_str());
  }
  auto run = EngineRun::adopt(raw, r.seed);
  if (!run || state_digest(*run) != cp->digest) return nullptr;
  run->set_next_client_id(client_id_after(log, cp->actions));
  *restored = cp->actions;
  return run;
}

std::unique_ptr<EngineRun> RunRecorder::fresh(const RunRecord& r, const std::optional<std::vector<std::uint8_t>>& pack) {
  if (!pack) return EngineRun::adopt(create_mission(r), r.seed);
  const std::string keys = storage_.packs().trusted_keys_json();
  return EngineRun::adopt(tl_run_create_pack(pack->data(), static_cast<uint32_t>(pack->size()), keys.c_str(), r.seed),
                          r.seed);
}

ResumeNotice RunRecorder::replay_notice(const RunRecord& r, std::int64_t replayed, const std::string& digest) const {
  if (!r.engine.empty() && r.engine != engine_.engine) return {ResumeNoticeKind::EngineChanged, r.engine};
  if (r.execution_policy != engine_.policy) return {ResumeNoticeKind::PolicyChanged, std::to_string(r.execution_policy)};
  if (!r.digest.empty() && r.actions == replayed && digest != r.digest) {
    return {ResumeNoticeKind::DigestMismatch, r.digest.substr(0, 8)};
  }
  return {};
}

std::optional<Resumed> RunRecorder::resume() {
  std::optional<StoredRun> stored;
  guarded([&] { stored = storage_.store().active_run(); });
  if (!stored) return std::nullopt;
  const RunRecord& r = stored->record;
  Resumed out;
  out.run_id = r.run_id;
  std::optional<std::vector<std::uint8_t>> pack;
  if (r.pack_digest) {
    pack = storage_.packs().bytes(*r.pack_digest);
    if (!pack) {
      out.notice = {ResumeNoticeKind::PackMissing, *r.pack_digest};
      return out;  // an evicted pack never starts from partial data
    }
  }
  std::int64_t restored = 0;
  auto run = restore_checkpoint(r, stored->actions, pack, &restored);
  if (!run) run = fresh(r, pack);
  if (!run) {
    out.notice = {ResumeNoticeKind::Damaged, r.mission_id};
    return out;
  }
  const auto total = static_cast<std::int64_t>(stored->actions.size());
  for (std::int64_t i = restored; i < total; ++i) apply_action(*run, parse_action(stored->actions[static_cast<std::size_t>(i)]));
  out.notice = replay_notice(r, total, state_digest(*run));
  out.actions = total;
  out.from_checkpoint = restored;
  run_id_ = r.run_id;
  next_seq_ = total;
  guarded([&] { storage_.store().prune_checkpoints(r.run_id); });
  out.run = std::move(run);
  return out;
}

}  // namespace tradelab::desktop::storage
