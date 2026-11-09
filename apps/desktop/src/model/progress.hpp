// The player's local progress and display settings, as the web keeps them (`meta.progress` v2 and the
// runs store for progress, `tradelab.settings.v1` and `tradelab.keymap.v1` for settings). Plain data:
// the rules that change them are ported in viewmodel/ (campaign, curriculum, journal, keymap).
//
// Repositories: the interfaces below are what viewmodels read and write. The in-memory ones keep
// the data for the session (tests, or no saved-data folder); the app uses the SQLite-backed ones
// (model/storage/profile_store.hpp).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/i18n.hpp"

namespace tradelab::desktop {

// ---- Campaign (campaign.ts, campaignReport.ts) ----

enum class LegResult : std::uint8_t { Finished, Bankrupt, Left };
enum class CampaignStatus : std::uint8_t { Active, Bankrupt, Complete, Abandoned };

struct LegTrade {
  std::string net;
  std::optional<int> r_bp;  // R multiple in basis points; none without a planned risk
  std::string regime;       // "" = unlabeled
};

// What the campaign report needs from one mission's engine debrief.
struct LegReport {
  std::vector<LegTrade> trades;
  std::string net_pnl;
  std::string fees;
  std::string spread_slippage;
  std::string funding;  // received (+) or paid (-)
};

struct CampaignLeg {
  std::string run_id;
  std::string mission_id;
  std::string start_capital;
  std::string end_capital;
  LegResult result = LegResult::Finished;
  std::optional<LegReport> report;
};

struct CampaignRun {
  std::string id;
  std::int64_t started_at = 0;
  std::string capital;  // what the next mission starts with
  int index = 0;        // next mission in the campaign order
  CampaignStatus status = CampaignStatus::Active;
  std::vector<CampaignLeg> legs;
};

// Links a run to the campaign mission it plays.
struct CampaignLink {
  std::string id;
  int index = 0;
  bool operator==(const CampaignLink&) const = default;
};

// How an assisted practice attempt was made (web assisted.ts AssistedLink): rewound to a decision of
// the run `from`, or the same mission on another market ("seed").
struct AssistedLink {
  std::string from;
  std::string kind = "rewind";  // rewind | seed
  std::optional<int> step;      // rewind: the decision step the attempt resumes at
  bool operator==(const AssistedLink&) const = default;
};

// ---- Runs and their journal (analysis.ts PlanEntry, storage.ts RunRecord) ----

// A planned trailing stop (add-practice-and-advanced-strategies D10): "Protect with: trailing stop"
// records the rule with the plan; it is offered after the fill and never sent without a click. Client
// data only: it reaches no action log or run file.
struct PlanTrail {
  bool pct = false;   // false: a price distance
  std::string value;  // the distance or the percent as typed
  bool operator==(const PlanTrail&) const = default;
};

struct PlanEntry {
  int step = 0;
  bool buy = true;
  std::string thesis;
  std::optional<double> invalidation;
  std::optional<double> target;
  int confidence = 50;
  std::optional<double> planned_risk;
  bool skipped = false;
  std::string lose_scenario;
  std::string instrument;  // option series label or combo name ("Iron condor · 4 legs"); empty = perpetual
  std::string combo;       // the strategy-builder combo this plan opened ("c1"); empty = none
  std::optional<PlanTrail> trail;
};

enum class RunStatus : std::uint8_t { Active, Finished, Bankrupt, Left };

struct RunRecord {
  std::string run_id;
  std::string mission_id;
  std::uint32_t seed = 0;
  std::int64_t created_at = 0;  // ms since the epoch
  std::optional<CampaignLink> campaign;
  RunStatus status = RunStatus::Active;
  bool intel = false;
  bool assisted = false;
  std::string pack_digest;  // empty for a built-in mission
  std::string equity;       // last known equity (a left campaign mission carries it)
  std::vector<PlanEntry> journal;
  // The journal statistics' summary of the run's debrief (web RunRecord.summary JSON); empty = none.
  std::string summary;
};

// A lesson's spaced-review schedule (review.ts ReviewEntry): box 1..4 while scheduled, kReviewRetired
// once retired; `due` is a local calendar day ("YYYY-MM-DD").
inline constexpr int kReviewRetired = 5;
struct ReviewEntry {
  int box = 1;
  std::string due;
  bool operator==(const ReviewEntry&) const = default;
};
using LessonReviews = std::map<std::string, ReviewEntry, std::less<>>;

struct Progress {
  std::vector<std::string> completed;     // missions played to the end (not bankrupt)
  std::vector<std::string> assessed;      // missions finished in a campaign run: their stages are passed
  std::vector<std::string> lessons_done;  // Academy lessons completed
  LessonReviews lesson_reviews;           // spaced reviews of completed lessons (progress v3)
  std::vector<std::string> coach_done;    // guided tours finished or skipped ("first-trade")
  std::vector<std::string> glossary_seen; // glossary terms opened (the "new" mark)
  std::optional<CampaignRun> campaign;
  std::vector<CampaignRun> campaign_history;  // ended campaign runs, oldest first
  std::vector<RunRecord> runs;
};

class ProgressRepository {
 public:
  virtual ~ProgressRepository() = default;
  [[nodiscard]] virtual Progress load() = 0;
  virtual void save(const Progress& progress) = 0;
};

class MemoryProgressRepository final : public ProgressRepository {
 public:
  MemoryProgressRepository() = default;
  explicit MemoryProgressRepository(Progress initial) : progress_(std::move(initial)) {}
  [[nodiscard]] Progress load() override { return progress_; }
  void save(const Progress& progress) override { progress_ = progress; }

 private:
  Progress progress_;
};

// ---- Display settings (settings.svelte.ts, shortcuts.ts keymap) ----

struct CrtSettings {
  bool enabled = true;  // the web's settings.crt master toggle
  bool scanlines = true;
  bool bloom = true;
  bool noise = true;
  bool operator==(const CrtSettings&) const = default;
};

struct Settings {
  Locale locale = Locale::PtBR;
  CrtSettings crt;
  bool colorblind = false;
  float text_scale = 1.0f;
  std::map<std::string, std::string> keymap;  // action -> key; empty = the web's defaults
  // The day the menu's review prompt was dismissed ("YYYY-MM-DD"; the web's tradelab.reviewDismissed.v1).
  std::string review_dismissed_on;
};

class SettingsRepository {
 public:
  virtual ~SettingsRepository() = default;
  [[nodiscard]] virtual std::optional<Settings> load() = 0;
  virtual void save(const Settings& settings) = 0;
};

class MemorySettingsRepository final : public SettingsRepository {
 public:
  [[nodiscard]] std::optional<Settings> load() override { return settings_; }
  void save(const Settings& settings) override { settings_ = settings; }

 private:
  std::optional<Settings> settings_;
};

}  // namespace tradelab::desktop
