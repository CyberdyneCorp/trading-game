// Read-only terminal panels as texts (web RightRail, OrdersPanel, EventFeed, NewsPanel, AdvisorPanel,
// TopBar): computed from the engine state, nothing here sends a command.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/game_state.hpp"
#include "viewmodel/game/shortcuts.hpp"
#include "viewmodel/game/texts.hpp"

namespace tradelab::desktop::game {

struct KeyValue {
  std::string key;
  std::string value;
  Tone tone = Tone::Plain;
};

struct RailView {
  std::string equity;
  Line pnl;
  std::vector<KeyValue> account;  // wallet, available, realized, fees, funding
  bool open = false;
  Line position;  // "▲ COMPRADO 1,5" or "ZERADO · Alavancagem 1x"
  std::vector<KeyValue> details;  // entry, unrealized, margin, notional, leverage
  std::string liq_label, liq_price, liq_distance;
  bool liq_danger = false;
  std::string liq_warning;
  std::optional<KeyValue> planned_risk;
  bool options = false;
  std::vector<Line> option_positions;
  std::vector<KeyValue> option_book;
  std::string funding_title;
  KeyValue funding;
  std::string funding_in, funding_who;
  std::vector<std::string> objectives;
};
RailView rail_view(const Texts& t, const GameState& g, std::optional<double> planned_risk);

struct OrderRow {
  int id = 0;
  std::string text;
  Tone tone = Tone::Plain;
};
std::vector<OrderRow> order_rows(const Texts& t, const GameState& g, const SeriesNames& names);

// Newest first, at most `max` lines, each "[step] text".
std::vector<Line> feed_lines(const Texts& t, const GameState& g, const SeriesNames& names, std::size_t max = 80);

struct NewsView {
  std::vector<std::string> upcoming;  // announced releases: kind, bar, consensus (never the outcome)
  std::string upcoming_note;
  std::vector<std::string> calendar;  // perk: rate decisions not already announced
  std::vector<Line> items;            // newest first: headline, then its meta line
  std::string note;
};
NewsView news_view(const Texts& t, const GameState& g);

struct AdvisorView {
  std::string points;
  bool can_ask = false;
  std::string ask_tooltip;
  std::string intro;  // shown without a read
  bool has_read = false;
  Line stance;
  std::string detail;  // stop and size
  std::vector<Line> reasons;
  bool actionable = false;
  std::string disclaimer;
};
AdvisorView advisor_view(const Texts& t, const GameState& g, bool busy);

struct TopBarView {
  std::string market, mission, turn;
};
TopBarView top_bar(const Texts& t, const GameState& g);

// packs.ts branchTurn: the first generated turn of a branch-mode market (null otherwise).
std::optional<int> branch_turn(const GameState& g);

// The terminal's buttons name the key that does the same thing, from the player's (remapped) key map:
// the web's <kbd> hints (TopBar, OrderTicket) and the tooltips of the icon buttons.
struct KeyHints {
  std::string patterns;  // button text: "Padrões [P]"
  std::string patterns_tip, sound_tip, help_tip, journal_tip, options_tip, buy_tip, sell_tip;
};
KeyHints key_hints(const Texts& t, const Keymap& map, bool muted);

}  // namespace tradelab::desktop::game
