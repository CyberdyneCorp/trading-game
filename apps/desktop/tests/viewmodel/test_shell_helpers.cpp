// Ported web helpers of the shell screens, with the web's own cases (curriculum.test.ts,
// campaign.test.ts, campaignReport.test.ts, journal.test.ts, shortcuts.test.ts, academy.test.ts,
// objectives.test.ts, analysis/debrief texts) on the generated web content.
#include <doctest/doctest.h>

#include "model/content.hpp"
#include "model/i18n.hpp"
#include "model/json.hpp"
#include "viewmodel/campaign.hpp"
#include "viewmodel/curriculum.hpp"
#include "viewmodel/journal.hpp"
#include "viewmodel/keymap.hpp"
#include "viewmodel/lesson_runner.hpp"
#include "viewmodel/report_texts.hpp"
#include "viewmodel/shell_state.hpp"

using namespace tradelab::desktop;

namespace {

const WebContent& C() { return web_content(); }

json::Value J(std::string_view text) { return json::parse(text).value(); }

Catalog pt() { return builtin_catalog(); }

Catalog en() {
  Catalog c = builtin_catalog();
  c.set_locale(Locale::En);
  return c;
}

}  // namespace

TEST_CASE("web content: stages, lessons, shortcuts and constants come from the web sources") {
  CHECK(C().stages.size() == 14);
  CHECK(C().stages.front().id == "foundations");
  CHECK(C().locked_leverage_cap == 2);
  CHECK(C().campaign_capital == "10000");
  CHECK(C().campaign_min_capital == "100");
  CHECK(C().report_min_trades == 30);
  REQUIRE(C().lesson("foundations-bar") != nullptr);
  const auto& steps = C().lesson("foundations-bar")->steps;
  CHECK(steps.size() == 8);
  CHECK(steps[3].id == "wick");
  CHECK(steps[3].choices.size() == 4);
  CHECK(steps[3].answer == 2);
  CHECK(default_keymap(C()).at("advance5") == "Shift+Space");
  CHECK(C().shortcut_actions.size() == 11);
}

TEST_CASE("curriculum: statuses, campaign order and the leverage gate") {
  const Ids none;
  CHECK(stage_status(C(), "foundations", none) == StageStatus::Open);
  CHECK(stage_status(C(), "structure", none) == StageStatus::Open);  // lessons never gate
  CHECK(stage_status(C(), "futures1", none) == StageStatus::Locked);
  CHECK(stage_status(C(), "foundations", none, {"foundations-bar"}) == StageStatus::Passed);
  const Ids order = campaign_missions(C());
  REQUIRE(order.size() == 17);
  CHECK(order.front() == "m06-thin-book");
  CHECK(order[1] == "m01-false-breakout");
  CHECK(order.back() == "m12-capstone");
  CHECK(stage_passed(C(), "orders", {"m06-thin-book"}));
  CHECK(stage_status(C(), "futures1", {"m06-thin-book"}) == StageStatus::Open);

  const Permissions first = campaign_permissions(C(), "m06-thin-book", none);
  CHECK(first.leverage_cap == 2);
  CHECK(contains(first.locked, "short"));
  // A mission's own stage grants its unlock; passed stages keep theirs.
  CHECK(contains(campaign_permissions(C(), "m01-false-breakout", none).granted, "short"));
  CHECK_FALSE(campaign_permissions(C(), "m02-funding-squeeze", {"m03-volatility-shock"}).leverage_cap.has_value());
  const CurriculumSummary s = curriculum_summary(C(), {"m06-thin-book"}, {"foundations-bar"});
  CHECK(s.passed == 3);
  CHECK(s.with_content == 14);
}

TEST_CASE("campaign: capital carries on, bankruptcy and the minimum end it, settling is idempotent") {
  Progress p = start_campaign(C(), {}, "c1", 1);
  REQUIRE(p.campaign);
  CHECK(next_campaign_mission(C(), p.campaign) == "m06-thin-book");
  const CampaignLink link{"c1", 0};
  p = settle_campaign_run(C(), p, link, {"r1", "m06-thin-book", LegResult::Finished, "10450.5", {}});
  CHECK(p.campaign->capital == "10450.5");
  CHECK(p.campaign->index == 1);
  CHECK(contains(p.assessed, "m06-thin-book"));
  // Same run again, or a run of another mission: unchanged.
  const Progress again = settle_campaign_run(C(), p, link, {"r1", "m06-thin-book", LegResult::Finished, "1", {}});
  CHECK(again.campaign->capital == "10450.5");

  // Leaving carries the equity it had and stays on the mission.
  p = settle_campaign_run(C(), p, {"c1", 1}, {"r2", "m01-false-breakout", LegResult::Left, "9000", {}});
  CHECK(p.campaign->index == 1);
  CHECK(p.campaign->capital == "9000");
  CHECK(p.campaign->status == CampaignStatus::Active);

  // Finished with less than the minimum counts as a bankruptcy and ends the run (archived).
  p = settle_campaign_run(C(), p, {"c1", 1}, {"r3", "m01-false-breakout", LegResult::Finished, "50", {}});
  CHECK(p.campaign->status == CampaignStatus::Bankrupt);
  CHECK(p.campaign->legs.back().result == LegResult::Bankrupt);
  CHECK_FALSE(contains(p.assessed, "m01-false-breakout"));
  CHECK(p.campaign_history.size() == 1);
  CHECK(next_campaign_mission(C(), p.campaign).empty());

  // A new campaign starts from the campaign capital; stages stay passed.
  p = start_campaign(C(), p, "c2", 2);
  CHECK(p.campaign->capital == "10000");
  CHECK(contains(p.assessed, "m06-thin-book"));
  p = abandon_campaign(C(), p);
  CHECK(p.campaign->status == CampaignStatus::Abandoned);
  CHECK(p.campaign_history.size() == 2);
}

TEST_CASE("campaign: completing every mission, best capital and the report") {
  Progress p = start_campaign(C(), {}, "c", 1);
  const Ids order = campaign_missions(C());
  for (int i = 0; i < static_cast<int>(order.size()); ++i) {
    LegReport r{{{"10", 5000, "range"}, {"-4", -10000, "trend"}, {"2", std::nullopt, ""}}, "8", "1", "0.5", "-0.5"};
    p = settle_campaign_run(C(), p, {"c", i},
                            {"r" + std::to_string(i), order[static_cast<std::size_t>(i)], LegResult::Finished,
                             std::to_string(10000 + i), r});
  }
  CHECK(p.campaign->status == CampaignStatus::Complete);
  CHECK(best_completed_capital(p.campaign_history) == "10016");
  const CampaignReport r = campaign_report(C(), p.campaign->legs);
  CHECK(r.missions == 17);
  CHECK(r.trades == 51);
  CHECK(r.wins == 34);
  CHECK(r.without_r == 17);
  CHECK(r.avg_win_r.value() == doctest::Approx(0.5));
  CHECK(r.avg_loss_r.value() == doctest::Approx(-1.0));
  CHECK(r.expectancy_r.value() == doctest::Approx(-0.25));
  CHECK(r.costs == doctest::Approx(17 * 2.0));  // fees + spread + funding paid
  CHECK(r.cost_share.value() == doctest::Approx(34.0 / (136 + 34)));
  CHECK_FALSE(r.too_few);
  REQUIRE(r.by_regime.size() == 3);
  CHECK(r.by_regime[0].trades == 17);
  CHECK(r.by_regime[2].regime == "unlabeled");
}

TEST_CASE("journal: runs with entries, newest first, filtered by mission") {
  std::vector<RunRecord> runs(3);
  runs[0] = {"a", "m01-false-breakout", 1, 100, std::nullopt, RunStatus::Finished, false, false, "", "", {{5, true}}};
  runs[1] = {"b", "m02-funding-squeeze", 2, 300, CampaignLink{"c", 0}, RunStatus::Left, false, false, "", "", {{9, false}, {3, true}}};
  runs[2] = {"c", "m01-false-breakout", 3, 200, std::nullopt, RunStatus::Active, false, false, "", "", {}};
  const auto all = journal_runs(runs);
  REQUIRE(all.size() == 2);
  CHECK(all[0].run_id == "b");
  CHECK(all[0].mode == RunMode::Campaign);
  CHECK(all[0].entries.front().step == 3);
  CHECK(journal_runs(runs, "m01-false-breakout").size() == 1);
  CHECK(journal_missions(runs) == std::vector<std::string>{"m01-false-breakout", "m02-funding-squeeze"});
}

TEST_CASE("shortcuts: names, labels, conflicts and reserved keys as on the web") {
  CHECK(key_name(" ", false) == "Space");
  CHECK(key_name(" ", true) == "Shift+Space");
  CHECK(key_name("B", true) == "b");
  CHECK(key_name("Shift", true).empty());
  CHECK(key_label("Shift+Space") == "Shift + Space");
  CHECK(key_label("b") == "B");
  CHECK(key_label("?") == "?");
  const Keymap map = default_keymap(C());
  const BindResult conflict = bind_key(C(), map, "buy", "s");
  CHECK(conflict.error == BindResult::Error::Conflict);
  CHECK(conflict.other == "sell");
  CHECK(bind_key(C(), map, "buy", "Shift+Enter").error == BindResult::Error::Reserved);
  const BindResult ok = bind_key(C(), map, "buy", "k");
  REQUIRE(ok.ok());
  CHECK(ok.map.at("buy") == "k");
  CHECK(action_for(C(), ok.map, "k") == "buy");
  CHECK(sanitize_keymap(C(), {{"buy", "s"}}) == map);  // a key bound twice: defaults
  CHECK(sanitize_keymap(C(), {{"buy", "k"}, {"bogus", "z"}}).at("buy") == "k");
}

TEST_CASE("Academy runner: a wrong first try explains, lets the player choose again, and does not score") {
  LessonRunner r(C().lesson("foundations-bar"));
  CHECK(r.solved());  // concept step
  CHECK(r.next());
  CHECK(r.next());
  CHECK(r.next());
  REQUIRE(r.step()->id == "wick");
  CHECK_FALSE(r.next());  // not solved yet
  CHECK(r.pick(0));
  CHECK_FALSE(r.solved());
  CHECK(r.pick(2));
  CHECK(r.solved());
  CHECK_FALSE(r.pick(1));  // solved: further picks ignored
  while (!r.finished()) {
    const LessonStep* s = r.step();
    if (s->question()) r.pick(s->answer);
    REQUIRE(r.next());
  }
  CHECK(r.score().total == 5);
  CHECK(r.score().right == 4);
}

TEST_CASE("report texts: objectives, findings, leverage and tiers in both languages") {
  CHECK(bp_pct(100, Locale::PtBR) == "1%");
  CHECK(bp_pct(250, Locale::PtBR) == "2,5%");
  CHECK(bp_pct(125, Locale::En) == "1.25%");
  CHECK(objective_text(pt(), J(R"({"code":"risk_per_idea","valueBp":200})")) ==
        "Use stop: risco planejado de até 2% do patrimônio por ideia.");
  CHECK(objective_text(en(), J(R"({"code":"future_code","valueBp":0})")) == "future_code");

  const auto findings = J(R"([
    {"code":"liquidated","tone":"bad","step":80,"count":0,"price":"95.5","requested":"0","amount":"120.25"},
    {"code":"stop_beyond_liq","tone":"bad","step":70,"count":3,"price":"94","requested":"96","amount":"0"},
    {"code":"no_stop","tone":"warn","step":0,"count":1,"price":"0","requested":"0","amount":"0"},
    {"code":"liq_buffer","tone":"warn","step":0,"count":150,"price":"0","requested":"0","amount":"0"}])");
  const auto texts = describe_findings(pt(), findings, 300);
  CHECK(texts[0] == "Liquidação na barra 80 a 95,50: a margem isolada inteira (120,25) foi perdida.");
  CHECK(texts[1].find("Com até 3x de alavancagem") != std::string::npos);
  CHECK(texts[2].starts_with("1 entrada sem stop"));
  CHECK(texts[3].find("a 1,5% do preço") != std::string::npos);
  CHECK(texts[3].find("(3%)") != std::string::npos);
  CHECK(describe_lesson(en(), "risk") == "Risk — sizing from the stop and a risk budget");

  CHECK(fmt_leverage_bp(35000, Locale::PtBR) == "3,5x");
  CHECK(fmt_liq_distance(-1, Locale::PtBR) == "\xE2\x80\x94");
  const auto contract = J(R"({"leverageCap":5,"marginTiers":[{"notionalFloor":"0","maintenanceMargin":"0.005","maxLeverage":50},
    {"notionalFloor":"50000","maintenanceMargin":"0.01","maxLeverage":20}]})");
  CHECK(brief_tiers(pt(), contract) == std::vector<std::string>{"≥ 50.000: 1,0% (até 5x)"});
  const auto trade = J(R"({"series":0,"side":"long","stopBeyondLiq":true,"stop":"90","liqPrice":"92","safeLeverage":4,"leverage":10})");
  CHECK(stop_advice_text(en(), trade) == "Stop 90.00 beyond liquidation 92.00 at 10x: up to 4x would have kept it in front");
  CHECK(trade_label(pt(), trade) == "▲ LONG");
}

TEST_CASE("release rows: before, after (within 10 bars), both, flat") {
  const auto events = J(R"([{"step":10,"surprise":2,"credibility":50},{"step":30,"surprise":-1,"credibility":100},
    {"step":50,"surprise":0,"credibility":80},{"step":99,"surprise":1,"credibility":10}])");
  const auto trades = J(R"([{"entryStep":5,"exitStep":12},{"entryStep":15,"exitStep":null},{"entryStep":28,"exitStep":29}])");
  const auto rows = release_rows(events, trades, 60, C().after_release_bars);
  REQUIRE(rows.size() == 3);  // the release after the last step is not revealed
  CHECK(rows[0].you == ReleasePosition::Both);
  CHECK(rows[0].weighted == doctest::Approx(1.0));
  CHECK(rows[1].you == ReleasePosition::Before);  // the open trade from bar 15
  CHECK(rows[2].you == ReleasePosition::Before);
}

TEST_CASE("CRT: every effect off without the master toggle or under reduced motion; text scale steps") {
  const CrtSettings on;
  CHECK(effective_crt(on, false) == CrtState{true, true, true, true});
  CHECK(effective_crt(on, true) == CrtState{});
  CrtSettings no_bloom = on;
  no_bloom.bloom = false;
  CHECK(effective_crt(no_bloom, false) == CrtState{true, true, false, true});
  no_bloom.enabled = false;
  CHECK(effective_crt(no_bloom, false) == CrtState{});
  CHECK(clamp_text_scale(1.3f) == 1.25f);
  CHECK(clamp_text_scale(9.0f) == 2.0f);
  CHECK(clamp_text_scale(0.1f) == 1.0f);
}
