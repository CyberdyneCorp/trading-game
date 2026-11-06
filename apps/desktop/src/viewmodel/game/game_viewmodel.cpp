#include "viewmodel/game/game_viewmodel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

namespace {

std::string side_word(const Texts& t, bool buy) { return t.tr(buy ? "side.buy" : "side.sell"); }

std::string two_decimals(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.2f", v);
  return buf;
}

}  // namespace

GameViewModel::GameViewModel(const Catalog& catalog, EngineCommands& engine, PrefsFile* prefs,
                             std::vector<std::string> built_in)
    : catalog_(&catalog), engine_(engine), prefs_file_(prefs), built_in_(std::move(built_in)) {
  if (prefs_file_ != nullptr) prefs_ = prefs_file_->load();
  keymap_ = Keymap::sanitized(prefs_.keymap);
}

void GameViewModel::attach_prefs(PrefsFile* file) {
  prefs_file_ = file;
  if (file != nullptr) prefs_ = file->load();
  keymap_ = Keymap::sanitized(prefs_.keymap);
  touch();
}

void GameViewModel::set_notice(Line l) {
  notice_ = std::move(l);
  touch();
}

void GameViewModel::set_keymap(const std::map<std::string, std::string>& map) {
  const Keymap next = Keymap::sanitized(map);
  if (next == keymap_) return;
  keymap_ = next;
  save_prefs();
  touch();
}

void GameViewModel::save_prefs() {
  prefs_.keymap = keymap_.to_map();
  if (prefs_file_ != nullptr) prefs_file_->save(prefs_);
}

RunOptions GameViewModel::options_for_new_run() {
  const int finished = distinct_finished(prefs_.finished, built_in_);
  next_options_ = perk_options(unlocked_perks(finished), allowance_for(prefs_));
  return next_options_;
}

std::optional<double> GameViewModel::planned_risk() const {
  if (!state_ || state_->position.qty == 0) return std::nullopt;
  return position_planned_risk(journal_);
}

// ---- snapshots and the command pipeline ----

void GameViewModel::on_snapshot(const Snapshot& s) {
  bool started = false;
  for (const auto& r : s.results) {
    if (r.kind == CommandKind::Start && r.accepted) started = true;
  }
  if (s.game) {
    if (started) {
      seed_ = s.seed;
      reset_run(*s.game);
    }
    state_ = s.game;
    names_.observe(*state_);
    ticket.sync(*state_);
  }
  for (const auto& r : s.results) on_result(r);
  if (state_ && !state_->active() && s.debrief) on_run_end(s);
  touch();
}

void GameViewModel::reset_run(const GameState& g) {
  // A run left behind by starting another is settled with the advice it used and nothing earned.
  if (state_ && !run_settled_ && !relaunching_) {
    settle_run(prefs_, run_id_, state_->advisor.allowance - state_->advisor.left, 0);
    save_prefs();
  }
  const std::string key = g.mission + "#" + std::to_string(seed_);
  if (!relaunching_) {
    run_options_ = next_options_;
    journal_.clear();
    names_.reset();
    ticket = TicketViewModel{};
    option_ticket = OptionTicketViewModel{};
    lessons = LessonDialog{};
    chart = ChartViewModel{};
    autoplay.stop();
    notice_ = {};
    tab_ = Tab::Perp;
    modal_ = Modal::None;
    pick_ = PickTarget::None;
    queued_ = 0;
    actions_ = 0;
    run_id_ = g.mission + "-" + std::to_string(++run_counter_) + "-" + std::to_string(g.advisor.allowance);
  }
  run_key_ = key;
  run_settled_ = false;
  relaunching_ = false;
}

void GameViewModel::on_run_end(const Snapshot& s) {
  autoplay.stop(PauseReason::Finished);
  if (run_settled_) return;
  run_settled_ = true;
  const GameState& g = *state_;
  settle_run(prefs_, run_id_, g.advisor.allowance - g.advisor.left, s.debrief->advisor_points_earned);
  const bool counts = g.status == "finished" && !g.known_scenario;
  if (counts && std::find(prefs_.finished.begin(), prefs_.finished.end(), g.mission) == prefs_.finished.end()) {
    prefs_.finished.push_back(g.mission);
  }
  save_prefs();
}

void GameViewModel::run(std::vector<Step> steps) {
  for (auto& s : steps) chain_.push_back(std::move(s));
  send_next();
  touch();
}

void GameViewModel::send_next() {
  if (in_flight_ != InFlight::None) return;
  if (chain_.empty()) return flush_queue();
  current_ = std::move(chain_.front());
  chain_.pop_front();
  in_flight_ = InFlight::Game;
  if (current_->cmd.kind != GameCommandKind::Lesson) ++actions_;
  engine_.game(current_->cmd);
}

void GameViewModel::flush_queue() {
  if (queued_ <= 0 || busy() || !active() || state_->calibration.due) return;
  const int n = queued_;
  queued_ = 0;
  advance(n);
}

void GameViewModel::on_result(const CommandResult& r) {
  if (r.kind == CommandKind::Advance && in_flight_ == InFlight::Advance) {
    in_flight_ = InFlight::None;
    if (state_) autoplay.after_advance(*state_);
    send_next();
    return;
  }
  if (r.kind == CommandKind::Start && in_flight_ == InFlight::Start) {
    in_flight_ = InFlight::None;
    send_next();
    return;
  }
  if (r.kind != CommandKind::Game || in_flight_ != InFlight::Game) return;
  in_flight_ = InFlight::None;
  Step step = std::move(*current_);
  current_.reset();
  if (step.done) step.done(*this, r);
  if (!r.accepted && step.abort_on_reject) chain_.clear();
  send_next();
}

void GameViewModel::notify(const std::string& reason, const RejectionFacts& facts) {
  set_notice({"\xE2\x9C\x96 " + rejection_text(texts(), reason, facts), Tone::Error});  // ✖
}

void GameViewModel::advance(int steps) {
  if (!active() || state_->calibration.due || steps <= 0) return;
  if (busy()) {
    queued_ += steps;  // e.g. Space pressed while an order is in flight: run it afterwards
    return;
  }
  in_flight_ = InFlight::Advance;
  ++actions_;
  engine_.advance(steps_until_next_question(*state_, steps));
  touch();
}

// ---- perpetual ticket ----

void GameViewModel::select_side(Side s) {
  ticket.side = s;
  touch();
}

void GameViewModel::submit() {
  if (!active()) return;
  const auto d = ticket.draft(*state_, busy(), texts().locale());
  if (!d) return;
  if (!d->increases) return send_order(*d);
  pending_order_ = d;
  pending_option_.reset();
  plan.open(is_buy(d->side), d->qty, d->leverage, d->estimate.planned_risk, d->stop, d->take_profit, std::nullopt);
  modal_ = Modal::Plan;
  touch();
}

void GameViewModel::send_order(const OrderDraft& d) {
  std::vector<Step> steps;
  const GameState before = *state_;
  if (d.leverage != state_->leverage) {
    const int lev = d.leverage;
    steps.push_back({GameCommand::leverage(lev), true, [before, lev](GameViewModel& vm, const CommandResult& r) {
                       if (!r.accepted) vm.notify(r.reason, relever_facts(before, lev));
                     }});
  }
  const bool buy = is_buy(d.side);
  steps.push_back({GameCommand::order(d.type, buy, d.qty, d.price, false, d.stop, d.take_profit), false,
                   [d, buy](GameViewModel& vm, const CommandResult& r) {
                     if (!r.accepted) return vm.notify(r.reason, order_facts(*vm.state_, d.required, d.leverage));
                     const Texts t = vm.texts();
                     vm.set_notice({"#" + std::to_string(r.order_id) + " \xE2\x9C\x93 " + side_word(t, buy) + " " +
                                        fmt::fmt_qty(d.qty, t.locale()),
                                    Tone::Ok});
                   }});
  run(std::move(steps));
}

void GameViewModel::protect() {
  if (!active() || busy()) return;
  if (!ticket.view(texts(), *state_, busy()).can_protect) return;
  std::vector<Step> steps;
  for (auto& cmd : ticket.protect_commands(*state_)) {
    steps.push_back({std::move(cmd), false, [](GameViewModel& vm, const CommandResult& r) {
                       if (!r.accepted) vm.notify(r.reason);
                     }});
  }
  ticket.clear_protection();
  run(std::move(steps));
}

void GameViewModel::close_position() {
  if (!active()) return;
  run({{GameCommand::close(), false, {}}});
}

void GameViewModel::cancel_order(int order_id) {
  if (!active()) return;
  run({{GameCommand::cancel(order_id), false, {}}});
}

void GameViewModel::adjust_margin(int sign) {
  if (!active() || busy()) return;
  const double v = std::fabs(fmt::parse_amount(margin_amount, texts().locale()));
  if (!std::isfinite(v) || v <= 0) return;
  const double delta = sign * v;
  const GameState before = *state_;
  run({{GameCommand::margin(two_decimals(delta)), false, [before, delta](GameViewModel& vm, const CommandResult& r) {
          if (!r.accepted) vm.notify(r.reason, margin_facts(before, delta));
        }}});
}

void GameViewModel::ask_advice() {
  if (!active() || state_->advisor.left <= 0) return;
  if (!advisor_view(texts(), *state_, busy()).can_ask) return;
  run({{GameCommand::advice(), false, [](GameViewModel& vm, const CommandResult& r) {
          if (!r.accepted) vm.set_notice({"\xE2\x9C\x96 " + vm.texts().reason(r.reason), Tone::Error});
        }}});
}

void GameViewModel::apply_advice() {
  if (!state_ || state_->advisor.advice.empty()) return;
  if (!advisor_view(texts(), *state_, busy()).actionable) return;
  ticket.apply_advice(*state_, state_->advisor.advice.back(), texts().locale());
  tab_ = Tab::Perp;
  set_notice({texts().tr("adv.applied"), Tone::Ok});
}

void GameViewModel::set_tab(Tab t) {
  if (t == Tab::Options && (!state_ || !state_->options.enabled)) return;
  tab_ = t;
  touch();
}

// ---- options ----

void GameViewModel::pick_option(int series, bool buy) {
  if (!state_) return;
  for (const auto& q : state_->options.chain) {
    if (q.id == series) option_ticket.pick(q, buy);
  }
  tab_ = Tab::Options;
  touch();
}

void GameViewModel::submit_option() {
  if (!active()) return;
  const auto d = option_ticket.draft(texts(), *state_, busy());
  if (!d) return;
  if (!d->opens_exposure) return send_option(*d);
  pending_option_ = d;
  pending_order_.reset();
  plan.open(d->buy, d->qty, 1, d->risk, {}, {}, d->label);
  modal_ = Modal::Plan;
  touch();
}

void GameViewModel::send_option(const OptionDraft& d) {
  run({{GameCommand::option_order(d.series, d.buy, d.limit, d.qty, d.premium), false,
        [d](GameViewModel& vm, const CommandResult& r) {
          if (!r.accepted) {
            RejectionFacts f;
            if (d.risk) {
              f.need = d.risk;
              f.available = vm.state_->account.available;
            }
            return vm.notify(r.reason, f);
          }
          const Texts t = vm.texts();
          vm.set_notice({"#" + std::to_string(r.order_id) + " \xE2\x9C\x93 " + side_word(t, d.buy) + " " + d.label +
                             " \xC3\x97 " + fmt::fmt_qty(d.qty, t.locale()),
                         Tone::Ok});
        }}});
}

// ---- chart ----

void GameViewModel::start_pick(PickTarget target) {
  pick_ = target;
  touch();
}

void GameViewModel::picked(double price) {
  const double tick = state_->contract.tick;
  const Locale L = texts().locale();
  if (pick_ == PickTarget::Stop || pick_ == PickTarget::Target) ticket.set_level(pick_ == PickTarget::Stop, price, tick, L);
  if (pick_ == PickTarget::ProtStop || pick_ == PickTarget::ProtTarget) {
    ticket.set_prot_level(pick_ == PickTarget::ProtStop, price, tick, L);
  }
  pick_ = PickTarget::None;
}

void GameViewModel::chart_action(const ChartAction& a) {
  if (!state_) return;
  using K = ChartAction::Kind;
  const double tick = state_->contract.tick;
  switch (a.kind) {
    case K::DraftStop: ticket.set_level(true, a.price, tick, texts().locale()); break;
    case K::DraftTarget: ticket.set_level(false, a.price, tick, texts().locale()); break;
    case K::MoveOrder: move_order(a.order_id, a.price); break;
    case K::Pattern: open_patterns(a.pattern); break;
    case K::Picked: picked(a.price); break;
    case K::CancelPick: pick_ = PickTarget::None; break;
    case K::None: return;
  }
  touch();
}

ChartScene GameViewModel::chart_scene(float width, float height) {
  if (!state_) return {};
  const auto preview = tab_ == Tab::Perp ? ticket.view(texts(), *state_, busy()).preview : std::nullopt;
  return chart.build(texts(), *state_, preview, pick_ != PickTarget::None, width, height);
}

// Dropping a working STOP/TP line moves that order in place (a bracket leg stays in its OCO pair).
void GameViewModel::move_order(int order_id, double price) {
  const auto it = std::find_if(state_->orders.begin(), state_->orders.end(), [&](const Order& o) { return o.id == order_id; });
  if (it == state_->orders.end()) return;
  const std::string to = fmt::price_to_tick(price, state_->contract.tick);
  if (to == it->price_text || fmt::num(to) == it->price) return;
  if (!exit_on_valid_side(it->side, it->type, fmt::num(to), state_->account.mark)) {
    return set_notice({"\xE2\x9C\x96 " + texts().tr(it->type == "stop_market" ? "chart.dragStopSide" : "chart.dragTpSide"), Tone::Error});
  }
  if (state_->rules.confirm_stop_beyond_liq &&
      stop_moved_beyond_liquidation(it->side, it->type, fmt::num(to), state_->account.liquidation)) {
    pending_move_ = PendingMove{order_id, to};
    modal_ = Modal::ConfirmMove;
    return touch();
  }
  run({{GameCommand::move(order_id, to), false, [order_id, to](GameViewModel& vm, const CommandResult& r) {
          if (!r.accepted) return vm.notify(r.reason);
          vm.set_notice({vm.texts().tr("chart.moved", {{"id", std::to_string(order_id)},
                                                      {"price", fmt::fmt_price(to, vm.texts().locale())}}),
                         Tone::Ok});
        }}});
}

void GameViewModel::confirm_move(bool yes) {
  const auto pm = pending_move_;
  pending_move_.reset();
  modal_ = Modal::None;
  touch();
  if (!yes || !pm) return;
  const int id = pm->order_id;
  const std::string to = pm->to;
  run({{GameCommand::move(id, to), false, [id, to](GameViewModel& vm, const CommandResult& r) {
          if (!r.accepted) return vm.notify(r.reason);
          vm.set_notice({vm.texts().tr("chart.moved", {{"id", std::to_string(id)}, {"price", fmt::fmt_price(to, vm.texts().locale())}}),
                         Tone::Ok});
        }}});
}

// ---- plan ----

void GameViewModel::plan_confirm(bool skipped) {
  if (modal_ != Modal::Plan || !state_) return;
  if (!skipped && !plan.can_save()) return;
  const PlanEntry e = plan.entry(state_->step, skipped);
  journal_.push_back(e);
  if (plan_sink_) plan_sink_(e);
  modal_ = Modal::None;
  chain_.push_back(Step{plan.command(e), false, {}});
  if (pending_order_) {
    const OrderDraft d = *pending_order_;
    pending_order_.reset();
    send_order(d);  // queued behind the plan: one intent, no advance can slip in between
    return;
  }
  if (pending_option_) {
    const OptionDraft d = *pending_option_;
    pending_option_.reset();
    send_option(d);
    return;
  }
  send_next();
  touch();
}

void GameViewModel::plan_cancel() {
  pending_order_.reset();
  pending_option_.reset();
  if (modal_ == Modal::Plan) modal_ = Modal::None;
  touch();
}

void GameViewModel::calibration_answer(bool skipped) {
  if (!state_ || !state_->calibration.due) return;
  run({{CalibrationDialog::answer(skipped ? std::nullopt : std::optional<int>(calibration.pct)), false, {}}});
}

}  // namespace tradelab::desktop::game
