// GameViewModel: the strategy builder's combos and the terminal's what-if lab (web Terminal.svelte's
// combo half, combos.ts submitCombo, ComboNotice.svelte; add-academy-and-strategies D3, D4). A combo
// is sent as ordinary market orders tagged with its id, one at a time: when a leg is refused, the legs
// already accepted are cancelled before anything else runs, so nothing of it executes. Legs cancelled
// at the open leave the combo incomplete: a notice names the open legs and their risk (the engine's
// what-if of the legs held) and offers to close them after a confirmation, never on its own.
#include <algorithm>

#include "viewmodel/format.hpp"
#include "viewmodel/game/game_viewmodel.hpp"

namespace tradelab::desktop::game {

namespace {

GameCommand leg_command(const OrderLeg& o) {
  if (o.series == 0) return GameCommand::order(OrderType::Market, o.buy, o.qty, {}, false);
  return GameCommand::option_order(o.series, o.buy, false, o.qty, {});
}

// The leg as a market order that closes it (the perpetual reduce-only).
GameCommand closing_command(const OrderLeg& o) {
  if (o.series == 0) return GameCommand::order(OrderType::Market, !o.buy, o.qty, {}, true);
  return GameCommand::option_order(o.series, !o.buy, false, o.qty, {});
}

std::string join(const std::vector<std::string>& parts) {
  std::string out;
  for (const auto& p : parts) out += (out.empty() ? "" : ", ") + p;
  return out;
}

}  // namespace

EngineCommands::AnalysisReply GameViewModel::guarded(std::function<void(GameViewModel&, const std::string&)> fn) {
  return [alive = std::weak_ptr<bool>(alive_), self = this, fn = std::move(fn)](const std::string& text) {
    if (alive.expired()) return;
    fn(*self, text);
    self->touch();
  };
}

void GameViewModel::reset_combos() {
  builder = BuilderViewModel{};
  lab = LabViewModel{};
  pending_combo_.reset();
  dismissed_.clear();
  risk_combo_.clear();
  combo_risk_.reset();
  risk_ticket_.forget();
  log_.reset();
}

// ---- builder ----

void GameViewModel::builder_changed() {
  if (state_) builder.sync(*state_);
  refresh_analysis();
  touch();
}

void GameViewModel::refresh_analysis() {
  if (!state_ || tab_ != Tab::Builder) return;
  builder.sync(*state_);
  const auto r = builder.requests(*state_);
  if (!r) return builder.clear_analysis();
  const auto tk = builder.ticket.next(r->key);
  if (!tk) return;
  builder.expect(2);
  const int t = *tk;
  engine_.analyze(AnalysisKind::RunWhatIf, r->whatif, guarded([t](GameViewModel& vm, const std::string& text) {
                    if (vm.builder.ticket.current(t)) vm.builder.answer_whatif(text);
                  }));
  engine_.analyze(AnalysisKind::ComboPreview, r->preview, guarded([t](GameViewModel& vm, const std::string& text) {
                    if (vm.builder.ticket.current(t)) vm.builder.answer_preview(text);
                  }));
}

BuilderView GameViewModel::builder_view() const {
  if (!state_) return {};
  return builder.view(texts(), *state_, busy());
}

void GameViewModel::builder_submit() {
  if (!active()) return;
  const auto d = builder.draft(texts(), *state_, combos(), busy());
  if (!d) return;
  pending_combo_ = d;
  pending_order_.reset();
  pending_option_.reset();
  // The engine pairs a plan with its order by step, instrument and side: the dialog plans the combo.
  plan.open(d->orders.front().buy, combo_contracts(d->orders), 1, d->risk, {}, {}, d->label);
  plan.risk_unbounded = d->unbounded;
  modal_ = Modal::Plan;
  touch();
}

// One plan per leg for the engine (the risk split evenly), the journal's entry once; then the legs.
void GameViewModel::plan_combo(const PlanEntry& e) {
  const ComboDraft d = *pending_combo_;
  pending_combo_.reset();
  const auto n = static_cast<double>(d.orders.size());
  for (const auto& o : d.orders) {
    PlanEntry leg = e;
    leg.buy = o.buy;
    if (e.planned_risk) leg.planned_risk = *e.planned_risk / n;
    leg.instrument = o.series == 0 ? std::string() : d.label;
    chain_.push_back(Step{plan.command(leg), false, {}});
  }
  place_combo_leg(std::make_shared<ComboRun>(ComboRun{d, {}}), 0);
}

void GameViewModel::place_combo_leg(const std::shared_ptr<ComboRun>& combo, std::size_t i) {
  const OrderLeg& o = combo->draft.orders[i];
  Step step{leg_command(o).tagged(combo->draft.combo, combo->draft.strategy), false,
            [combo, i](GameViewModel& vm, const CommandResult& r) {
              if (!r.accepted) return vm.withdraw_combo(*combo, i, r.reason);
              combo->accepted.push_back(r.order_id);
              if (i + 1 < combo->draft.orders.size()) return vm.place_combo_leg(combo, i + 1);
              vm.set_notice({vm.texts().tr("combo.sent", {{"name", combo->draft.label}}), Tone::Ok});
            }};
  if (i == 0) return run({std::move(step)});
  chain_.push_front(std::move(step));  // right after the leg before it: nothing runs in between
}

void GameViewModel::withdraw_combo(const ComboRun& run, std::size_t refused, const std::string& reason) {
  for (auto it = run.accepted.rbegin(); it != run.accepted.rend(); ++it) {
    chain_.push_front(Step{GameCommand::cancel(*it).tagged(run.draft.combo, run.draft.strategy), false, {}});
  }
  const Texts t = texts();
  set_notice({"\xE2\x9C\x96 " + t.tr("combo.refused", {{"n", std::to_string(refused + 1)}, {"reason", rejection_text(t, reason)}}),
              Tone::Error});
}

// ---- combos of the run ----

std::vector<ComboRecord> GameViewModel::combos() const {
  if (!log_ || !state_) return {};
  return combos_from_log(*log_, state_->warmup_bars);
}

std::vector<ComboStatus> GameViewModel::combo_statuses() const {
  std::vector<ComboStatus> out;
  if (!state_) return out;
  for (const auto& r : combos()) out.push_back(combo_status(state_->events, r));
  return out;
}

namespace {

std::optional<ComboStatus> latest_incomplete(const std::vector<ComboStatus>& statuses, const std::vector<std::string>& dismissed) {
  for (auto it = statuses.rbegin(); it != statuses.rend(); ++it) {
    if (it->state != ComboState::Incomplete) continue;
    if (std::find(dismissed.begin(), dismissed.end(), it->record.id) == dismissed.end()) return *it;
  }
  return std::nullopt;
}

std::string leg_name(const Texts& t, const GameState& g, const OrderLeg& leg) {
  std::string what = "OPT#" + std::to_string(leg.series);
  if (leg.series == 0) what = t.tr("lab.perp");
  for (const auto& q : g.options.chain) {
    if (q.id == leg.series && leg.series != 0) what = std::string(q.kind == "call" ? "C " : "P ") + fmt::fmt_price(q.strike_text, t.locale());
  }
  return t.tr(leg.buy ? "combo.long" : "combo.short") + " " + what;
}

std::vector<std::string> open_names(const Texts& t, const GameState& g, const ComboStatus& st) {
  std::vector<std::string> out;
  for (const auto& l : open_legs(st)) out.push_back(leg_name(t, g, l.leg));
  return out;
}

}  // namespace

std::optional<GameViewModel::ComboNotice> GameViewModel::combo_notice() const {
  if (!state_) return std::nullopt;
  const auto st = latest_incomplete(combo_statuses(), dismissed_);
  if (!st) return std::nullopt;
  const Texts t = texts();
  ComboNotice n;
  n.id = st->record.id;
  n.title = "\xE2\x9A\xA0 " + t.tr("combo.incompleteTitle", {{"name", t.tr("strategy." + st->record.strategy)}});
  n.open_legs = t.tr("combo.openLegs") + ": " + join(open_names(t, *state_, *st));
  std::vector<std::string> cancelled;
  for (const auto& l : st->legs) {
    if (l.state == LegState::Cancelled) cancelled.push_back(leg_name(t, *state_, l.leg) + " (" + t.reason(l.reason.empty() ? "none" : l.reason) + ")");
  }
  n.cancelled_legs = t.tr("combo.cancelledLegs") + ": " + join(cancelled);
  if (combo_risk_ && risk_combo_ == n.id && combo_risk_->expiry) {
    const auto above = unbounded_above(*combo_risk_);
    n.risk = above ? t.tr("combo.riskUnbounded", {{"price", fmt::fmt_price(*above, t.locale())}})
                   : t.tr("combo.riskBounded", {{"loss", fmt::fmt_money(combo_risk_->expiry->max_loss.value_or("0"), t.locale())}});
  }
  n.legging = t.tr("combo.legging");
  return n;
}

// The risk of the legs actually held, from the engine (the legs at their fill prices).
void GameViewModel::request_combo_risk() {
  if (!state_) return;
  const auto st = latest_incomplete(combo_statuses(), dismissed_);
  if (!st) return;
  std::vector<AnalysisLeg> legs;
  for (const auto& l : open_legs(*st)) legs.push_back({l.leg.series, l.leg.buy, l.leg.qty, l.price});
  const std::string id = st->record.id;
  const auto tk = risk_ticket_.next(id + run_request(legs));
  if (!tk) return;
  const int t = *tk;
  engine_.analyze(AnalysisKind::RunWhatIf, run_request(legs), guarded([t, id](GameViewModel& vm, const std::string& text) {
                    if (!vm.risk_ticket_.current(t)) return;
                    vm.risk_combo_ = id;
                    vm.combo_risk_ = whatif_from_json(text).value;
                  }));
}

void GameViewModel::combo_keep() {
  if (const auto n = combo_notice()) dismissed_.push_back(n->id);
  touch();
}

void GameViewModel::combo_close() {
  if (!combo_notice() || !active()) return;
  modal_ = Modal::ConfirmCombo;
  touch();
}

std::string GameViewModel::combo_close_text() const {
  const auto st = state_ ? latest_incomplete(combo_statuses(), dismissed_) : std::nullopt;
  if (!st) return {};
  return texts().tr("combo.closeConfirm", {{"legs", join(open_names(texts(), *state_, *st))}});
}

void GameViewModel::combo_close_confirm(bool yes) {
  if (modal_ == Modal::ConfirmCombo) modal_ = Modal::None;
  touch();
  const auto st = state_ ? latest_incomplete(combo_statuses(), dismissed_) : std::nullopt;
  if (!yes || !st || !active()) return;
  std::vector<Step> steps;
  for (const auto& l : open_legs(*st)) {
    steps.push_back({closing_command(l.leg), false, [](GameViewModel& vm, const CommandResult& r) {
                       if (!r.accepted) vm.notify(r.reason);
                     }});
  }
  dismissed_.push_back(st->record.id);
  set_notice({texts().tr("combo.closing"), Tone::Ok});
  run(std::move(steps));
}

std::string GameViewModel::combo_note(const std::string& id) const {
  for (const auto& st : combo_statuses()) {
    if (st.record.id != id) continue;
    const Texts t = texts();
    const std::string premium = fmt::fmt_money(net_premium(st, state_->options.multiplier), t.locale(), true);
    return t.tr("combo.state." + std::string(combo_state_code(st.state))) + " \xC2\xB7 " +
           t.tr("combo.premium", {{"amount", premium}});
  }
  return {};
}

// ---- terminal lab ----

namespace {

int bars_to_expiry(const GameState& g, const std::vector<AnalysisLeg>& legs) {
  std::vector<int> steps;
  for (const auto& l : legs) {
    for (const auto& q : g.options.chain) {
      if (q.id == l.series && l.series != 0) steps.push_back(q.expiry_step);
    }
  }
  return run_max_bars(steps, g.step);
}

}  // namespace

void GameViewModel::open_lab_positions() {
  if (!state_) return;
  const auto legs = position_legs(*state_);
  if (legs.empty()) return;
  lab.open_terminal(legs, state_->last_price(), bars_to_expiry(*state_, legs));
  modal_ = Modal::Lab;
  request_lab();
  touch();
}

void GameViewModel::open_lab_builder() {
  if (!state_) return;
  const auto legs = builder.lab_legs(*state_);
  if (legs.empty()) return;
  lab.open_terminal(legs, state_->last_price(), bars_to_expiry(*state_, legs));
  modal_ = Modal::Lab;
  request_lab();
  touch();
}

void GameViewModel::lab_changed() {
  request_lab();
  touch();
}

void GameViewModel::request_lab() {
  const std::string request = lab.request();
  const auto tk = lab.ticket.next(request);
  if (!tk) return;
  const int t = *tk;
  engine_.analyze(lab.kind(), request, guarded([t](GameViewModel& vm, const std::string& text) {
                    if (vm.lab.ticket.current(t)) vm.lab.answer(text);
                  }));
}

}  // namespace tradelab::desktop::game
