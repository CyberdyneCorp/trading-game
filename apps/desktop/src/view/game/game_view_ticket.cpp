// GameView: the perpetual ticket, the protection of an open position, the option ticket and chain.
#include <algorithm>
#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_layout.h>
#include <raven/raven_widget.h>

#include "view/game/game_view.hpp"
#include "view/raven/raven_ui.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop::view {

namespace n = names::game;
using game::GameViewModel;
using game::TicketViewModel;

namespace {

std::string chip_name(std::string_view field, int i) { return std::string(field) + "-chip-" + std::to_string(i); }

void select(Rid combo, int index) {
  if (combo != R_INVALID_ID && Rcombo_box_get_selected(combo) != index) Rcombo_box_set_selected(combo, index);
}

void check(Rid box, bool on) {
  if (box != R_INVALID_ID && (Rcheckbox_is_checked(box) != 0) != on) Rcheckbox_set_checked(box, on ? R_TRUE : R_FALSE);
}

std::string kv(const std::vector<std::pair<std::string, std::string>>& rows) {
  std::string out;
  for (const auto& [k, v] : rows) out += (out.empty() ? "" : "\n") + k + ": " + v;
  return out;
}

std::vector<std::pair<std::string, std::string>> option_rows(const game::Texts& t, const game::OptionTicketView& v) {
  std::vector<std::pair<std::string, std::string>> rows;
  if (!v.has_quote) return rows;
  rows = {{t.tr("opt.premium"), v.premium}, {t.tr("opt.breakeven"), v.breakeven}};
  if (v.max_loss) rows.emplace_back(t.tr("opt.maxLoss"), *v.max_loss);
  rows.emplace_back(t.tr("ticket.estFee"), v.fee);
  if (v.margin) rows.emplace_back(t.tr("opt.margin"), *v.margin);
  rows.emplace_back(t.tr("opt.greeks"), v.greeks);
  return rows;
}

std::vector<game::Line> option_notes(const game::Texts& t, const game::OptionTicketView& v) {
  std::vector<game::Line> notes;
  for (const auto* s : {&v.short_locked, &v.unbounded, &v.no_bid, &v.settling, &v.cant_afford}) {
    if (!s->empty()) notes.push_back({*s, game::Tone::Attention});
  }
  if (!v.curve.empty()) notes.push_back({t.tr("opt.payoff"), game::Tone::Muted});
  return notes;
}

}  // namespace

void GameView::bind_input(std::string_view name, std::string TicketViewModel::*field) {
  cb_.text(find(name), [this, field](const std::string& text) {
    if (vm().ticket.*field == text) return;
    vm().ticket.*field = text;
    vm().touch();
  });
  cb_.commit(find(name), [this] { vm().submit(); });  // Enter submits; advance never takes Enter
}

void GameView::bind_ticket() {
  cb_.click(find(n::kTabPerp), [this] { vm().set_tab(game::Tab::Perp); });
  cb_.click(find(n::kTabOptions), [this] { vm().set_tab(game::Tab::Options); });
  cb_.choose(find(n::kType), [this](int i) {
    vm().ticket.type = static_cast<game::TicketType>(std::min(std::max(0, i), 2));
    vm().touch();
  });
  cb_.choose(find(n::kSizeBy), [this](int i) {
    vm().ticket.size_by = static_cast<game::SizeBy>(std::min(std::max(0, i), 3));
    vm().touch();
  });
  bind_input(n::kLeverage, &TicketViewModel::leverage);
  bind_input(n::kPrice, &TicketViewModel::price);
  bind_input(n::kStop, &TicketViewModel::stop);
  bind_input(n::kTp, &TicketViewModel::take_profit);
  cb_.commit(find(names::kGameQty), [this] { vm().submit(); });
  cb_.check(find(n::kBeyondConfirm), [this](bool on) {
    if (vm().state()) vm().ticket.confirm_beyond(*vm().state(), on);
    vm().touch();
  });
  cb_.click(find(n::kSubmit), [this] { vm().submit(); });
}

void GameView::bind_chips(std::string_view field, int count, bool stop_level, bool protect) {
  for (int i = 0; i < count; ++i) {
    cb_.click(find(chip_name(field, i)), [this, i, stop_level, protect] { use_chip(i, stop_level, protect); });
  }
}

// A preset chip writes its level into the field it belongs to (snapped to the tick, in the locale).
void GameView::use_chip(int i, bool stop_level, bool protect) {
  const GameState* g = vm().state();
  if (g == nullptr) return;
  const game::TicketView v = vm().ticket.view(vm().texts(), *g, vm().busy());
  const auto& stops = protect ? v.prot_stop_chips : v.stop_chips;
  const auto& targets = protect ? v.prot_tp_chips : v.tp_chips;
  const auto& chips = stop_level ? stops : targets;
  if (i >= static_cast<int>(chips.size()) || !chips[static_cast<std::size_t>(i)].value) return;
  const double value = *chips[static_cast<std::size_t>(i)].value;
  if (protect) vm().ticket.set_prot_level(stop_level, value, g->contract.tick, vm().texts().locale());
  else vm().ticket.set_level(stop_level, value, g->contract.tick, vm().texts().locale());
  vm().touch();
}

void GameView::bind_levels() {
  bind_chips(n::kStop, 5, true, false);
  bind_chips(n::kTp, 3, false, false);
  cb_.click(find(n::kStopPick), [this] { vm().start_pick(game::PickTarget::Stop); });
  cb_.click(find(n::kTpPick), [this] { vm().start_pick(game::PickTarget::Target); });
}

void GameView::bind_protect() {
  const auto field = [this](std::string_view name, std::string TicketViewModel::*member) {
    cb_.text(find(name), [this, member](const std::string& text) {
      vm().ticket.*member = text;
      vm().touch();
    });
  };
  field(n::kProtStop, &TicketViewModel::prot_stop);
  field(n::kProtTp, &TicketViewModel::prot_tp);
  cb_.text(find(n::kTrail), [this](const std::string& text) {
    (vm().ticket.trail_by_pct ? vm().ticket.prot_trail_pct : vm().ticket.prot_trail) = text;
    vm().touch();
  });
  bind_chips(n::kProtStop, 5, true, true);
  bind_chips(n::kProtTp, 3, false, true);
  cb_.click(find(n::kProtStopPick), [this] { vm().start_pick(game::PickTarget::ProtStop); });
  cb_.click(find(n::kProtTpPick), [this] { vm().start_pick(game::PickTarget::ProtTarget); });
  cb_.click(find(n::kTrailByPrice), [this] {
    vm().ticket.trail_by_pct = false;
    vm().touch();
  });
  cb_.click(find(n::kTrailByPct), [this] {
    vm().ticket.trail_by_pct = true;
    vm().touch();
  });
  static constexpr int kPcts[] = {1, 2, 3, 5};
  for (int i = 0; i < 4; ++i) {
    cb_.click(find(chip_name(n::kTrail, i)), [this, i] {
      const GameState* g = vm().state();
      if (g == nullptr) return;
      if (vm().ticket.trail_by_pct) {
        vm().ticket.set_trail_pct(kPcts[i], vm().texts().locale());
      } else {
        const auto chips = vm().ticket.view(vm().texts(), *g, false).trail_chips;
        if (i < static_cast<int>(chips.size()) && chips[static_cast<std::size_t>(i)].value) {
          vm().ticket.prot_trail = fmt::price_input_text(*chips[static_cast<std::size_t>(i)].value, g->contract.tick, vm().texts().locale());
        }
      }
      vm().touch();
    });
  }
  cb_.check(find(n::kProtBeyondConfirm), [this](bool on) {
    vm().ticket.confirm_prot_beyond(on);
    vm().touch();
  });
  cb_.click(find(n::kProtect), [this] { vm().protect(); });
  cb_.click(find(n::kClose), [this] { vm().close_position(); });
}

void GameView::bind_options() {
  cb_.click(find(n::kOptBuy), [this] {
    vm().option_ticket.buy = true;
    vm().touch();
  });
  cb_.click(find(n::kOptSell), [this] {
    vm().option_ticket.buy = false;
    vm().touch();
  });
  cb_.text(find(n::kOptQty), [this](const std::string& text) {
    vm().option_ticket.qty = text;
    vm().touch();
  });
  cb_.text(find(n::kOptLimit), [this](const std::string& text) {
    vm().option_ticket.limit_price = text;
    vm().touch();
  });
  cb_.choose(find(n::kOptType), [this](int i) {
    vm().option_ticket.limit = i == 1;
    vm().touch();
  });
  cb_.commit(find(n::kOptQty), [this] { vm().submit_option(); });
  cb_.commit(find(n::kOptLimit), [this] { vm().submit_option(); });
  cb_.click(find(n::kOptSubmit), [this] { vm().submit_option(); });
}

void GameView::set_chips(std::string_view field, const std::vector<game::LevelChip>& chips) {
  for (std::size_t i = 0; i < 5; ++i) {
    const Rid chip = find(chip_name(field, static_cast<int>(i)));
    if (chip == R_INVALID_ID) continue;
    set_visible(chip, i < chips.size());
    if (i >= chips.size()) continue;
    ui::set_text(chip, chips[i].label);
    Rwidget_set_tooltip(chip, chips[i].title.c_str());
    set_enabled(chip, chips[i].value.has_value());
  }
}

void GameView::apply_levels(const game::TicketView& v) {
  set_chips(n::kStop, v.stop_chips);
  set_chips(n::kTp, v.tp_chips);
  set_label(find(n::kStopHint), v.stop_warn ? "\xE2\x9A\xA0 " + *v.stop_warn : v.stop_hint.value_or(""));
  set_color(find(n::kStopHint), v.stop_warn ? game::Tone::Attention : game::Tone::Muted);
  set_label(find(n::kTpHint), v.tp_warn ? "\xE2\x9A\xA0 " + *v.tp_warn : v.tp_hint.value_or(""));
  set_color(find(n::kTpHint), v.tp_warn ? game::Tone::Attention : game::Tone::Muted);
}

void GameView::apply_ticket(const game::Texts& t, const GameState& g) {
  GameViewModel& m = vm();
  const bool options = g.options.enabled;
  set_visible(find(n::kTabs), options);
  Rswitcher_set_index(find(n::kTicketSwitch), options && m.tab() == game::Tab::Options ? 1 : 0);
  ui::set_text(find(n::kTabPerp), std::string(m.tab() == game::Tab::Perp ? "\xE2\x96\xB8 " : "") + t.tr("opt.tabPerp"));
  ui::set_text(find(n::kTabOptions), std::string(m.tab() == game::Tab::Options ? "\xE2\x96\xB8 " : "") + t.tr("opt.tabOptions"));
  const game::TicketView v = m.ticket.view(t, g, m.busy());
  const bool buy = m.ticket.side == game::Side::Buy;
  ui::set_text(find(names::kGameBuy), std::string(buy ? "[\xE2\x96\xB2 " : "\xE2\x96\xB2 ") + t.tr("ticket.buy") + (buy ? "]" : ""));
  ui::set_text(find(names::kGameSell), std::string(!buy ? "[\xE2\x96\xBC " : "\xE2\x96\xBC ") + t.tr("ticket.sell") + (!buy ? "]" : ""));
  set_label(find(n::kShortLocked), v.short_locked);
  set_visible(find(n::kShortLocked), !v.short_locked.empty());
  select(find(n::kType), static_cast<int>(m.ticket.type));
  select(find(n::kSizeBy), static_cast<int>(m.ticket.size_by));
  set_label(find(n::kLevLabel), v.leverage_label);
  ui::set_text(find(n::kLeverage), m.ticket.leverage);
  set_visible(find(n::kPriceBox), v.needs_price);
  set_label(find(n::kPriceLabel), v.price_label);
  ui::set_text(find(n::kPrice), m.ticket.price);
  Rtext_input_set_placeholder(find(n::kPrice), v.price_placeholder.c_str());
  set_label(find(names::kGameQtyLabel), v.amount_label);
  ui::set_text(find(n::kStop), m.ticket.stop);
  ui::set_text(find(n::kTp), m.ticket.take_profit);
  apply_levels(v);
  set_label(find(n::kEstimate), kv({{t.tr("ticket.qty"), v.est_qty},
                                    {t.tr("ticket.estNotional"), v.est_notional},
                                    {t.tr("ticket.estEntry"), v.est_entry},
                                    {t.tr("ticket.estMargin"), v.est_margin},
                                    {t.tr("ticket.estFee"), v.est_fee},
                                    {t.tr("ticket.estLiq"), v.est_liq},
                                    {t.tr("ticket.estRisk"), v.est_risk}}));
  std::vector<game::Line> notes;
  const auto note = [&](const std::string& text, game::Tone tone) {
    if (!text.empty()) notes.push_back({text, tone});
  };
  note(v.risk_needs_stop, game::Tone::Attention);
  note(v.shock, game::Tone::Attention);
  note(v.relever, v.relever_closer ? game::Tone::Attention : game::Tone::Info);
  note(v.opposing, game::Tone::Attention);
  note(v.cant_afford, game::Tone::Down);
  note(v.above_tier, game::Tone::Down);
  note(v.beyond_liq, game::Tone::Down);
  note(v.no_stop, game::Tone::Attention);
  note(t.tr("ticket.attachNote"), game::Tone::Muted);
  notes_.set(notes);
  set_visible(find(n::kBeyondConfirm), v.needs_beyond_confirm);
  check(find(n::kBeyondConfirm), m.ticket.beyond_confirmed(g));
  ui::set_text(find(n::kSubmit), v.submit_label);
  set_enabled(find(n::kSubmit), v.can_submit);
  apply_protect(t, v);
}

void GameView::apply_protect(const game::Texts& t, const game::TicketView& v) {
  GameViewModel& m = vm();
  set_visible(find(n::kProtectBox), v.protect_visible);
  if (!v.protect_visible) return;
  set_label(find(n::kUnprotected), v.unprotected);
  set_visible(find(n::kUnprotected), !v.unprotected.empty());
  ui::set_text(find(n::kProtStop), m.ticket.prot_stop);
  ui::set_text(find(n::kProtTp), m.ticket.prot_tp);
  set_chips(n::kProtStop, v.prot_stop_chips);
  set_chips(n::kProtTp, v.prot_tp_chips);
  set_label(find(n::kProtStopHint), v.prot_stop_warn ? "\xE2\x9A\xA0 " + *v.prot_stop_warn : v.prot_stop_hint.value_or(""));
  set_label(find(n::kProtTpHint), v.prot_tp_warn ? "\xE2\x9A\xA0 " + *v.prot_tp_warn : v.prot_tp_hint.value_or(""));
  const bool pct = m.ticket.trail_by_pct;
  ui::set_text(find(n::kTrailByPrice), std::string(!pct ? "\xE2\x96\xB8 " : "") + t.tr("ticket.trailByPrice"));
  ui::set_text(find(n::kTrailByPct), std::string(pct ? "\xE2\x96\xB8 " : "") + t.tr("ticket.trailByPct"));
  ui::set_text(find(n::kTrail), pct ? m.ticket.prot_trail_pct : m.ticket.prot_trail);
  Rtext_input_set_placeholder(find(n::kTrail), t.tr(pct ? "ticket.trailPct" : "ticket.trail").c_str());
  std::vector<game::LevelChip> trail = v.trail_chips;
  if (pct) trail = {{"1%", "", 1.0}, {"2%", "", 2.0}, {"3%", "", 3.0}, {"5%", "", 5.0}};
  set_chips(n::kTrail, trail);
  set_label(find(n::kTrailHint), pct ? v.trail_hint : std::string());
  set_label(find(n::kProtBeyond), v.prot_beyond ? "\xE2\x9A\xA0 " + t.tr("ticket.stopBeyondLiq") : std::string());
  set_visible(find(n::kProtBeyond), v.prot_beyond);
  set_visible(find(n::kProtBeyondConfirm), v.prot_beyond);
  check(find(n::kProtBeyondConfirm), m.ticket.prot_beyond_confirmed());
  set_enabled(find(n::kProtect), v.can_protect);
  set_enabled(find(n::kClose), !m.busy());
  ui::set_text(find(n::kClose), "\xE2\x9C\x95 " + t.tr("ticket.close") + " [" + game::key_label(m.keymap().key(game::Action::Close)) + "]");
}

void GameView::apply_options(const game::Texts& t, const GameState& g) {
  GameViewModel& m = vm();
  if (!g.options.enabled) return;
  const game::OptionTicketView v = m.option_ticket.view(t, g, m.busy());
  set_label(find(n::kOptSeries), v.has_quote ? v.series + "  [" + t.tr("opt.simulated") + "]" : t.tr("opt.none"));
  const bool buy = m.option_ticket.buy;
  ui::set_text(find(n::kOptBuy), std::string(buy ? "[\xE2\x96\xB2 " : "\xE2\x96\xB2 ") + t.tr("opt.buy") + (buy ? "]" : ""));
  ui::set_text(find(n::kOptSell), std::string(!buy ? "[\xE2\x96\xBC " : "\xE2\x96\xBC ") + t.tr("opt.sell") + (!buy ? "]" : ""));
  ui::set_text(find(n::kOptQty), m.option_ticket.qty);
  select(find(n::kOptType), m.option_ticket.limit ? 1 : 0);
  set_visible(find(n::kOptLimit), m.option_ticket.limit);
  ui::set_text(find(n::kOptLimit), m.option_ticket.limit_price);
  Rtext_input_set_placeholder(find(n::kOptLimit), v.limit_placeholder.c_str());
  set_label(find(n::kOptEstimate), kv(option_rows(t, v)));
  opt_notes_.set(option_notes(t, v));
  ui::set_text(find(n::kOptSubmit), t.tr("opt.submit"));
  set_enabled(find(n::kOptSubmit), v.can_submit);
}

namespace {

// What the chain shows; the rows are rebuilt only when it changes (quotes move every bar, a click on
// a quote never rebuilds them).
std::string chain_key(const game::Texts& t, const std::vector<game::ChainExpiry>& chain) {
  std::string key(locale_code(t.locale()));
  const auto cell = [&key](const std::optional<game::ChainCell>& c) {
    if (c) key += "," + std::to_string(c->series) + c->bid + "/" + c->ask;
  };
  for (const auto& e : chain) {
    key += "|" + e.title;
    for (const auto& r : e.rows) {
      key += ";" + r.strike + (r.held ? "*" : "");
      cell(r.call);
      cell(r.put);
    }
  }
  return key;
}

}  // namespace

void GameView::apply_chain(const game::Texts& t, const GameState& g) {
  const Rid box = find(n::kChain);
  set_visible(find(n::kChainBox), g.options.enabled);
  if (!g.options.enabled || box == R_INVALID_ID) return;
  const auto chain = game::option_chain(t, g, vm().option_ticket.series);
  const std::string key = chain_key(t, chain);
  if (key == chain_key_ && !chain_rows_.empty()) return;
  chain_key_ = key;
  build_chain(box, t, chain);
}

void GameView::build_chain(Rid box, const game::Texts& t, const std::vector<game::ChainExpiry>& chain) {
  clear_rows(box, chain_rows_);
  const auto label = [&](const std::string& text, game::Tone tone) {
    const Rid l = Rlabel_create(text.c_str());
    set_color(l, tone);
    Rlabel_set_wrap(l, R_TRUE);
    Rwidget_add_child(box, l);
    chain_rows_.push_back(l);
  };
  label(t.tr("opt.chain") + "  [" + t.tr("opt.simulated") + "]", game::Tone::Plain);
  for (const auto& e : chain) {
    label(e.title, game::Tone::Muted);
    for (const auto& r : e.rows) add_chain_row(box, r);
  }
  label(t.tr("opt.pick"), game::Tone::Muted);
}

// Call bid | call ask | strike | put bid | put ask; a bid sells, an ask buys.
void GameView::add_chain_row(Rid box, const game::ChainRow& r) {
  const Rid row = Rwidget_create(R_WIDGET_HBOX);
  Rwidget_add_child(box, row);
  chain_rows_.push_back(row);
  const auto quote_button = [&](const std::optional<game::ChainCell>& c, bool buy) {
    const Rid b = Rbutton_create(c ? (buy ? c->ask : c->bid).c_str() : "\xE2\x80\x94");
    Rwidget_set_size(b, 70, 0);
    Rwidget_add_child(row, b);
    if (!c) return Rwidget_set_enabled(b, R_FALSE);
    const int series = c->series;
    cb_.click(b, [this, series, buy] { vm().pick_option(series, buy); });
    Rwidget_set_tooltip(b, (std::string(buy ? "ask" : "bid") + "  IV " + c->iv + "  \xCE\x94 " + c->delta).c_str());
  };
  quote_button(r.call, false);
  quote_button(r.call, true);
  const Rid strike = Rlabel_create((" " + r.strike + (r.held ? " \xE2\x97\x8F" : "") + " ").c_str());
  set_color(strike, r.itm_call ? game::Tone::Up : game::Tone::Plain);
  Rwidget_add_child(row, strike);
  quote_button(r.put, false);
  quote_button(r.put, true);
}

void GameView::clear_rows(Rid box, std::vector<Rid>& rows) {
  (void)box;
  for (const Rid r : rows) {
    for (int i = 0; i < Rwidget_get_child_count(r); ++i) cb_.erase(Rwidget_get_child(r, i));
    cb_.erase(r);
    Rwidget_destroy(r);
  }
  rows.clear();
}

}  // namespace tradelab::desktop::view
