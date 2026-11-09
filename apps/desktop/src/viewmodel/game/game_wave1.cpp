// GameViewModel: wave 1 of add-practice-and-advanced-strategies (web Terminal.svelte, TrailNotice.svelte,
// OrdersPanel.svelte, PortfolioPanel.svelte, HeatmapPanel.svelte, LiveCombos.svelte, ComboWorking.svelte).
// The planned trailing stop's notice, player-built OCO links, the portfolio with its risk limits and
// "Hedge delta", the P&L heatmaps, rolling and adjusting a live combo, and the working limit legs of a
// combo. Every number comes from the engine (the holdings what-if, the dry run); nothing here is sent
// without the player's click and, where the web asks, a confirmation.
#include <algorithm>
#include <cmath>

#include "viewmodel/format.hpp"
#include "viewmodel/game/game_viewmodel.hpp"

namespace tradelab::desktop::game {

namespace {

// Bars to the last expiry of the given series; the perpetual alone: one day of bars (at most 24).
int heatmap_bars(const GameState& g, const std::vector<int>& series) {
  int last = -1;
  for (const auto& q : g.options.chain) {
    if (std::find(series.begin(), series.end(), q.id) != series.end()) last = std::max(last, q.expiry_step);
  }
  return last >= 0 ? last - g.step : std::min(24, g.total_bars - 1 - g.step);
}

std::string held_name(const Texts& t, const GameState& g, int series, double qty) {
  std::string what = series == 0 ? t.tr("lab.perp") : "OPT#" + std::to_string(series);
  for (const auto& q : g.options.chain) {
    if (q.id == series && series != 0) {
      what = std::string(q.kind == "call" ? "C " : "P ") + fmt::fmt_price(q.strike_text, t.locale()) + " @" + std::to_string(q.expiry_step);
    }
  }
  return std::string(qty >= 0 ? "+" : "\xE2\x88\x92") + fmt::fmt_qty(js_number(std::fabs(qty)), t.locale()) + " " + what;
}

std::string order_name(const Texts& t, const GameState& g, const OrderLeg& o) {
  const double q = fmt::num(o.qty);
  return t.tr(o.buy ? "opt.buy" : "opt.sell") + " " + held_name(t, g, o.series, q).substr(1) +
         (o.premium ? " @ " + fmt::fmt_price(*o.premium, t.locale()) : "");
}

}  // namespace

void GameViewModel::refresh_wave1() {
  refresh_portfolio();
  refresh_builder_heatmap();
  refresh_live();
}

// ---- the planned trailing stop (D10) ----

const PlanEntry* GameViewModel::trail_plan() const {
  if (!state_ || !state_->active()) return nullptr;
  const auto it = std::find_if(journal_.rbegin(), journal_.rend(), [](const PlanEntry& e) { return e.instrument.empty(); });
  if (it == journal_.rend() || !it->trail) return nullptr;
  const double pos = state_->position.qty;
  const bool exit_buy = pos < 0;
  const bool trailing = std::any_of(state_->orders.begin(), state_->orders.end(), [&](const Order& o) {
    return o.series == 0 && o.type == "trailing_stop" && (o.side == "buy") == exit_buy;
  });
  const bool handled = std::find(trail_handled_.begin(), trail_handled_.end(), it->step) != trail_handled_.end();
  const bool show = pos != 0 && (pos > 0) == it->buy && it->step < state_->step && !trailing && !handled;
  return show ? &*it : nullptr;
}

std::optional<GameViewModel::TrailNotice> GameViewModel::trail_notice() const {
  const PlanEntry* plan = trail_plan();
  if (plan == nullptr) return std::nullopt;
  const Texts t = texts();
  const Locale L = t.locale();
  const GameState& g = *state_;
  const double v = fmt::parse_decimal(plan->trail->value);
  const bool exit_sell = g.position.qty > 0;
  const std::string close = js_number(g.last_price());
  const std::string tick = fmt::to_fixed_text(g.contract.tick, 8);
  std::string trigger;
  if (plan->trail->pct) {
    if (const auto rate = trail_rate_from_percent(v)) trigger = trail_trigger(exit_sell, close, {}, *rate, tick);
  } else if (const std::string dist = fmt::price_to_tick(v, g.contract.tick); !dist.empty()) {
    trigger = trail_trigger(exit_sell, close, dist, {}, tick);
  }
  const std::string rule = plan->trail->pct ? fmt::fmt_pct(v / 100, L) : fmt::fmt_price(js_number(v), L);
  return TrailNotice{t.tr("trail.notice", {{"rule", rule}, {"price", trigger.empty() ? "\xE2\x80\x94" : fmt::fmt_price(trigger, L)}}),
                     t.tr("trail.place"), t.tr("trail.dismiss")};
}

void GameViewModel::trail_notice_place() {
  const PlanEntry* plan = trail_plan();
  if (plan == nullptr) return;
  trail_handled_.push_back(plan->step);
  ticket.fill_planned_trail(*plan->trail);
  tab_ = Tab::Perp;
  set_notice({texts().tr("trail.filled"), Tone::Ok});
  touch();
}

void GameViewModel::trail_notice_dismiss() {
  if (const PlanEntry* plan = trail_plan()) trail_handled_.push_back(plan->step);
  touch();
}

// ---- OCO links (D7) ----

void GameViewModel::toggle_link_pick(int order_id) {
  const auto it = std::find(link_picks_.begin(), link_picks_.end(), order_id);
  if (it != link_picks_.end()) {
    link_picks_.erase(it);
  } else {
    link_picks_.push_back(order_id);
    if (link_picks_.size() > 2) link_picks_.erase(link_picks_.begin());
  }
  touch();
}

bool GameViewModel::link_picked(int order_id) const {
  return std::find(link_picks_.begin(), link_picks_.end(), order_id) != link_picks_.end();
}

namespace {

const Order* order_of(const GameState& g, int id) {
  const auto it = std::find_if(g.orders.begin(), g.orders.end(), [id](const Order& o) { return o.id == id; });
  return it == g.orders.end() ? nullptr : &*it;
}

}  // namespace

GameViewModel::LinkView GameViewModel::link_view() const {
  LinkView v;
  if (!state_ || state_->orders.size() < 2) return v;
  const Texts t = texts();
  v.shown = true;
  v.button = "\xE2\x9B\x93 " + t.tr("orders.link");
  std::vector<const Order*> pair;
  for (const int id : link_picks_) {
    if (const Order* o = order_of(*state_, id)) pair.push_back(o);
  }
  const std::string problem = pair.size() == 2 ? link_problem(*pair[0], *pair[1]) : std::string();
  v.hint = "OCO: " + (problem.empty() ? t.tr("orders.linkHint") : t.tr("orders.linkProblem." + problem));
  v.can_link = pair.size() == 2 && problem.empty() && !busy() && state_->active();
  return v;
}

void GameViewModel::link_oco() {
  if (!link_view().can_link) return;
  confirm_ = ConfirmKind::LinkOco;
  modal_ = Modal::Confirm;
  touch();
}

GameViewModel::ConfirmView GameViewModel::confirm_view() const {
  const Texts t = texts();
  const Locale L = t.locale();
  ConfirmView v{{}, {}, {}, t.tr("orders.linkNo"), false};
  if (confirm_ == ConfirmKind::LinkOco && link_picks_.size() == 2) {
    v.title = t.tr("orders.linkTitle", {{"a", std::to_string(link_picks_[0])}, {"b", std::to_string(link_picks_[1])}});
    v.text = t.tr("orders.linkBody");
    v.yes = t.tr("orders.linkYes");
  } else if (confirm_ == ConfirmKind::Hedge && portfolio_ && portfolio_->risk) {
    const auto h = hedge_now();
    v.title = t.tr("portfolio.hedgeTitle");
    v.yes = t.tr("portfolio.hedgeYes");
    if (h) {
      v.text = t.tr("portfolio.hedgeConfirm", {{"side", t.tr(h->buy ? "side.buy" : "side.sell")},
                                                {"qty", fmt::fmt_qty(h->qty, L)},
                                                {"after", fmt::fmt_num(delta_after_hedge(*state_, *portfolio_->risk, *h), L, 4)}});
    }
  } else if (confirm_ == ConfirmKind::Uncovered) {
    v.title = t.tr("adjust.uncoveredTitle");
    v.text = t.tr("adjust.uncovered", {{"leg", live_uncovered().value_or("")}});
    v.yes = t.tr("adjust.uncoveredYes");
    v.danger = true;
  }
  return v;
}

void GameViewModel::confirm_answer(bool yes) {
  const ConfirmKind kind = confirm_;
  confirm_ = ConfirmKind::None;
  modal_ = kind == ConfirmKind::Hedge && !yes ? Modal::Portfolio : Modal::None;
  if (yes && kind == ConfirmKind::LinkOco && link_picks_.size() == 2) {
    const int a = link_picks_[0];
    const int b = link_picks_[1];
    run({{GameCommand::link_oco(a, b), false, [a, b](GameViewModel& vm, const CommandResult& r) {
            if (!r.accepted) return vm.notify(r.reason);
            vm.link_picks_.clear();
            vm.set_notice({vm.texts().tr("orders.linkedOk", {{"a", std::to_string(a)}, {"b", std::to_string(b)}}), Tone::Ok});
          }}});
  }
  if (yes && kind == ConfirmKind::Hedge) send_hedge();
  if (yes && kind == ConfirmKind::Uncovered) send_live();
  touch();
}

// ---- portfolio and hedge (D9) ----

void GameViewModel::refresh_portfolio() {
  if (!state_ || replay_) return;
  std::vector<int> series;
  for (const auto& p : state_->options.positions) series.push_back(p.series);
  const std::string request = heatmap_request({}, state_->last_price(), state_->contract.tick, heatmap_bars(*state_, series), true);
  const auto tk = portfolio_ticket_.next(request + "|" + std::to_string(state_->step) + "|" + std::to_string(state_->events.size()));
  if (!tk) return;
  const int t = *tk;
  engine_.analyze(AnalysisKind::RunWhatIf, request, guarded([t](GameViewModel& vm, const std::string& text) {
                    if (!vm.portfolio_ticket_.current(t)) return;
                    const auto a = whatif_from_json(text);
                    vm.portfolio_ = a.value;
                    vm.portfolio_heatmap_ = a.ok() ? a.value->heatmap : std::nullopt;
                  }));
}

PortfolioView GameViewModel::portfolio_view() const {
  if (!state_) return {};
  return game::portfolio_view(texts(), *state_, portfolio_, busy());
}

RailPortfolio GameViewModel::rail_portfolio_view() const { return game::rail_portfolio(texts(), portfolio_); }

void GameViewModel::open_portfolio() {
  if (!state_) return;
  modal_ = Modal::Portfolio;
  touch();
}

std::optional<OrderLeg> GameViewModel::hedge_now() const {
  if (!state_ || !portfolio_) return std::nullopt;
  return hedge_ticket(*state_, portfolio_->risk);
}

// The hedge asks first (Modal::Confirm), then goes out as a normal perpetual market ticket: through
// the plan dialog when it adds exposure.
void GameViewModel::hedge_delta() {
  if (!hedge_now() || !active() || busy()) return;
  confirm_ = ConfirmKind::Hedge;
  modal_ = Modal::Confirm;
  touch();
}

void GameViewModel::send_hedge() {
  const auto h = hedge_now();
  if (!h || !active()) return;
  OrderDraft d;
  d.side = h->buy ? Side::Buy : Side::Sell;
  d.type = OrderType::Market;
  d.qty = h->qty;
  d.leverage = state_->leverage;
  const double pos = state_->position.qty;
  const double qty = fmt::num(h->qty);
  d.increases = pos == 0 || (pos > 0) == h->buy || qty > std::fabs(pos);
  modal_ = Modal::None;
  if (!d.increases) return send_order(d);
  pending_order_ = d;
  pending_option_.reset();
  pending_combo_.reset();
  plan.open(h->buy, d.qty, d.leverage, std::nullopt, {}, {}, std::nullopt);
  modal_ = Modal::Plan;
  touch();
}

// ---- heatmap (D4) ----

void GameViewModel::refresh_builder_heatmap() {
  if (!state_ || tab_ != Tab::Builder || !builder.heatmap_open) return;
  const auto legs = builder.lab_legs(*state_);
  if (legs.empty()) {
    builder_heatmap_.reset();
    return;
  }
  std::vector<int> series;
  for (const auto& l : legs) series.push_back(l.series);
  const std::string request = heatmap_request(legs, state_->last_price(), state_->contract.tick, heatmap_bars(*state_, series));
  const auto tk = heatmap_ticket_.next(request + "|" + std::to_string(state_->step));
  if (!tk) return;
  const int t = *tk;
  engine_.analyze(AnalysisKind::RunWhatIf, request, guarded([t](GameViewModel& vm, const std::string& text) {
                    if (!vm.heatmap_ticket_.current(t)) return;
                    const auto a = whatif_from_json(text);
                    vm.builder_heatmap_ = a.ok() ? a.value->heatmap : std::nullopt;
                  }));
}

// ---- live combos: roll and adjust (D3) ----

std::vector<GameViewModel::LiveCombo> GameViewModel::live_combos() const {
  std::vector<LiveCombo> out;
  if (!state_) return out;
  const Texts t = texts();
  for (const auto& st : combo_statuses()) {
    const auto held = combo_holdings(st);
    if (std::none_of(held.begin(), held.end(), [](const HeldLeg& h) { return h.series != 0; })) continue;
    std::string text = t.tr("strategy." + st.record.strategy) + " \xC2\xB7";
    for (const auto& h : held) text += " " + held_name(t, *state_, h.series, h.qty);
    out.push_back({st.record.id, text});
  }
  return out;
}

std::optional<ComboStatus> GameViewModel::live_status() const {
  if (!live_ || !state_) return std::nullopt;
  for (const auto& st : combo_statuses()) {
    if (st.record.id == live_->id) return st;
  }
  return std::nullopt;
}

namespace {

std::vector<HeldLeg> held_options(const ComboStatus& st) {
  std::vector<HeldLeg> out;
  for (const auto& h : combo_holdings(st)) {
    if (h.series != 0) out.push_back(h);
  }
  return out;
}

int near_expiry(const GameState& g, const std::vector<HeldLeg>& options) {
  int from = 0;
  for (const auto& h : options) {
    for (const auto& q : g.options.chain) {
      if (q.id == h.series && (from == 0 || q.expiry_step < from)) from = q.expiry_step;
    }
  }
  return from;
}

}  // namespace

void GameViewModel::open_roll(const std::string& id) {
  live_ = Live{id, true, 0, false, false, {}, true, std::nullopt, std::nullopt, std::nullopt};
  if (const auto st = live_status()) {
    const auto targets = roll_targets(state_->options.chain, near_expiry(*state_, held_options(*st)));
    live_->to = targets.empty() ? 0 : targets.front();
  }
  refresh_live();
  touch();
}

void GameViewModel::open_adjust(const std::string& id) {
  live_ = Live{id, false, 0, false, false, {}, true, std::nullopt, std::nullopt, std::nullopt};
  refresh_live();
  touch();
}

void GameViewModel::live_close() {
  live_.reset();
  touch();
}

void GameViewModel::set_roll_target(int index) {
  const auto st = live_status();
  if (!st) return;
  const auto targets = roll_targets(state_->options.chain, near_expiry(*state_, held_options(*st)));
  if (index >= 0 && index < static_cast<int>(targets.size())) live_->to = targets[static_cast<std::size_t>(index)];
  refresh_live();
  touch();
}

void GameViewModel::set_roll_recenter(bool on) {
  if (!live_) return;
  live_->recenter = on;
  refresh_live();
  touch();
}

void GameViewModel::set_roll_limit_ioc(bool on) {
  if (!live_) return;
  live_->limit_ioc = on;
  refresh_live();
  touch();
}

void GameViewModel::adjust_close_leg(std::size_t held_index) {
  const auto st = live_status();
  if (!st) return;
  const auto held = combo_holdings(*st);
  if (held_index >= held.size()) return;
  const HeldLeg& h = held[held_index];
  const double lot = h.series == 0 ? state_->contract.lot : state_->options.lot;
  live_->adjustment = {OrderLeg{h.series, h.qty < 0, fmt::to_fixed_text(std::fabs(h.qty), fmt::step_decimals(lot)), std::nullopt}};
  live_->adjust_close = true;
  refresh_live();
  touch();
}

void GameViewModel::adjust_add_hedge() {
  if (!live_ || !state_) return;
  const double delta = live_->before && live_->before->risk ? fmt::num(live_->before->risk->delta_units) : 0;
  const auto h = hedge_order(*state_, delta);
  live_->adjustment = h ? std::vector<OrderLeg>{*h} : std::vector<OrderLeg>{};
  live_->adjust_close = false;
  refresh_live();
  touch();
}

std::vector<OrderLeg> GameViewModel::live_orders() const {
  const auto st = live_status();
  if (!st) return {};
  if (!live_->roll) return live_->adjustment;
  const auto plan = roll_orders(*state_, held_options(*st), live_->to, live_->recenter, state_->last_price());
  if (!plan) return {};
  if (!live_->limit_ioc) return plan->orders;
  std::vector<OrderLeg> out = plan->orders;
  for (auto& o : out) {
    for (const auto& q : state_->options.chain) {
      if (q.id == o.series) o.premium = premium_text(default_limit(q.mid, o.buy, state_->options.premium_tick), state_->options.premium_tick);
    }
  }
  return out;
}

std::optional<ComboTif> GameViewModel::live_tif() const {
  if (live_ && live_->roll && live_->limit_ioc) return ComboTif{"ioc", 0};
  return std::nullopt;
}

// The engine's dry run of the orders and the what-if of the holdings before and after them.
void GameViewModel::refresh_live() {
  if (!live_ || !state_) return;
  const auto orders = live_orders();
  const std::string before = R"({"holdings":true,"legs":[]})";
  const std::string after = R"({"holdings":true,"legs":)" + legs_json(whatif_legs(orders, std::nullopt)) + "}";
  const std::string preview = combo_request(combo_legs(orders, live_tif()));
  const auto tk = live_ticket_.next(preview + after + "|" + std::to_string(state_->step) + "|" + std::to_string(state_->events.size()));
  if (!tk) return;
  const int t = *tk;
  engine_.analyze(AnalysisKind::RunWhatIf, before, guarded([t](GameViewModel& vm, const std::string& text) {
                    if (vm.live_ticket_.current(t) && vm.live_) vm.live_->before = whatif_from_json(text).value;
                  }));
  if (orders.empty()) return;
  engine_.analyze(AnalysisKind::ComboPreview, preview, guarded([t](GameViewModel& vm, const std::string& text) {
                    if (vm.live_ticket_.current(t) && vm.live_) vm.live_->preview = preview_from_json(text);
                  }));
  engine_.analyze(AnalysisKind::RunWhatIf, after, guarded([t](GameViewModel& vm, const std::string& text) {
                    if (vm.live_ticket_.current(t) && vm.live_) vm.live_->after = whatif_from_json(text).value;
                  }));
}

// Closing a leg (or a roll) that leaves a short with no maximum loss names that short; nullopt otherwise.
std::optional<std::string> GameViewModel::live_uncovered() const {
  if (!live_ || !live_->before || !live_->after) return std::nullopt;
  const bool removing = live_->roll || live_->adjust_close;
  const auto orders = live_orders();
  if (!removing || orders.empty()) return std::nullopt;
  const WhatIfExpiry* was = analytics_of(*live_->before);
  const WhatIfExpiry* now = analytics_of(*live_->after);
  if (was == nullptr || !was->max_loss || now == nullptr || now->max_loss) return std::nullopt;
  const Texts t = texts();
  if (live_->roll) {
    const auto sale = std::find_if(orders.rbegin(), orders.rend(), [](const OrderLeg& o) { return !o.buy; });
    return sale == orders.rend() ? std::string() : order_name(t, *state_, *sale);
  }
  const auto st = live_status();
  for (const auto& h : st ? held_options(*st) : std::vector<HeldLeg>{}) {
    const bool closing = std::any_of(orders.begin(), orders.end(), [&](const OrderLeg& o) { return o.series == h.series; });
    if (h.qty < 0 && !closing) return held_name(t, *state_, h.series, h.qty);
  }
  return std::string();
}

namespace {

void live_preview_lines(const Texts& t, const GameState& g, const std::vector<OrderLeg>& orders, const ComboPreview& p,
                        GameViewModel::LiveView& v) {
  for (std::size_t i = 0; i < orders.size(); ++i) {
    const bool ok = i < p.legs.size() && p.legs[i].accepted;
    v.orders.push_back({std::to_string(i + 1) + ". " + order_name(t, g, orders[i]), ok ? Tone::Plain : Tone::Down});
  }
}

}  // namespace

GameViewModel::LiveView GameViewModel::live_view() const {
  LiveView v;
  const auto st = live_status();
  if (!st) return v;
  const Texts t = texts();
  const Locale L = t.locale();
  const std::string name = t.tr("strategy." + st->record.strategy);
  v.shown = true;
  v.roll = live_->roll;
  v.title = t.tr(live_->roll ? "roll.title" : "adjust.title", {{"name", name}});
  if (live_->roll) {
    const auto targets = roll_targets(state_->options.chain, near_expiry(*state_, held_options(*st)));
    if (targets.empty()) v.none = t.tr("roll.none");
    for (std::size_t i = 0; i < targets.size(); ++i) {
      v.targets.push_back(t.tr("builder.expiryAt", {{"step", std::to_string(targets[i])}, {"n", std::to_string(targets[i] - state_->step)}}));
      if (targets[i] == live_->to) v.target_index = static_cast<int>(i);
    }
    v.recenter = t.tr("roll.recenter");
    v.limit_ioc = t.tr("roll.limitIoc");
    v.recenter_on = live_->recenter;
    v.limit_on = live_->limit_ioc;
  } else {
    for (const auto& h : combo_holdings(*st)) v.held.push_back(held_name(t, *state_, h.series, h.qty));
    v.close_leg = t.tr("adjust.closeLeg");
    v.add_hedge = "+ " + t.tr("adjust.addHedge") + ": " + t.tr("adjust.hedgePerp");
    if (!live_->adjust_close && live_->adjustment.empty()) v.hedge_none = t.tr("portfolio.hedgeNone");
  }
  const auto orders = live_orders();
  v.send = t.tr(live_->roll ? "roll.send" : "adjust.send");
  if (orders.empty() || !live_->preview || !live_->preview->ok()) return v;
  const ComboPreview& p = *live_->preview->value;
  live_preview_lines(t, *state_, orders, p, v);
  v.totals.emplace_back(t.tr(live_->roll ? "roll.net" : "builder.credit"), fmt::fmt_money(p.net_premium, L, true));
  if (live_->after) {
    const WhatIfExpiry* a = analytics_of(*live_->after);
    v.totals.emplace_back(t.tr("adjust.after") + ": " + t.tr("lab.maxLoss"),
                          a != nullptr && a->max_loss ? fmt::fmt_money(*a->max_loss, L) : t.tr("lab.unbounded"));
  }
  v.can_send = p.accepted && !busy() && state_->active();
  return v;
}

void GameViewModel::live_send() {
  if (!live_view().can_send) return;
  if (live_uncovered()) {
    confirm_ = ConfirmKind::Uncovered;
    modal_ = Modal::Confirm;
    return touch();
  }
  send_live();
}

// One submission of ordinary orders under the combo's id, tagged roll/adjust (buys, perpetual, sells:
// roll_orders is already in that order and an adjustment is one leg), with the plan of the change.
void GameViewModel::send_live() {
  const auto st = live_status();
  const auto orders = live_orders();
  if (!st || orders.empty() || !active()) return;
  const Texts t = texts();
  const bool roll = live_->roll;
  ComboDraft d;
  d.orders = orders;
  d.combo = st->record.id;
  d.strategy = st->record.strategy;
  (roll ? d.roll : d.adjust) = next_submission_number(st->record, roll ? SubmissionKind::Roll : SubmissionKind::Adjust);
  d.label = t.tr(roll ? "roll.title" : "adjust.title", {{"name", t.tr("strategy." + st->record.strategy)}});
  const WhatIfExpiry* a = live_->after ? analytics_of(*live_->after) : nullptr;
  if (a != nullptr && a->max_loss) d.risk = std::max(0.0, fmt::num(*a->max_loss));
  else if (live_->preview && live_->preview->ok()) d.risk = fmt::num(live_->preview->value->required);
  d.unbounded = a != nullptr && !a->max_loss;
  d.tif = live_tif();
  live_.reset();
  pending_combo_ = d;
  pending_order_.reset();
  pending_option_.reset();
  plan.open(d.orders.front().buy, combo_contracts(d.orders), 1, d.risk, {}, {}, d.label);
  plan.risk_unbounded = d.unbounded;
  modal_ = Modal::Plan;
  touch();
}

// ---- working limit legs (D2) ----

std::optional<GameViewModel::WorkingCombo> GameViewModel::working_combo() const {
  if (!state_) return std::nullopt;
  const auto statuses = combo_statuses();
  for (auto it = statuses.rbegin(); it != statuses.rend(); ++it) {
    const ComboStatus& st = *it;
    const int placed = st.record.submissions.empty() ? st.record.step : st.record.submissions.back().step;
    const bool limits = std::any_of(st.legs.begin(), st.legs.end(), [](const LegStatus& l) { return l.leg.premium.has_value(); });
    if (st.state != ComboState::Working || working_legs(st).empty() || !limits || state_->step <= placed) continue;
    const Texts t = texts();
    const auto done = std::count_if(st.legs.begin(), st.legs.end(), [](const LegStatus& l) { return l.state == LegState::Filled; });
    return WorkingCombo{t.tr("combo.workingTitle", {{"name", t.tr("strategy." + st.record.strategy)}}),
                        t.tr("combo.working", {{"done", std::to_string(done)}, {"total", std::to_string(st.legs.size())}}),
                        t.tr("combo.cancelWorking")};
  }
  return std::nullopt;
}

void GameViewModel::cancel_working_legs() {
  if (!working_combo() || !active()) return;
  const auto statuses = combo_statuses();
  for (auto it = statuses.rbegin(); it != statuses.rend(); ++it) {
    if (it->state != ComboState::Working || working_legs(*it).empty()) continue;
    std::vector<Step> steps;
    for (const int id : working_legs(*it)) steps.push_back({GameCommand::cancel(id).tagged(it->record.id, it->record.strategy), false, {}});
    return run(std::move(steps));
  }
}

}  // namespace tradelab::desktop::game
