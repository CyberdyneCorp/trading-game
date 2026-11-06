// Shell screen viewmodels (screens::*) and AppViewModel's shell commands, without a window: a fake
// engine records commands, and snapshots come from real engine runs (EngineRun, same thread).
#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "model/engine_run.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;

namespace {

struct FakeEngine final : EngineCommands {
  std::vector<std::string> log;
  RunOptions last_options;
  void start(const std::string& mission, std::uint32_t seed) override {
    log.push_back("start " + mission + " " + std::to_string(seed));
  }
  void start_with(const std::string& mission, std::uint32_t seed, const RunOptions& o) override {
    last_options = o;
    start(mission, seed);
  }
  void advance(int steps) override { log.push_back("advance " + std::to_string(steps)); }
  void market(Side side, const std::string& qty) override { log.push_back(side == Side::Buy ? "buy " + qty : "sell " + qty); }
};

std::shared_ptr<const Snapshot> with_result(Snapshot s, CommandKind kind) {
  s.results.push_back(CommandResult{kind, true, "none"});
  return std::make_shared<Snapshot>(std::move(s));
}

// A real m01 run on seed 1: buy 1,5 at the first decision, sell after ten bars, play to the end.
struct RealRun {
  std::unique_ptr<EngineRun> run;
  explicit RealRun(const std::string& mission = "m01-false-breakout", const RunOptions& o = {})
      : run(EngineRun::create(mission, 1, o)) {}
  std::shared_ptr<const Snapshot> started() { return with_result(run->snapshot(), CommandKind::Start); }
  std::shared_ptr<const Snapshot> trade_and_finish() {
    run->place_market(Side::Buy, "1.5");
    run->advance(10);
    run->place_market(Side::Sell, "1.5");
    run->advance(500);
    return with_result(run->snapshot(), CommandKind::Advance);
  }
  std::shared_ptr<const Snapshot> midway() {
    run->advance(5);
    return with_result(run->snapshot(), CommandKind::Advance);
  }
};

struct Fixture {
  FakeEngine engine;
  AppViewModel vm{builtin_catalog(), engine_missions(), engine, [] { return 1u; }};
  Fixture() {
    std::int64_t t = 1'700'000'000'000;
    vm.set_clock([t]() mutable { return t += 1000; });
  }
  // Menu -> briefing -> terminal of `run`.
  void enter(RealRun& run) {
    REQUIRE(vm.screen() == Screen::Menu);
    vm.on_snapshot(run.started());
    REQUIRE(vm.screen() == Screen::Briefing);
    vm.open_terminal();
  }
  void play_selected(RealRun& run) {
    vm.start_selected();
    enter(run);
    vm.advance(1);
    vm.on_snapshot(run.trade_and_finish());
    REQUIRE(vm.screen() == Screen::Debrief);
  }
};

bool has_line(const ScreenModel& m, std::string_view widget, std::string_view needle) {
  const WidgetLines* l = m.lines_of(widget);
  if (l == nullptr) return false;
  for (const auto& line : l->lines) {
    if (line.text.find(needle) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("shell: menu lists the missions, the campaign and no resume line at start") {
  Fixture f;
  const ScreenModel m = f.vm.screen_model();
  REQUIRE(m.items.size() == 1);
  CHECK(m.items[0].items.size() == 12);
  CHECK(m.items[0].items[0] == "Missão 01 · Range");
  CHECK_FALSE(m.is_visible(names::kMenuResumeRow));
  CHECK(m.text_of(names::kCampaignStart) == "▶ Começar campanha");
  CHECK(m.text_of(names::kMenuCurriculum) == "▦ Currículo e progresso");
  const WidgetLines* steps = m.lines_of(names::kCampaignSteps);
  REQUIRE(steps != nullptr);
  CHECK(steps->lines.size() == 12);
  CHECK(steps->lines[0].text == "· Missão 06 · Livro fino");
  // Twelve identical buttons on the web: the screen-reader name says which mission.
  bool named = false;
  for (const auto& n : m.names) named = named || (n.widget == names::kMenuStart && n.text == "Iniciar missão: Missão 01 · Range");
  CHECK(named);
}

TEST_CASE("shell: briefing shows the engine's rules, permissions, tiers and mark-price note") {
  Fixture f;
  RealRun run;
  f.vm.start_selected();
  f.vm.on_snapshot(run.started());
  const ScreenModel m = f.vm.screen_model();
  CHECK(m.text_of(names::kBriefTitle) == "Briefing · K2A");
  CHECK(m.text_of(names::kBriefPermissions) == "Permitido: long · short");
  CHECK(m.text_of(names::kBriefNotAllowed) == "Não permitido: comprar opções · vender opções");
  CHECK(m.text_of(names::kBriefLeverage) == "Alavancagem permitida: 1x – 5x");
  CHECK(m.text_of(names::kBriefLimits).find("3%") != std::string::npos);
  CHECK(m.text_of(names::kBriefTiers).find("≥ 50.000: 1,0% (até 5x)") != std::string::npos);
  CHECK(m.text_of(names::kBriefMark).starts_with("Preço de marcação: Margem e liquidação usam o último preço"));
  CHECK_FALSE(m.is_visible(names::kBriefShocks));
  CHECK_FALSE(m.is_visible(names::kBriefLocked));
  const WidgetLines* obj = m.lines_of(names::kBriefObjectives);
  REQUIRE(obj != nullptr);
  CHECK(obj->lines.size() == 3);
  CHECK(obj->lines[1].text == "• Use stop: risco planejado de até 2% do patrimônio por ideia.");
}

TEST_CASE("shell: the full debrief from a real run, and the mission marked completed") {
  Fixture f;
  RealRun run;
  f.play_selected(run);
  const ScreenModel m = f.vm.screen_model();
  CHECK(m.text_of(names::kDebriefTitle) == "Missão concluída");
  CHECK(has_line(m, names::kDebriefOutcomeLines, "Spread e slippage"));
  CHECK(has_line(m, names::kDebriefOutcomeLines, "Comprar e segurar"));
  CHECK(has_line(m, names::kDebriefDims, "Risco (35%): 80"));
  CHECK(has_line(m, names::kDebriefFindings, "1 entrada sem stop"));
  CHECK(m.text_of(names::kDebriefNextLesson).starts_with("Próxima lição recomendada: Risco"));
  CHECK(has_line(m, names::kDebriefReveal, "semente 1"));
  CHECK(has_line(m, names::kDebriefDecisions, "Agiu em 2, manteve a posição em"));
  CHECK_FALSE(m.is_visible(names::kDebriefGreeksPanel));
  CHECK_FALSE(m.is_visible(names::kDebriefCampaignPanel));
  const WidgetTable* trades = m.table_of(names::kDebriefTrades);
  REQUIRE(trades != nullptr);
  REQUIRE(trades->rows.size() == 1);
  CHECK(trades->columns.back() == "Regime na entrada");
  CHECK(trades->rows[0][1] == "▲ LONG × 1,5");
  CHECK(trades->rows[0][5] == "—");  // no stop: no R
  CHECK(trades->rows[0][9] == "Lateral");
  const WidgetTable* regimes = m.table_of(names::kDebriefRegimes);
  REQUIRE(regimes != nullptr);
  CHECK(regimes->rows.size() == 6);
  const WidgetTable* releases = m.table_of(names::kDebriefReleases);
  REQUIRE(releases != nullptr);
  REQUIRE(releases->rows.size() == 2);
  CHECK(releases->rows[0][0] == "81");
  // The web's timeline (playtest): engine events as the feed words them, not trade summaries, and the
  // released headlines rather than the kind of news.
  CHECK(has_line(m, names::kDebriefTimeline, "[61] Execução #1: BUY 1,5 @ 101,46"));
  CHECK_FALSE(has_line(m, names::kDebriefTimeline, "▲ LONG"));
  CHECK_FALSE(has_line(m, names::kDebriefTimeline, "Ordem #1 aceita"));  // accepted orders stay in the feed
  CHECK_FALSE(has_line(m, names::kDebriefTimeline, "📰 Adoção institucional"));
  CHECK(has_line(m, names::kDebriefTimeline, "Fim da missão"));
  // The point balance after the run (the web's progress.advisorPoints), never a dash.
  CHECK(has_line(m, names::kDebriefAdvisor, "Saldo: " + std::to_string(f.vm.game().prefs().advisor_points) + "."));
  CHECK_FALSE(has_line(m, names::kDebriefAdvisor, "Saldo: —"));
  // The reveal panels carry their titles, as RegimeReveal/ReleaseReveal.svelte.
  CHECK(m.text_of(names::kDebriefRegimesTitle) == "Regimes revelados e sinais");
  CHECK(m.text_of(names::kDebriefReleasesTitle) == "Divulgações: esperado × surpresa");
  CHECK(m.text_of(names::kDebriefCalibration).find("Menos de 3") != std::string::npos);

  f.vm.back_to_menu();
  const ScreenModel menu = f.vm.screen_model();
  CHECK(menu.items[0].items[0] == "Missão 01 · Range  ✓");
  CHECK(menu.text_of(names::kMenuDetailDone) == "✓ Concluída");
  CHECK(f.vm.progress().runs.size() == 1);
  CHECK(f.vm.progress().runs[0].status == RunStatus::Finished);
}

TEST_CASE("shell: a run left from the terminal shows the resume line and resumes") {
  Fixture f;
  RealRun run;
  f.vm.start_selected();
  f.enter(run);
  f.vm.advance(1);
  f.vm.on_snapshot(run.midway());
  f.vm.back_to_menu();
  ScreenModel m = f.vm.screen_model();
  CHECK(m.is_visible(names::kMenuResumeRow, false));
  CHECK(m.text_of(names::kMenuResumeText) == "Missão 01 · Range · seed 1");
  f.vm.resume();
  CHECK(f.vm.screen() == Screen::Game);
  // Starting another run leaves this one (stored as left): no resume line any more.
  f.vm.back_to_menu();
  f.vm.select(1);
  f.vm.start_selected();
  CHECK(f.vm.progress().runs[0].status == RunStatus::Left);
  CHECK_FALSE(f.vm.resumable());
}

TEST_CASE("shell: a campaign starts its first mission with the campaign capital and the leverage gate") {
  Fixture f;
  f.vm.campaign_start();
  REQUIRE(f.engine.log.size() == 1);
  CHECK(f.engine.log[0] == "start m06-thin-book 1");
  CHECK(f.engine.last_options.starting_balance == "10000");
  CHECK(f.engine.last_options.leverage_cap == 2);
  RealRun run("m06-thin-book", f.engine.last_options);
  f.enter(run);
  f.vm.back_to_menu();  // from the terminal, back to the menu: the campaign panel offers Continue
  ScreenModel m = f.vm.screen_model();
  CHECK(m.text_of(names::kCampaignProgress).starts_with("Missão 1 de 12: Missão 06 · Livro fino"));
  CHECK(m.is_visible(names::kCampaignContinue, false));
  f.vm.campaign_continue();  // resumes the mission in play
  CHECK(f.vm.screen() == Screen::Game);
  f.vm.advance(1);
  f.vm.on_snapshot(run.trade_and_finish());
  REQUIRE(f.vm.screen() == Screen::Debrief);
  const ScreenModel d = f.vm.screen_model();
  CHECK(d.is_visible(names::kDebriefCampaignPanel, false));
  CHECK(has_line(d, names::kDebriefCampaign, "CAMPANHA 1/12"));
  CHECK(has_line(d, names::kDebriefCampaign, "Estágios aprovados: Estrutura de mercado, Ordens"));
  CHECK(d.is_visible(names::kDebriefCampaignNext, false));
  CHECK_FALSE(d.is_visible(names::kDebriefAgain));
  const std::string capital = f.vm.progress().campaign->capital;
  CHECK(capital == f.vm.snapshot()->debrief->final_equity);
  f.vm.campaign_next();
  CHECK(f.engine.log.back() == "start m01-false-breakout 1");
  CHECK(f.engine.last_options.starting_balance == capital);
}

TEST_CASE("shell: ending a campaign asks first and keeps passed stages") {
  Fixture f;
  f.vm.campaign_start();
  RealRun run("m06-thin-book", f.engine.last_options);
  f.enter(run);
  f.vm.advance(1);
  f.vm.on_snapshot(run.trade_and_finish());
  f.vm.back_to_menu();
  f.vm.campaign_end_ask();
  ScreenModel m = f.vm.screen_model();
  CHECK(m.is_visible(names::kCampaignConfirmRow, false));
  CHECK(m.text_of(names::kCampaignConfirm).find("Encerrar") != std::string::npos);
  f.vm.campaign_end_answer(false);
  CHECK(f.vm.progress().campaign->status == CampaignStatus::Active);
  f.vm.campaign_end_ask();
  f.vm.campaign_end_answer(true);
  CHECK(f.vm.progress().campaign->status == CampaignStatus::Abandoned);
  CHECK(f.vm.progress().assessed == std::vector<std::string>{"m06-thin-book"});
  m = f.vm.screen_model();
  CHECK(m.is_visible(names::kCampaignEnded, false));
  CHECK(m.text_of(names::kCampaignStart) == "▶ Nova campanha");
}

TEST_CASE("shell: curriculum map statuses, Academy lesson to completion, stage passed") {
  Fixture f;
  f.vm.open_curriculum();
  ScreenModel m = f.vm.screen_model();
  const WidgetTable* stages = m.table_of(names::kCurriculumStages);
  REQUIRE(stages != nullptr);
  REQUIRE(stages->rows.size() == 13);
  CHECK(stages->rows[0][1] == "Fundamentos");
  CHECK(stages->rows[0][2] == "DISPONÍVEL");
  CHECK(stages->rows[3][2] == "BLOQUEADO");
  CHECK(stages->rows[3][4].find("short") != std::string::npos);
  CHECK(m.text_of(names::kCurriculumLessonStart) == "Começar lição");

  f.vm.start_selected_lesson();
  REQUIRE(f.vm.screen() == Screen::Lesson);
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kLessonTitle) == "Uma barra, quatro preços");
  CHECK(m.text_of(names::kLessonStep) == "1 de 8");
  while (!f.vm.shell().lesson.finished()) {
    const LessonStep* s = f.vm.shell().lesson.step();
    if (s->question()) {
      f.vm.lesson_pick((s->answer + 1) % static_cast<int>(s->choices.size()));  // wrong first
      m = f.vm.screen_model();
      CHECK(m.text_of(names::kLessonFeedback).starts_with("Tente de novo. "));
      f.vm.lesson_pick(s->answer);
    }
    f.vm.lesson_next();
  }
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kLessonScore) == "0 de 5 respostas certas na primeira tentativa");
  f.vm.lesson_finish();
  CHECK(f.vm.screen() == Screen::Curriculum);
  m = f.vm.screen_model();
  CHECK(m.table_of(names::kCurriculumStages)->rows[0][2] == "APROVADO");
  CHECK(m.text_of(names::kCurriculumLessonStart) == "Refazer lição");
}

// Regression: the terminal kept its plans to itself, so the Journal screen never listed them.
TEST_CASE("shell: a plan recorded in the terminal reaches the run's journal") {
  Fixture f;
  RealRun a;
  f.vm.start_selected();
  f.enter(a);
  auto& game = f.vm.game();
  game.ticket.size_by = game::SizeBy::Qty;
  game.ticket.qty = "1,5";
  game.ticket.stop = "95,5";
  game.submit();
  REQUIRE(game.modal() == game::Modal::Plan);
  game.plan.thesis = "rompimento com volume";
  game.plan_confirm(false);
  const RunRecord* run = nullptr;
  for (const auto& r : f.vm.progress().runs) {
    if (r.run_id == f.vm.shell().run_id) run = &r;
  }
  REQUIRE(run != nullptr);
  REQUIRE(run->journal.size() == 1);
  CHECK(run->journal[0].thesis == "rompimento com volume");
  CHECK(run->journal[0].buy);
}

TEST_CASE("shell: journal lists the plans of the runs, newest first, with a mission filter") {
  Fixture f;
  RealRun a;
  f.vm.start_selected();
  f.enter(a);
  f.vm.add_journal_entry(PlanEntry{60, true, "rompimento falso", 99.5, 104.0, 70, 30.0, false, "", ""});
  f.vm.back_to_menu();
  f.vm.select(1);
  f.vm.start_selected();
  RealRun b("m02-funding-squeeze");
  f.enter(b);
  f.vm.add_journal_entry(PlanEntry{61, false, "", std::nullopt, std::nullopt, 50, std::nullopt, true, "", ""});
  f.vm.back_to_menu();
  f.vm.open_journal();
  ScreenModel m = f.vm.screen_model();
  CHECK(m.is_visible(names::kJournalFilterRow, false));
  CHECK_FALSE(m.is_visible(names::kJournalEmpty));
  const WidgetLines* runs = m.lines_of(names::kJournalRuns);
  REQUIRE(runs != nullptr);
  REQUIRE(runs->lines.size() == 5);
  CHECK(runs->lines[0].text.starts_with("Missão 02 · Momentum  [LIVRE] · "));
  CHECK(runs->lines[0].text.ends_with(" · seed 1 · " + f.vm.tr("journal.status.active")));
  CHECK(runs->lines[1].text == "[barra 61] ▼ SHORT — entrada sem plano");
  CHECK(runs->lines[3].text == "[barra 60] ▲ LONG  rompimento falso");
  CHECK(runs->lines[4].text == "    confiança 70% · invalidação 99,50 · alvo 104,00 · risco 30,00 cr");
  f.vm.set_journal_filter(1);
  m = f.vm.screen_model();
  CHECK(m.lines_of(names::kJournalRuns)->lines.size() == 3);
  CHECK(m.lines_of(names::kJournalRuns)->lines[0].text.starts_with("Missão 01 · Range"));
}

TEST_CASE("shell: settings toggle CRT effects, palette, text scale, sound, language and remap shortcuts") {
  Fixture f;
  MemorySettingsRepository repo;
  f.vm.attach_repositories(nullptr, &repo);
  f.vm.open_settings();
  CrtSettings crt = f.vm.settings().crt;
  crt.bloom = false;
  f.vm.set_crt(crt);
  CHECK(f.vm.crt_state() == CrtState{true, true, false, true});
  f.vm.set_reduce_motion(true);
  ScreenModel m = f.vm.screen_model();
  CHECK(m.is_visible(names::kSettingsCrtNote, false));
  CHECK(f.vm.crt_state() == CrtState{});
  f.vm.set_text_scale(1.5f);
  f.vm.set_colorblind(true);
  REQUIRE(repo.load());
  CHECK(repo.load()->text_scale == 1.5f);
  CHECK(repo.load()->colorblind);
  CHECK_FALSE(repo.load()->crt.bloom);

  // Remap "buy" (row 2) to S: refused, S is Sell's; then K: saved.
  f.vm.select_key_row(2);
  f.vm.begin_key_change();
  CHECK(f.vm.screen_model().text_of(names::kSettingsKeyChange) == "Pressione a tecla… (Esc cancela)");
  CHECK(f.vm.capture_key("s"));
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kSettingsKeyMessage).starts_with("A tecla S já é usada por \"Boleta: vender\""));
  f.vm.begin_key_change();
  f.vm.capture_key("k");
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kSettingsKeyMessage) == "\"Boleta: comprar\" agora usa K.");
  CHECK(m.table_of(names::kSettingsKeys)->rows[2][1] == "K");
  CHECK(repo.load()->keymap.at("buy") == "k");
  // Regression: the terminal used its own map, so a key remapped here did nothing in play.
  CHECK(f.vm.game().keymap().key(game::Action::Buy) == "k");
  CHECK_FALSE(f.vm.capture_key("x"));  // not waiting any more
  f.vm.reset_keys();
  CHECK(f.vm.shell().keymap.at("buy") == "b");
  CHECK(f.vm.game().keymap().key(game::Action::Buy) == "b");

  // Sound (the web menu's block): on/off and the two volumes, shared with the terminal's mute key.
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kSettingsSound) == "Som (música e efeitos)");
  CHECK(m.text_of(names::kSettingsMusicLabel) == "Música 35%");
  f.vm.set_sound_on(false);
  f.vm.set_music_volume(0.6);
  f.vm.set_sfx_volume(2);
  CHECK(f.vm.game().muted());
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kSettingsMusicLabel) == "Música 60%");
  CHECK(m.text_of(names::kSettingsSfxLabel) == "Efeitos 100%");

  f.vm.set_locale(Locale::En);
  CHECK(repo.load()->locale == Locale::En);
  CHECK(f.vm.screen_model().text_of(names::kSettingsTitle) == "Settings");
}
