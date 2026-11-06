// Port of apps/web/src/lib/game/curriculum.ts (names kept): stage statuses, campaign order and the
// unlocks a campaign mission may use. The stages themselves are the web's data (model/content).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/content.hpp"

namespace tradelab::desktop {

enum class StageStatus : std::uint8_t { Passed, Open, Locked, Soon };

std::string_view stage_status_name(StageStatus s);  // "passed", "open", "locked", "soon"

using Ids = std::vector<std::string>;

// Campaign missions in curriculum order (a mission assessing two stages appears once).
Ids campaign_missions(const WebContent& c);
bool stage_passed(const WebContent& c, std::string_view id, const Ids& assessed, const Ids& lessons_done = {});
StageStatus stage_status(const WebContent& c, std::string_view id, const Ids& assessed, const Ids& lessons_done = {});
Ids stages_of(const WebContent& c, std::string_view mission);
std::string unlock_stage(const WebContent& c, std::string_view unlock);

struct Permissions {
  Ids granted;
  Ids locked;
  std::optional<int> leverage_cap;  // engine start option; none = the mission's own cap
};
Permissions campaign_permissions(const WebContent& c, std::string_view mission, const Ids& assessed);

struct CurriculumSummary {
  int passed = 0;
  int with_content = 0;
};
CurriculumSummary curriculum_summary(const WebContent& c, const Ids& assessed, const Ids& lessons_done = {});

bool contains(const Ids& ids, std::string_view id);

}  // namespace tradelab::desktop
