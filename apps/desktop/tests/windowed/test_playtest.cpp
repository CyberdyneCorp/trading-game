// Playtest (task 1.17 "every mission to the debrief"): the twelve missions played to the debrief on the
// Vulkan renderer through injected input, with PT-BR and en, two window sizes, disciplined and reckless
// strategies (stop and target, a liquidation, a trailing stop in percent, options bought and written,
// limit orders, closing early, only waiting). Each run saves PNG captures and a text report (account line
// after each step that changed it, every text of the debrief) in $TRADELAB_CAPTURE_DIR/playtest, the
// material compared with the web for the same mission, seed and actions (README "Playtest").
#include <doctest/doctest.h>

#include <raven/raven_resources.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/png.hpp"
#include "support/text_dump.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
namespace gn = tradelab::desktop::names::game;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::Terminal;

namespace {

std::filesystem::path out_dir() {
  const char* dir = std::getenv("TRADELAB_CAPTURE_DIR");
  std::filesystem::path p = dir != nullptr && *dir != '\0' ? dir : "captures";
  p /= "playtest";
  std::filesystem::create_directories(p);
  return p;
}

struct RavenSession {
  RavenSession() {
    std::string detail;
    REQUIRE_MESSAGE(init_vulkan(detail), "no Vulkan driver: " << detail);
    Rset_animations_enabled(R_FALSE);
    Rset_animation_time_step(1.0 / 60.0);
  }
  ~RavenSession() { Rshutdown(); }
};

// One mission played by a script: its captures and report carry `name`.
class Run {
 public:
  Run(App& app, Driver& d, std::string name) : app_(app), d_(d), t_(app, d), name_(std::move(name)) {}

  Terminal& t() { return t_; }
  Driver& d() { return d_; }
  App& app() { return app_; }
  const GameState& g() { return t_.g(); }

  void shot(const std::string& label) {
    d_.frames(3);
    int w = 0;
    int h = 0;
    std::uint8_t* rgba = Rwindow_capture(app_.view().window(), &w, &h);
    REQUIRE(rgba != nullptr);
    CHECK(testing::write_png((out_dir() / (name_ + "-" + label + ".png")).string(), rgba, w, h));
    Rpixels_free(rgba);
  }

  // The account line, recorded when it changed since the last note.
  void note(const std::string& when) {
    const std::string line = d_.text(names::kGameAccount);
    if (line == last_account_) return;
    last_account_ = line;
    report_ << "[" << when << " bar " << g().step << "] " << line << "\n";
  }

  void advance(int bars) {
    for (int i = 0; i < bars && app_.vm().screen() == Screen::Game; ++i) {
      t_.advance();
      note("advance");
    }
  }

  void play_to_debrief() {
    for (int guard = 0; guard < 120 && app_.vm().screen() == Screen::Game; ++guard) {
      t_.skip_question();
      d_.focus(names::kGameAdvance);
      d_.key(R_KEY_SPACE, R_MOD_SHIFT);
      REQUIRE(d_.idle());
      if (app_.vm().screen() == Screen::Game) note("shift+space");
    }
    REQUIRE(app_.vm().screen() == Screen::Debrief);
  }

  // Perpetual order by quantity from the ticket: side, amount (typed with the locale's comma), optional
  // stop and target chips, then the plan dialog (a thesis when given).
  void order(bool buy, std::string_view qty, int stop_chip = -1, int tp_chip = -1, std::string_view thesis = "") {
    t_.skip_question();
    REQUIRE(d_.click(buy ? names::kGameBuy : names::kGameSell));
    d_.choose(gn::kSizeBy, 1);
    t_.type(names::kGameQty, qty);
    if (stop_chip >= 0) REQUIRE(d_.click("game-stop-chip-" + std::to_string(stop_chip)));
    if (tp_chip >= 0) REQUIRE(d_.click("game-tp-chip-" + std::to_string(tp_chip)));
    t_.submit_with_plan(thesis);
    report_ << "  notice: " << d_.text(names::kGameNotice) << "\n";
  }

  // A quote button of chain row `row` (0-based over every expiry): 0 call bid, 1 call ask, 3 put bid, 4 put ask.
  void option(int row, int column, std::string_view qty = "1") {
    t_.skip_question();
    REQUIRE(d_.click(gn::kTabOptions));
    d_.frames(2);
    const Rid b = chain_button(row, column);
    REQUIRE(b != R_INVALID_ID);
    Rwidget_scroll_into_view(b);
    d_.frames(2);
    float x = 0, y = 0, w = 0, h = 0;
    Rwidget_get_rect(b, &x, &y, &w, &h);
    shot("chain-" + std::to_string(++chains_));
    clipping("chain");
    d_.click_at(x + w / 2, y + h / 2);
    REQUIRE(t_.vm().option_ticket.series.has_value());
    report_ << "  picked: series " << *t_.vm().option_ticket.series << (t_.vm().option_ticket.buy ? " buy" : " sell") << "\n";
    t_.type(gn::kOptQty, qty);
    REQUIRE(d_.click(gn::kOptSubmit));
    if (t_.vm().modal() == game::Modal::Plan) REQUIRE(d_.click(gn::kPlanSkip));
    REQUIRE(d_.idle());
    report_ << "  notice: " << d_.text(names::kGameNotice) << "\n";
    REQUIRE(d_.click(gn::kTabPerp));
  }

  void finish() {
    report_ << "---- debrief\n" << testing::dump_texts(app_.view().screen_root(Screen::Debrief));
    std::ofstream(out_dir() / (name_ + ".txt")) << report_.str();
  }

  // No text of the current screen is cut by its own box or by the panel holding it.
  void clipping(const std::string& when) {
    d_.frames(3);
    const std::string cut = testing::overflow_report(app_.view().screen_root(app_.vm().screen()));
    report_ << "---- clipped (" << when << ")\n" << cut;
    CHECK_MESSAGE(cut.empty(), name_ << " " << when << ":\n" << cut);
  }

  std::ostringstream& report() { return report_; }

 private:
  Rid chain_button(int row, int column) {
    const Rid chain = d_.widget(gn::kChain);
    int seen = 0;
    for (int i = 0; i < Rwidget_get_child_count(chain); ++i) {
      const Rid r = Rwidget_get_child(chain, i);
      if (Rwidget_get_type(r) != R_WIDGET_HBOX || Rwidget_get_child_count(r) != 5) continue;
      if (seen++ == row) return Rwidget_get_child(r, column);
    }
    return R_INVALID_ID;
  }

  App& app_;
  Driver& d_;
  Terminal t_;
  std::string name_;
  std::string last_account_;
  int chains_ = 0;
  std::ostringstream report_;
};

struct Mission {
  int index;
  std::uint32_t seed;
  Locale locale;
  int width;
  int height;
  const char* name;
  std::function<void(Run&)> play;
};

void play(const Mission& m) {
  RavenSession raven;
  const std::uint32_t seed = m.seed;
  App app(builtin_catalog(), [seed] { return seed; });
  REQUIRE(app.build(m.width, m.height));
  Driver d(app);
  d.frames(4);
  app.vm().set_locale(m.locale);
  d.frames(2);
  Run run(app, d, m.name);
  run.t().open(m.index);
  run.report() << m.name << " seed " << m.seed << " " << m.width << "x" << m.height << "\n";
  run.note("open");
  run.shot("1-terminal");
  run.clipping("terminal");
  m.play(run);
  run.shot("2-before-end");
  run.report() << "---- terminal\n" << testing::dump_texts(app.view().screen_root(Screen::Game));
  run.play_to_debrief();
  run.shot("3-debrief");
  run.clipping("debrief");
  run.finish();
  CHECK_FALSE(d.text(names::kDebriefTitle).empty());
}

constexpr Locale kPt = Locale::PtBR;
constexpr Locale kEn = Locale::En;

}  // namespace

TEST_CASE("playtest: m01 disciplined, stop and target with a written plan (pt-BR, 1280x800)") {
  play({0, 4242, kPt, 1280, 800, "m01", [](Run& r) {
          r.order(true, "2,5", 4, 0, "rompimento do range com volume");
          r.advance(3);
          r.clipping("position");
        }});
}

TEST_CASE("playtest: m02 short with stop and target (en, 1024x768)") {
  play({1, 21, kEn, 1024, 768, "m02", [](Run& r) {
          r.advance(5);
          r.order(false, "3", 3, 1);
          r.advance(3);
        }});
}

TEST_CASE("playtest: m03 reckless 20x without a stop until liquidated (pt-BR)") {
  play({2, 5, kPt, 1280, 800, "m03", [](Run& r) {
          r.t().skip_question();
          r.t().type(gn::kLeverage, "20");
          r.order(true, "1800");
          for (int i = 0; i < 60 && !testing::has_event(r.g(), "liquidation") && r.app().vm().screen() == Screen::Game; ++i) r.advance(1);
          CHECK(testing::has_event(r.g(), "liquidation"));
          r.shot("liquidated");
        }});
}

TEST_CASE("playtest: m04 options bought from the chain (en)") {
  play({3, 7, kEn, 1280, 800, "m04", [](Run& r) {
          r.option(2, 1);  // a call at the ask
          r.advance(2);
          r.shot("option");
        }});
}

TEST_CASE("playtest: m05 options written (pt-BR)") {
  play({4, 13, kPt, 1280, 800, "m05", [](Run& r) {
          r.option(2, 3);  // a put at the bid: written
          r.advance(2);
          r.shot("written");
        }});
}

TEST_CASE("playtest: m06 thin book, size beyond the bar, closed early (en, 1024x768)") {
  play({5, 3, kEn, 1024, 768, "m06", [](Run& r) {
          r.order(true, "60");
          r.advance(8);
          r.t().skip_question();
          REQUIRE(r.d().click(gn::kClose));
          REQUIRE(r.d().idle());
          r.advance(2);
        }});
}

TEST_CASE("playtest: m07 trailing stop in percent (pt-BR)") {
  play({6, 9, kPt, 1280, 800, "m07", [](Run& r) {
          r.order(true, "5");
          r.advance(1);
          r.t().skip_question();
          REQUIRE(r.d().click(gn::kTrailByPct));
          REQUIRE(r.d().click("game-trail-chip-1"));  // 2%
          CHECK(r.t().vm().ticket.prot_trail_pct == "2");
          r.shot("trail");
          r.clipping("trail");
          REQUIRE(r.d().click(gn::kProtect));
          REQUIRE(r.d().idle());
          r.report() << "  notice: " << r.d().text(names::kGameNotice) << "\n";
          r.advance(2);
        }});
}

TEST_CASE("playtest: m08 waiting only (en)") {
  play({7, 2, kEn, 1280, 800, "m08", [](Run& r) { r.advance(2); }});
}

TEST_CASE("playtest: m09 straddle bought before the event (pt-BR, 1024x768)") {
  play({8, 17, kPt, 1024, 768, "m09", [](Run& r) {
          r.option(2, 1);
          r.option(2, 4);  // the put of the same strike: a straddle
          CHECK(r.t().vm().option_ticket.series.has_value());
          r.advance(2);
          CHECK(r.g().options.positions.size() == 2);
          r.clipping("straddle");
        }});
}

TEST_CASE("playtest: m10 hedged book, long perp and a put (en)") {
  play({9, 19, kEn, 1280, 800, "m10", [](Run& r) {
          r.order(true, "3", 4);
          r.option(2, 4);
          r.advance(2);
          r.clipping("hedged");
        }});
}

TEST_CASE("playtest: m11 limit order below the market (pt-BR)") {
  play({10, 23, kPt, 1280, 800, "m11", [](Run& r) {
          r.t().skip_question();
          r.d().choose(gn::kType, 1);
          const double below = r.g().account.mark * 0.99;
          const std::string price = fmt::price_input_text(below, r.g().contract.tick, Locale::PtBR);
          r.t().type(gn::kPrice, price);
          r.report() << "  limit price: " << price << "\n";
          r.order(true, "4", 2);
          r.advance(4);
        }});
}

TEST_CASE("playtest: m12 capstone, risk-sized entry closed mid-run (en, 1024x768)") {
  play({11, 29, kEn, 1024, 768, "m12", [](Run& r) {
          r.t().skip_question();
          REQUIRE(r.d().click(names::kGameBuy));
          r.d().choose(gn::kSizeBy, 0);  // risk %
          REQUIRE(r.d().click("game-stop-chip-4"));
          r.t().submit_with_plan();
          r.report() << "  notice: " << r.d().text(names::kGameNotice) << "\n";
          r.advance(20);
          r.t().skip_question();
          if (r.g().position.qty != 0) {
            REQUIRE(r.d().click(gn::kClose));
            REQUIRE(r.d().idle());
          }
          r.advance(1);
        }});
}

namespace {

// Opens the campaign's mission from its briefing and plays it to the debrief.
void play_campaign_leg(Run& run, int leg) {
  REQUIRE(run.d().until([&] { return run.app().vm().screen() == Screen::Briefing; }));
  CHECK(run.d().text(names::kBriefMode).find(std::to_string(leg) + " de 12") != std::string::npos);
  REQUIRE(run.d().click(names::kBriefGo));
  run.order(true, "1,5", 4, 1, "campanha: entrada com stop");
  run.shot("leg" + std::to_string(leg) + "-terminal");
  run.play_to_debrief();
  run.shot("leg" + std::to_string(leg) + "-debrief");
  run.clipping("campaign debrief");
  CHECK(Rwidget_get_visibility(run.d().widget(names::kDebriefCampaignPanel)) == R_VISIBLE);
}

}  // namespace

TEST_CASE("playtest: a campaign through three missions (pt-BR)") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 31u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  Run run(app, d, "campaign");
  REQUIRE(d.click(names::kCampaignStart));
  play_campaign_leg(run, 1);
  for (int leg = 2; leg <= 3; ++leg) {
    REQUIRE(d.click(names::kDebriefCampaignNext));
    play_campaign_leg(run, leg);
  }
  run.finish();
  REQUIRE(d.click(names::kDebriefMenu));
  run.shot("menu");
  run.clipping("menu");
}

TEST_CASE("playtest: advance remapped to N with the CRT off, then an Academy lesson (en, 1024x768)") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 37u; });
  REQUIRE(app.build(1024, 768));
  Driver d(app);
  app.vm().set_reduce_motion(false);
  d.frames(4);
  app.vm().set_locale(Locale::En);
  Run run(app, d, "session");
  REQUIRE(d.click(names::kMenuSettings));
  REQUIRE(d.click(names::kSettingsCrt));
  CHECK_FALSE(app.vm().crt_state().overlay);
  REQUIRE(d.click(names::kSettingsKeys));
  d.key(R_KEY_HOME);  // row 0: advance
  REQUIRE(d.click(names::kSettingsKeyChange));
  d.key(R_KEY_N);
  Rwindow_inject_text(app.view().window(), "n");
  d.frames(2);
  CHECK(app.vm().shell().keymap.at("advance") == "n");
  run.shot("settings");
  run.clipping("settings");
  REQUIRE(d.click(names::kSettingsBack));

  run.t().open(0);
  run.t().skip_question();
  const int step = run.g().step;
  d.focus(gn::kReadout);  // off any field: shortcuts reach the terminal
  d.key(R_KEY_SPACE);
  d.frames(3);
  CHECK(run.g().step == step);
  d.key(R_KEY_N);
  REQUIRE(d.idle());
  CHECK(run.g().step == step + 1);
  run.shot("advanced-with-n");
  REQUIRE(d.click(names::kGameMenu));

  REQUIRE(d.click(names::kMenuCurriculum));
  run.clipping("curriculum");
  REQUIRE(d.click(names::kCurriculumLessonStart));
  for (int guard = 0; guard < 40 && !app.vm().shell().lesson.finished(); ++guard) {
    const LessonStep* s = app.vm().shell().lesson.step();
    if (s->question()) REQUIRE(d.click(names::kLessonChoices[static_cast<std::size_t>(s->answer)]));
    run.clipping("lesson");
    REQUIRE(d.click(names::kLessonNext));
  }
  run.shot("lesson-done");
  run.clipping("lesson done");
  REQUIRE(d.click(names::kLessonFinish));
}
