// W3-X1 of add-practice-and-advanced-strategies on the Vulkan renderer: the practice formats played with
// the same inputs as the web's cross-check (apps/web/e2e/cross-check-w3.spec.ts), PT-BR 1280x800, the
// device clock at noon of 2026-11-09: the first drill session's answers, the daily challenge of ten
// dates, five guess rounds and the coach review of m23 at seed 3 (wave 2's X1 inputs) with a mistake
// retried from its first warning. The expected texts are the web's, word for word; the report
// ($TRADELAB_CAPTURE_DIR/playtest/w3-x1.txt) lists what each screen showed.
#include <doctest/doctest.h>

#include <raven/raven_widget.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "support/playtest_run.hpp"
#include "support/practice_flows.hpp"
#include "support/text_dump.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/game/wave1_widgets.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace gn = tradelab::desktop::names::game;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::RavenSession;
using tradelab::desktop::testing::Run;

namespace {

struct Player {
  RavenSession raven;
  App app;
  std::unique_ptr<Driver> d;
  std::unique_ptr<Run> run;
  std::unique_ptr<testing::Terminal> t;
  explicit Player(const std::string& name) : app(builtin_catalog(), [] { return 3u; }) {
    REQUIRE(app.build(1280, 800));
    app.vm().set_crt(CrtSettings{false, false, false, false});
    set_day("2026-11-09");
    d = std::make_unique<Driver>(app);
    d->frames(4);
    app.vm().set_locale(Locale::PtBR);
    d->frames(2);
    run = std::make_unique<Run>(app, *d, name, 1280, 800);
    t = std::make_unique<testing::Terminal>(app, *d);
  }
  void set_day(const std::string& date) {
    const std::int64_t noon = practice::day_start(date) + 12 * 3'600'000LL;
    app.vm().set_clock([noon] { return noon; });
  }
  void menu() {
    app.vm().back_to_menu();
    d->frames(3);
  }
  std::string text(std::string_view widget) {
    d->frames(2);
    return d->text(widget);
  }
  // Every text under `widget` (labels drawn by child widgets included), one per line.
  std::string texts(std::string_view widget) {
    d->frames(2);
    return testing::dump_texts(d->widget(widget));
  }
  std::ostream& report() { return run->report(); }
};

bool has(const std::string& text, const std::string& part) {
  INFO(text);
  INFO(part);
  return text.find(part) != std::string::npos;
}

// The first session of 2026-11-09 (drills.json buildSession, session 0), each item answered wrong: the
// feedback names the answer.
void drills(Player& p) {
  const char* answers[] = {"2,40", "8,40", "10,40", "Atrapalha (theta negativo)", "95,50", "0,40"};
  testing::press(*p.d, names::kMenuDrills);
  testing::press(*p.d, names::kDrillsStart);
  const DrillsState& s = p.app.vm().practice().drills;
  REQUIRE(s.items.size() == 6);
  p.report() << "---- drills\n";
  for (std::size_t k = 0; k < s.items.size(); ++k) {
    if (s.items[k].choice) {
      testing::press(*p.d, names::kDrillsChoice[0]);
    } else {
      p.d->type_into(names::kDrillsInput, "-1");
      p.d->key(R_KEY_ENTER);
      p.d->frames(2);
    }
    const std::string feedback = p.texts(names::kDrillsFeedback);
    p.report() << p.text(names::kDrillsPrompt) << "\n  " << feedback << "\n";
    CHECK(has(feedback, std::string("a resposta é ") + answers[k]));
    testing::press(*p.d, names::kDrillsNext);
  }
  CHECK(has(p.texts(names::kDrillsFirstTry), "0 de 6"));
  p.menu();
}

// Ten dates: the pool's mission by the day index since 2026-01-01, the seed FNV-1a 32 of
// "tradelab-daily-v1:<date>" (the web's e2e asserts the same table).
void daily(Player& p) {
  struct Day {
    const char* date;
    const char* mission;
    const char* seed;
  };
  const Day days[] = {
      {"2025-12-31", "Missão 25", "1637798984"}, {"2026-01-01", "Missão 01", "138805866"},  {"2026-02-28", "Missão 11", "3464289812"},
      {"2026-03-01", "Missão 13", "416280928"},  {"2026-11-09", "Missão 01", "3582066521"}, {"2026-11-10", "Missão 02", "3800322663"},
      {"2026-12-31", "Missão 05", "1083961673"}, {"2027-01-01", "Missão 06", "1635316717"}, {"2028-02-29", "Missão 23", "2268926261"},
      {"2028-03-01", "Missão 24", "2243913778"},
  };
  p.report() << "---- daily\n";
  for (const Day& day : days) {
    p.set_day(day.date);
    testing::press(*p.d, names::kMenuDaily);
    REQUIRE(p.app.vm().screen() == Screen::Daily);
    CHECK(p.text(names::kDailyToday) == std::string("Hoje, ") + day.date);
    CHECK(has(p.text(names::kDailyMission), day.mission));
    CHECK(p.text(names::kDailySeed) == std::string("Mercado do dia: semente ") + day.seed);
    p.report() << day.date << " " << p.text(names::kDailyMission) << " " << p.text(names::kDailySeed) << "\n";
    p.menu();
  }
  p.set_day("2026-11-09");
}

// A price in 1e-8 units rounded to cents and moved by `cents`, typed as PT-BR does ("89,08").
std::string level(std::int64_t close, std::int64_t cents) {
  const std::int64_t v = (close + 500'000) / 1'000'000 + cents;
  const std::string frac = std::to_string(100 + v % 100).substr(1);
  return std::to_string(v / 100) + "," + frac;
}

struct Round {
  int decision;  // 0 long, 1 short, 2 no trade
  int p;
  std::int64_t inv;     // cents to the invalidation, on the losing side
  std::int64_t target;  // cents to the target, on the winning side
  const char* market;   // the reveal's lines, as the web shows them
  const char* close;
  const char* levels;
  const char* outcome;
  const char* score;
};

void guess(Player& p) {
  const Round rounds[] = {
      {0, 70, 150, 300, "K2T · decisão na barra 113", "Fechamento agora: 90,58 · ATR(14): 0,95", "Invalidação 89,08 · alvo 93,58",
       "O fechamento não ficou acima: 90,58 → 90,11.", "Nota 51 de 100"},
      {1, 30, 150, 300, "K8O · decisão na barra 107", "Fechamento agora: 99,59 · ATR(14): 0,96", "Invalidação 101,09 · alvo 96,59",
       "O fechamento não ficou acima: 99,59 → 98,26.", "Nota 91 de 100"},
      {2, 50, 0, 0, "K8X · decisão na barra 86", "Fechamento agora: 98,70 · ATR(14): 0,63", "",
       "O fechamento ficou acima: 98,70 → 102,41.", "Nota 75 de 100"},
      {0, 95, 20, 20, "K6R · decisão na barra 116", "Fechamento agora: 102,05 · ATR(14): 1,42", "Invalidação 101,85 · alvo 102,25",
       "O fechamento não ficou acima: 102,05 → 97,03.", "Nota 20 de 100"},
      {1, 80, 400, 100, "K9F · decisão na barra 111", "Fechamento agora: 112,44 · ATR(14): 0,80", "Invalidação 116,44 · alvo 111,44",
       "O fechamento não ficou acima: 112,44 → 109,26.", "Nota 10 de 100"},
  };
  AppViewModel& vm = p.app.vm();
  const GuessState& g = vm.practice().guess;
  testing::press(*p.d, names::kMenuGuess);
  p.report() << "---- guess\n";
  for (const Round& r : rounds) {
    testing::press(*p.d, g.phase == GuessState::Phase::Reveal ? names::kGuessNext : names::kGuessStart);
    REQUIRE(p.d->until([&] { return g.phase == GuessState::Phase::Commit; }));
    CHECK(has(p.text(names::kGuessMarket), r.market));
    CHECK(p.text(names::kGuessClose) == r.close);
    p.d->choose(names::kGuessDecision, r.decision);
    vm.guess_set_p(r.p / 5 - 1);
    if (r.decision != 2) {
      const auto close = practice::fixed(g.at_decision_raw["bars"][static_cast<std::size_t>(g.pick->step)][3].as_string());
      REQUIRE(close.has_value());
      const std::int64_t dir = r.decision == 0 ? 1 : -1;
      p.d->type_into(names::kGuessInv, level(*close, -dir * r.inv));
      p.d->type_into(names::kGuessTarget, level(*close, dir * r.target));
    }
    testing::press(*p.d, names::kGuessCommit);
    REQUIRE(p.d->until([&] { return g.phase == GuessState::Phase::Reveal; }));
    const std::string reveal = p.texts(names::kGuessReveal);
    p.report() << p.text(names::kGuessMarket) << " | " << r.close << "\n" << reveal << "\n";
    if (*r.levels != '\0') CHECK(has(reveal, r.levels));
    CHECK(has(reveal, r.outcome));
    CHECK(has(reveal, r.score));
  }
  p.menu();
}

void buy(Player& p, std::string_view qty) {
  p.t->skip_question();
  if (p.app.vm().game().tab() != game::Tab::Perp) REQUIRE(p.d->click(gn::kTabPerp));
  REQUIRE(p.d->click(names::kGameBuy));
  p.d->choose(gn::kSizeBy, 1);
  p.t->type(names::kGameQty, qty);
  REQUIRE(p.d->click(gn::kSubmit));
  if (p.app.vm().game().modal() == game::Modal::Plan) REQUIRE(p.d->click(gn::kPlanSkip));
  REQUIRE(p.d->idle());
}

void close_position(Player& p) {
  p.t->skip_question();
  REQUIRE(p.d->click(gn::kClose));
  if (p.app.vm().game().modal() == game::Modal::Confirm) REQUIRE(p.d->click(names::wave1::kConfirmYes));
  if (p.app.vm().game().modal() == game::Modal::Plan) REQUIRE(p.d->click(gn::kPlanSkip));
  REQUIRE(p.d->idle());
}

// m23 at seed 3 with wave 2's X1 inputs: 10 bought, closed by hand the next bar, 20 bought the bar after.
void m23(Player& p) {
  p.app.vm().select(testing::mission_index("m23-second-chance"));
  p.d->frames(2);
  REQUIRE(p.d->click(names::kMenuStart));
  REQUIRE(p.d->until([&] { return p.app.vm().screen() == Screen::Tutorial || p.app.vm().screen() == Screen::Briefing; }));
  while (p.app.vm().screen() == Screen::Tutorial) REQUIRE(p.d->click(names::kTutNext));  // shown on the first start
  REQUIRE(p.d->until([&] { return p.app.vm().screen() == Screen::Briefing; }));
  REQUIRE(p.d->click(names::kBriefGo));
  REQUIRE(p.app.vm().screen() == Screen::Game);
  buy(p, "10");
  p.run->advance(1);
  close_position(p);
  p.run->advance(1);
  buy(p, "20");
  p.run->play_to_debrief();
  if (const auto* log = p.app.vm().game().action_log()) {
    for (const auto& a : *log) p.report() << "  " << a << "\n";
  }
  CHECK(p.app.vm().snapshot()->digest == "6ae787a60d868510740c2e441707fa847a68266d96b8df6e5fff3d02059b7590");
}

// The coach's moments as the web shows them, then the first warning retried with no trade: avoided.
void coach_and_retry(Player& p) {
  const char* moments[][3] = {
      {"Momento 1 de 2 · barra 62", "Atenção",
       "Barra 62: logo depois de o trade 1 fechar com perda de 8,31 cr na barra 62, veio uma entrada nova, sem plano, com 2,00× o "
       "tamanho dele. Depois de uma perda, espere o setup se formar de novo."},
      {"Momento 2 de 2 · barra 63", "Atenção",
       "Barra 63: a posição (20,0 a 100,31) ficou aberta no fechamento (99,96) sem nenhum stop. Sem stop, a perda não tem um limite "
       "que você escolheu."},
  };
  AppViewModel& vm = p.app.vm();
  testing::press(*p.d, names::kDebriefOpenReview);
  REQUIRE(vm.screen() == Screen::CoachReview);
  REQUIRE(p.d->until([&] { return vm.practice().coach.review.has_value(); }));
  REQUIRE(vm.practice().coach.review->moments.size() == 2);
  p.report() << "---- coach\n";
  for (int i = 0; i < 2; ++i) {
    if (i > 0) vm.coach_review_step(1);
    const auto& m = moments[i];
    const std::string shown = p.texts(names::kReviewMomentBox);
    p.report() << shown;
    for (const char* part : m) CHECK(has(shown, part));
    const int step = vm.practice().coach.review->moments[static_cast<std::size_t>(i)].step;
    CHECK(practice::state_at(*vm.snapshot()->game, step).bars.size() == static_cast<std::size_t>(step) + 1);  // no later bar
  }
  vm.coach_review_step(-1);
  testing::press(*p.d, names::kReviewRetry);
  REQUIRE(p.d->until([&] { return vm.screen() == Screen::Game; }));
  CHECK(has(p.text(names::kGameMistakeBanner), "Nova tentativa a partir da barra 62"));
  p.run->play_to_debrief();
  REQUIRE(p.d->until([&] { return vm.practice().mistake.has_value(); }));
  const std::string report = p.texts(names::kDebriefMistakePanel);
  p.report() << "---- retry\n" << report;
  p.report() << "---- digest " << vm.snapshot()->digest << "\n";
  CHECK(vm.practice().mistake->outcome == "avoided");
  CHECK(has(report, "Evitado: trade de vingança não apareceu entre as barras 62 e 67."));
  CHECK(vm.practice().mistake->original->score == 35);
  CHECK(vm.practice().mistake->retry.score == 47);
  CHECK(has(report, "| Original | 35 | -77,71 cr |"));
  CHECK(has(report, "| Nova tentativa | 47 | -8,31 cr |"));
  CHECK(vm.snapshot()->digest == "1f98858d99defe5234683c79a16c2208dcdf4bd2ca2559b878e47f8b74aa0952");
}

// The first warning retried buying the 20 at once: the mistake repeated, the original's result. The digest
// differs from the original's: the retry starts without the advisor's allowance, which the digest hashes.
// A new player, as the web's test: on a second m23 the calendar perk (one finished mission) is on, and
// the run and its retry end on other digests (4b74f151... and 9c76ad5c..., the same on both clients).
void retry_again(Player& p) {
  AppViewModel& vm = p.app.vm();
  testing::press(*p.d, names::kDebriefOpenReview);
  REQUIRE(p.d->until([&] { return vm.practice().coach.review.has_value(); }));
  testing::press(*p.d, names::kReviewRetry);
  REQUIRE(p.d->until([&] { return vm.screen() == Screen::Game; }));
  buy(p, "20");
  p.run->play_to_debrief();
  REQUIRE(p.d->until([&] { return vm.practice().mistake.has_value(); }));
  if (const auto* log = p.app.vm().game().action_log()) {
    for (const auto& a : *log) p.report() << "  retry: " << a << "\n";
  }
  const std::string report = p.texts(names::kDebriefMistakePanel);
  p.report() << "---- retry again\n" << report << "---- digest " << vm.snapshot()->digest << "\n";
  CHECK(vm.practice().mistake->outcome == "repeated");
  CHECK(has(report, "Repetido: trade de vingança apareceu de novo entre as barras 62 e 67."));
  CHECK(has(report, "| Nova tentativa | 35 | -77,71 cr |"));
  CHECK(vm.snapshot()->digest == "60af7fa9b04628db2e9f1361389a3dd02b2fcb9a7291a54aa4831d49ebc63c7c");
}

}  // namespace

TEST_CASE("playtest wave 3 X1: drills, daily, guess rounds and the m23 coach review, same inputs as the web") {
  Player p("w3-x1");
  drills(p);
  daily(p);
  guess(p);
  m23(p);
  coach_and_retry(p);
  p.run->finish();
}

TEST_CASE("playtest wave 3 X1: m23 retried from its first warning repeating the mistake, same inputs as the web") {
  Player p("w3-x1-again");
  m23(p);
  retry_again(p);
  p.run->finish();
}
