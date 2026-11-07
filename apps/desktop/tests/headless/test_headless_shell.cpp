// Headless shell screens (Raven null platform + null backend): keyboard-only navigation, screen-reader
// names on every control, the curriculum map, an Academy lesson, the journal and the settings, all
// driven by injected input.
#include <doctest/doctest.h>

#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_widget.h>

#include <utility>

#include "model/storage/storage.hpp"
#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/storage_fixtures.hpp"
#include "view/app.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
using tradelab::desktop::testing::Driver;

namespace {

struct RavenSession {
  RavenSession() { REQUIRE(init_headless()); }
  ~RavenSession() { Rshutdown(); }
};

bool control(RWidgetType t) {
  switch (t) {
    case R_WIDGET_BUTTON:
    case R_WIDGET_CHECKBOX:
    case R_WIDGET_SLIDER:
    case R_WIDGET_TEXT_INPUT:
    case R_WIDGET_COMBO_BOX:
    case R_WIDGET_LIST_VIEW:
    case R_WIDGET_TABLE:
    case R_WIDGET_CUSTOM: return true;
    default: return false;
  }
}

// Controls under `root` that are shown and have no screen-reader name.
void unnamed(Rid root, std::vector<std::string>& out) {
  if (Rwidget_get_visibility(root) == R_COLLAPSED) return;
  if (control(Rwidget_get_type(root))) {
    const char* name = Rwidget_get_accessible_name(root);
    if (name == nullptr || *name == '\0') {
      const char* id = Rwidget_get_name(root);
      out.push_back(id ? id : "<unnamed widget>");
    }
  }
  for (int i = 0; i < Rwidget_get_child_count(root); ++i) unnamed(Rwidget_get_child(root, i), out);
}

void check_names(App& app, Screen s) {
  std::vector<std::string> missing;
  unnamed(app.view().screen_root(s), missing);
  INFO("screen " << static_cast<int>(s));
  for (const auto& m : missing) MESSAGE("no accessible name: " << m);
  CHECK(missing.empty());
}

// Presses Tab until `name` has the keyboard focus (at most 60 times).
bool tab_to(Driver& d, std::string_view name) {
  for (int i = 0; i < 60; ++i) {
    if (Rwidget_is_focused(d.widget(name))) return true;
    d.key(R_KEY_TAB);
  }
  return Rwidget_is_focused(d.widget(name)) != 0;
}

}  // namespace

TEST_CASE("headless shell: every shown control of every shell screen has a screen-reader name") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  CHECK(app.view().missing_names().empty());
  for (const auto& m : app.view().missing_names()) MESSAGE("missing: " << m);
  check_names(app, Screen::Menu);
  REQUIRE(d.click(names::kMenuStart));
  REQUIRE(d.to_briefing());
  check_names(app, Screen::Briefing);
  REQUIRE(d.click(names::kBriefBack));
  REQUIRE(d.click(names::kMenuCurriculum));
  check_names(app, Screen::Curriculum);
  REQUIRE(d.click(names::kCurriculumLessonStart));
  REQUIRE(app.vm().screen() == Screen::Lesson);
  check_names(app, Screen::Lesson);
  app.vm().open_journal();
  d.frames(2);
  check_names(app, Screen::Journal);
  app.vm().open_settings();
  d.frames(2);
  check_names(app, Screen::Settings);
}

TEST_CASE("headless shell: keyboard only, from the menu to the briefing and into the terminal") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  REQUIRE(tab_to(d, names::kMenuList));
  d.key(R_KEY_DOWN);  // Missão 02
  CHECK(app.vm().selected() == 1);
  REQUIRE(tab_to(d, names::kMenuStart));
  d.key(R_KEY_ENTER);
  REQUIRE(d.to_briefing());
  CHECK(app.vm().snapshot()->mission == "m02-funding-squeeze");
  REQUIRE(tab_to(d, names::kBriefGo));
  d.key(R_KEY_ENTER);
  CHECK(app.vm().screen() == Screen::Game);
}

TEST_CASE("headless shell: curriculum map, an Academy lesson played by clicks, the stage passed") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  REQUIRE(d.click(names::kMenuCurriculum));
  REQUIRE(app.vm().screen() == Screen::Curriculum);
  const Rid stages = d.widget(names::kCurriculumStages);
  CHECK(Rtable_get_row_count(stages) == 14);
  CHECK(std::string(Rtable_get_cell(stages, 0, 2)) == "DISPONÍVEL");
  REQUIRE(d.click(names::kCurriculumLessonStart));
  REQUIRE(app.vm().screen() == Screen::Lesson);
  CHECK(d.text(names::kLessonTitle) == "Uma barra, quatro preços");
  for (int guard = 0; guard < 40 && !app.vm().shell().lesson.finished(); ++guard) {
    const LessonStep* s = app.vm().shell().lesson.step();
    if (s->is_question()) REQUIRE(d.click(names::kLessonChoices[static_cast<std::size_t>(s->question->answer)]));
    REQUIRE(d.click(names::kLessonNext));
  }
  REQUIRE(app.vm().shell().lesson.finished());
  CHECK(d.text(names::kLessonScore) == "5 de 5 respostas certas na primeira tentativa");
  REQUIRE(d.click(names::kLessonFinish));
  CHECK(app.vm().screen() == Screen::Curriculum);
  CHECK(std::string(Rtable_get_cell(stages, 0, 2)) == "APROVADO");
  REQUIRE(d.click(names::kCurriculumBack));
  CHECK(app.vm().screen() == Screen::Menu);
}

TEST_CASE("headless shell: journal and settings (palette switch, text scale, a remapped shortcut)") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  REQUIRE(d.click(names::kMenuJournal));
  CHECK(app.vm().screen() == Screen::Journal);
  CHECK(d.text(names::kJournalEmpty).starts_with("Nenhum plano"));
  REQUIRE(d.click(names::kJournalBack));
  REQUIRE(d.click(names::kMenuSettings));
  REQUIRE(app.vm().screen() == Screen::Settings);
  REQUIRE(d.click(names::kSettingsPalette));
  CHECK(app.vm().settings().colorblind);
  REQUIRE(d.click(names::kSettingsBloom));
  CHECK_FALSE(app.vm().settings().crt.bloom);
  // Text scale by keyboard: the slider steps 0.25.
  REQUIRE(d.click(names::kSettingsTextScale));
  d.key(R_KEY_HOME);
  d.key(R_KEY_RIGHT);
  d.key(R_KEY_RIGHT);
  CHECK(app.vm().settings().text_scale == 1.5f);
  CHECK(Raccessibility_get_text_scale() == doctest::Approx(1.5f));
  app.vm().set_text_scale(1.0f);
  d.frames(2);
  // Remap "Ticket: buy" (row 2) by pressing K.
  REQUIRE(d.click(names::kSettingsKeys));
  d.key(R_KEY_HOME);
  d.key(R_KEY_DOWN);
  d.key(R_KEY_DOWN);
  CHECK(app.vm().shell().key_row == 2);
  REQUIRE(d.click(names::kSettingsKeyChange));
  CHECK(app.vm().shell().capturing_key);
  d.key(R_KEY_K);
  Rwindow_inject_text(app.view().window(), "k");
  d.frames(2);
  CHECK(app.vm().shell().keymap.at("buy") == "k");
  CHECK(d.text(names::kSettingsKeyMessage) == "\"Boleta: comprar\" agora usa K.");
  // Sound off from Settings mutes the terminal too (one setting, kept with the prefs).
  REQUIRE(d.click(names::kSettingsSound));
  CHECK(app.vm().game().muted());
  CHECK(app.vm().audio().settings().muted);
  REQUIRE(d.click(names::kSettingsLangEn));
  CHECK(d.text(names::kSettingsTitle) == "Settings");
}

// Playtest finding: a run left mid-mission (window closed, or the process killed: OFF02 covers the
// kill itself) was saved but the next launch's menu did not offer it.
TEST_CASE("headless shell: a run left mid-mission is offered by the next launch's menu and resumes") {
  testing::TempDir dir;
  int step = 0;
  double qty = 0;
  std::string account;
  {
    RavenSession raven;
    App app(builtin_catalog(), [] { return 11u; }, std::make_unique<storage::Storage>(dir.path()));
    REQUIRE(app.build(1280, 800));
    Driver d(app);
    d.frames(2);
    CHECK(Rwidget_get_visibility(d.widget(names::kMenuResumeRow)) != R_VISIBLE);  // nothing saved yet
    testing::Terminal t(app, d);
    t.open(0);
    t.skip_question();
    d.choose(names::game::kSizeBy, 1);
    t.type(names::kGameQty, "2");
    REQUIRE(d.click(names::kGameBuy));
    t.submit_with_plan();
    for (int i = 0; i < 3; ++i) t.advance();
    step = t.g().step;
    qty = t.g().position.qty;
    account = d.text(names::kGameAccount);
    REQUIRE(qty > 0);
  }
  RavenSession raven;
  App app(builtin_catalog(), [] { return 99u; }, std::make_unique<storage::Storage>(dir.path()));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  REQUIRE(Rwidget_get_visibility(d.widget(names::kMenuResumeRow)) == R_VISIBLE);
  CHECK(d.text(names::kMenuResumeText) == "Missão 01 · Range · seed 11");
  REQUIRE(d.click(names::kMenuResume));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Game; }));
  const GameState& g = *app.vm().game().state();
  CHECK(g.step == step);
  CHECK(g.position.qty == doctest::Approx(qty));
  CHECK(d.text(names::kGameAccount) == account);
  // Played on from there, and the offer is gone once it is back in play.
  testing::Terminal t(app, d);
  t.advance();
  CHECK(app.vm().game().state()->step == step + 1);
  CHECK_FALSE(app.vm().saved_run().has_value());
}

namespace {

// The item under a click on the first row a player sees in a list.
int first_visible_item(Driver& d, std::string_view list) {
  const Rid w = d.widget(list);
  Rlist_view_set_selected(w, -1);
  float x = 0, y = 0, width = 0, h = 0;
  Rwidget_get_rect(w, &x, &y, &width, &h);
  d.click_at(x + 20, y + 6);
  return Rlist_view_get_selected(w);
}

}  // namespace

// A list filled and given its selection before its screen was laid out used to scroll by one row: the
// menu opened on "Mission 02" at the top with Mission 01 selected out of sight, the curriculum's lessons
// on the second lesson.
TEST_CASE("headless shell: lists open at their first row (menu missions, curriculum lessons)") {
  for (auto [w, h] : {std::pair{1280, 800}, std::pair{1024, 768}}) {
    RavenSession raven;
    App app(builtin_catalog(), [] { return 1u; });
    REQUIRE(app.build(w, h));
    Driver d(app);
    d.frames(3);
    CHECK(first_visible_item(d, names::kMenuList) == 0);
    REQUIRE(d.click(names::kMenuCurriculum));
    d.frames(3);
    CHECK(first_visible_item(d, names::kCurriculumLessons) == 0);
  }
}
