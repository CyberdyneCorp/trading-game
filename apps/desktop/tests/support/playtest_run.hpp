// The playtest's player (windowed/test_playtest*.cpp): one Vulkan session, a run with its captures and
// text report under $TRADELAB_CAPTURE_DIR/playtest, and the check that no text is cut by its panel.
#pragma once

#include <doctest/doctest.h>

#include <raven/raven_resources.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/png.hpp"
#include "support/text_dump.hpp"
#include "support/window_size.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop::testing {

namespace gn = tradelab::desktop::names::game;

inline std::filesystem::path out_dir() {
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

// One mission played by a script in a `width` x `height` window: its captures and report carry `name`.
class Run {
 public:
  Run(App& app, Driver& d, std::string name, int width, int height)
      : app_(app), d_(d), t_(app, d), name_(std::move(name)), exact_size_(window_has_size(d.window(), width, height)) {}

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
    CHECK(write_png((out_dir() / (name_ + "-" + label + ".png")).string(), rgba, w, h));
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
    report_ << "---- debrief\n" << dump_texts(app_.view().screen_root(Screen::Debrief));
    std::ofstream(out_dir() / (name_ + ".txt")) << report_.str();
  }

  // No text of the current screen is cut by its own box or by the panel holding it. Asserted only in a
  // window of the requested size (support/window_size.hpp); reported in any case.
  void clipping(const std::string& when) {
    d_.frames(3);
    const std::string cut = overflow_report(app_.view().screen_root(app_.vm().screen()));
    report_ << "---- clipped (" << when << ")\n" << cut;
    if (exact_size_) CHECK_MESSAGE(cut.empty(), name_ << " " << when << ":\n" << cut);
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
  bool exact_size_;
  std::string last_account_;
  int chains_ = 0;
  std::ostringstream report_;
};

}  // namespace tradelab::desktop::testing
