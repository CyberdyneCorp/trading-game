// AppViewModel::bindings(): the localized text of each named widget, per screen, built from the web's
// catalog keys the matching Svelte component uses (MissionMenu, Briefing, TopBar/RightRail, Debrief).
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop {

namespace {

constexpr std::string_view kSep = " \xC2\xB7 ";  // " · "

std::string str(int v) { return std::to_string(v); }

}  // namespace

std::vector<Binding> AppViewModel::bindings() const {
  switch (screen_) {
    case Screen::Menu: return menu_bindings();
    case Screen::Briefing: return briefing_bindings();
    case Screen::Game: return game_bindings();
    case Screen::Debrief: return debrief_bindings();
    case Screen::Curriculum:
    case Screen::Journal:
    case Screen::Lesson:
    case Screen::Settings: break;  // screens::*
  }
  return {};
}

std::vector<Binding> AppViewModel::menu_bindings() const {
  std::vector<Binding> b = {
      {names::kMenuTitle, tr("menu.missions")},
      {names::kMenuSubtitle, tr("app.subtitle")},
      {names::kMenuNote, tr("menu.freePlayNote")},
      {names::kMenuStart, waiting() ? tr("desktop.waiting") : tr("menu.start")},
      {names::kMenuLanguage, tr("desktop.language")},
      {names::kMenuDisclaimer, tr("app.disclaimer")},
  };
  if (missions_.empty()) return b;
  const MissionInfo& m = missions_[static_cast<std::size_t>(selected_)];
  const int bars = m.total_bars - m.warmup_bars;
  b.push_back({names::kMenuDetailTitle, mission_text(m.id, "title")});
  b.push_back({names::kMenuDetailTagline, mission_text(m.id, "tagline")});
  b.push_back({names::kMenuDetailTopic, mission_text(m.id, "topic")});
  b.push_back({names::kMenuDetailTags, tr("menu.leverageCap") + " " + str(m.leverage_cap) + "x" + std::string(kSep) +
                                           fmt::fmt_num(bars, locale(), 0) + " " + tr("menu.bars") + std::string(kSep) +
                                           tr("top.synthetic")});
  return b;
}

std::vector<Binding> AppViewModel::briefing_bindings() const {
  if (!snap_) return {};
  const Snapshot& s = *snap_;
  const std::string instrument = tr(s.allow_short ? "brief.instrumentValue" : "brief.instrumentValueLong");
  return {
      {names::kBriefTitle, tr("brief.title")},
      {names::kBriefMission, mission_text(s.mission, "title")},
      {names::kBriefTagline, mission_text(s.mission, "tagline")},
      {names::kBriefCapital, tr("brief.capital") + ": " + fmt::fmt_money(s.initial_balance, locale())},
      {names::kBriefInstrument, tr("brief.instrument") + ": " + instrument},
      {names::kBriefLeverage, tr("brief.leverage") + ": " + str(s.leverage_cap) + "x"},
      {names::kBriefMode, tr("brief.mode") + ": " + tr("brief.modeSynthetic", {{"seed", std::to_string(s.seed)}})},
      {names::kBriefGo, tr("brief.go")},
      {names::kBriefBack, tr("debrief.menu")},
  };
}

std::string AppViewModel::position_text() const {
  const Snapshot& s = *snap_;
  const double qty = fmt::num(s.position_qty);
  if (!(std::fabs(qty) > 0)) return tr("rail.flat");
  const std::string side = tr(qty > 0 ? "rail.long" : "rail.short");
  return side + " " + fmt::fmt_num(std::fabs(qty), locale(), 1) + " @ " + fmt::fmt_price(s.position_entry, locale());
}

// The account line: equity, available balance and position, as the web's rail lists them.
std::string AppViewModel::account_line() const {
  const Snapshot& s = *snap_;
  return tr("rail.equity") + " " + fmt::fmt_money(s.equity, locale()) + std::string(kSep) + tr("rail.available") + " " +
         fmt::fmt_money(s.available, locale()) + std::string(kSep) + tr("rail.position") + ": " + position_text();
}

std::string AppViewModel::game_notice() const {
  if (snap_ && snap_->progress_total > 1 && snap_->progress_done < snap_->progress_total) {
    return tr("desktop.progress", {{"done", str(snap_->progress_done)}, {"total", str(snap_->progress_total)}});
  }
  return game_->notice().text.empty() ? notice_ : game_->notice().text;
}

std::vector<Binding> AppViewModel::game_bindings() const {
  if (!snap_) return {};
  const Snapshot& s = *snap_;
  const int turn = std::max(0, s.step - s.warmup_bars + 1);
  const int playable = s.last_decision_step - s.warmup_bars + 1;
  return {
      {names::kGameMarket, tr("top.market") + " " + s.alias + " " + tr("top.synthetic")},
      {names::kGameMission, mission_text(s.mission, "title")},
      {names::kGameTurn, tr("top.step") + " " + str(turn) + " " + tr("top.of") + " " + str(playable)},
      {names::kGameAccount, account_line()},
      {names::kGameQtyLabel, tr("ticket.qty")},
      {names::kGameBuy, "\xE2\x96\xB2 " + tr("ticket.buy")},   // ▲
      {names::kGameSell, "\xE2\x96\xBC " + tr("ticket.sell")},  // ▼
      {names::kGameAdvance, "\xE2\x96\xB6 " + tr("top.advance") + " [" +
                                game::key_label(game_->keymap().key(game::Action::Advance)) + "]"},  // ▶
      {names::kGameNotice, game_notice()},
      {names::kGameMenu, tr("top.menu")},
  };
}

std::vector<Binding> AppViewModel::debrief_bindings() const {
  if (!snap_ || !snap_->debrief) return {};
  const Snapshot& s = *snap_;
  const Debrief& d = *s.debrief;
  const bool bankrupt = s.status == "bankrupt";
  const double pnl = fmt::num(d.net_pnl);
  const std::string score = d.score_total < 0 ? std::string("\xE2\x80\x94") : str(d.score_total);  // —
  return {
      {names::kDebriefTitle, tr(bankrupt ? "debrief.gameOver" : "debrief.finished")},
      {names::kDebriefText, bankrupt ? tr("debrief.gameOverText") : mission_text(s.mission, "title")},
      {names::kDebriefOutcome, tr("debrief.outcome")},
      {names::kDebriefPnl, std::string(fmt::sign_mark(pnl)) + " " + fmt::fmt_money(pnl, locale(), true)},
      {names::kDebriefPnlPct,
       fmt::fmt_pct(pnl / fmt::num(s.initial_balance), locale(), 2, true) + std::string(kSep) + tr("debrief.netPnl")},
      {names::kDebriefHold, tr("debrief.vsHold") + ": " + fmt::fmt_money(d.buy_and_hold_pnl, locale(), true)},
      {names::kDebriefScore, tr("debrief.score") + ": " + score + "/100"},
      {names::kDebriefMenu, tr("debrief.menu")},
      {names::kDebriefAgain, tr("debrief.again")},
  };
}

}  // namespace tradelab::desktop
