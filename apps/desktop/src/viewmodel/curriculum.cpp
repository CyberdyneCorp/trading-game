#include "viewmodel/curriculum.hpp"

#include <algorithm>

namespace tradelab::desktop {

namespace {

bool has_content(const Stage& s) { return !s.missions.empty() || !s.lessons.empty(); }

// Cleared = passed, or a stage without a mission (lessons only, or no content yet) whose own
// prerequisites are cleared: lessons never gate the campaign, so they never block a later stage.
bool cleared(const WebContent& c, std::string_view id, const Ids& assessed) {
  const Stage* s = c.stage(id);
  if (s == nullptr) return false;
  if (!s->missions.empty()) return stage_passed(c, id, assessed);
  return std::ranges::all_of(s->requires_, [&](const std::string& r) { return cleared(c, r, assessed); });
}

}  // namespace

bool contains(const Ids& ids, std::string_view id) { return std::ranges::find(ids, id) != ids.end(); }

std::string_view stage_status_name(StageStatus s) {
  switch (s) {
    case StageStatus::Passed: return "passed";
    case StageStatus::Open: return "open";
    case StageStatus::Locked: return "locked";
    case StageStatus::Soon: return "soon";
  }
  return "soon";
}

Ids campaign_missions(const WebContent& c) {
  Ids out;
  for (const auto& s : c.stages) {
    for (const auto& m : s.missions) {
      if (!contains(out, m)) out.push_back(m);
    }
  }
  return out;
}

// A stage with missions is passed by its missions only (its Academy lessons prepare for it and never
// gate it); a lesson-only stage when its lessons are done (add-academy-and-strategies design D9).
bool stage_passed(const WebContent& c, std::string_view id, const Ids& assessed, const Ids& lessons_done) {
  const Stage* s = c.stage(id);
  if (s == nullptr || !has_content(*s)) return false;
  const auto in = [](const Ids& ids) { return [&ids](const std::string& x) { return contains(ids, x); }; };
  if (!s->missions.empty()) return std::ranges::all_of(s->missions, in(assessed));
  return std::ranges::all_of(s->lessons, in(lessons_done));
}

StageStatus stage_status(const WebContent& c, std::string_view id, const Ids& assessed, const Ids& lessons_done) {
  const Stage* s = c.stage(id);
  if (s == nullptr || !has_content(*s)) return StageStatus::Soon;
  if (stage_passed(c, id, assessed, lessons_done)) return StageStatus::Passed;
  const bool open = std::ranges::all_of(s->requires_, [&](const std::string& r) { return cleared(c, r, assessed); });
  return open ? StageStatus::Open : StageStatus::Locked;
}

Ids stages_of(const WebContent& c, std::string_view mission) {
  Ids out;
  for (const auto& s : c.stages) {
    if (contains(s.missions, mission)) out.push_back(s.id);
  }
  return out;
}

Ids stages_passed_with(const WebContent& c, std::string_view mission, const Ids& assessed) {
  Ids out;
  for (const auto& s : stages_of(c, mission)) {
    if (stage_passed(c, s, assessed)) out.push_back(s);
  }
  return out;
}

std::string unlock_stage(const WebContent& c, std::string_view unlock) {
  for (const auto& s : c.stages) {
    if (contains(s.unlocks, unlock)) return s.id;
  }
  return {};
}

Permissions campaign_permissions(const WebContent& c, std::string_view mission, const Ids& assessed) {
  const Ids own = stages_of(c, mission);
  Permissions p;
  for (const auto& s : c.stages) {
    for (const auto& u : s.unlocks) {
      const std::string stage = unlock_stage(c, u);
      const bool ok = contains(own, stage) || stage_passed(c, stage, assessed);
      (ok ? p.granted : p.locked).push_back(u);
    }
  }
  if (contains(p.locked, "leverage")) p.leverage_cap = c.locked_leverage_cap;
  return p;
}

CurriculumSummary curriculum_summary(const WebContent& c, const Ids& assessed, const Ids& lessons_done) {
  CurriculumSummary out;
  for (const auto& s : c.stages) {
    if (!has_content(s)) continue;
    ++out.with_content;
    if (stage_passed(c, s.id, assessed, lessons_done)) ++out.passed;
  }
  return out;
}

}  // namespace tradelab::desktop
