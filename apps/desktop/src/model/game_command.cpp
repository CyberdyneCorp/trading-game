#include "model/game_command.hpp"

#include <tradelab/c_api.h>

#include <utility>

#include "model/engine_run.hpp"

namespace tradelab::desktop {

namespace {

const char* opt(const std::string& s) { return s.empty() ? nullptr : s.c_str(); }

int side_code(bool buy) { return buy ? 0 : 1; }

CommandResult result_from(GameCommandKind kind, const char* text) {
  CommandResult r{CommandKind::Game, false, "invalid"};
  r.game = kind;
  const auto v = json::parse(text != nullptr ? text : "");
  if (!v) return r;
  r.accepted = (*v)["accepted"].as_bool(false);
  r.reason = (*v)["reason"].as_string("invalid");
  r.order_id = static_cast<int>((*v)["orderId"].as_int(0));
  r.warning = (*v)["warning"].as_string();
  return r;
}

const char* run_order(tl_run* run, std::uint32_t id, const GameCommand& c) {
  return tl_place_order_ex(run, id, c.type, side_code(c.buy), c.qty.c_str(), opt(c.price), c.reduce_only ? 1 : 0,
                           opt(c.stop), opt(c.tp), opt(c.trail));
}

const char* run_plan(tl_run* run, std::uint32_t id, const GameCommand& c) {
  return tl_record_plan(run, id, side_code(c.buy), c.skipped ? 1 : 0, c.option ? 1 : 0, c.lose_scenario ? 1 : 0,
                        opt(c.stop), opt(c.tp), c.number, opt(c.amount));
}

const char* dispatch(tl_run* run, std::uint32_t id, const GameCommand& c) {
  switch (c.kind) {
    case GameCommandKind::Order: return run_order(run, id, c);
    case GameCommandKind::TrailingRate:
      return tl_place_trailing_rate(run, id, side_code(c.buy), c.qty.c_str(), c.reduce_only ? 1 : 0, c.trail.c_str());
    case GameCommandKind::Option:
      return tl_place_option_order(run, id, static_cast<std::uint32_t>(c.id), side_code(c.buy), c.type, c.qty.c_str(),
                                   opt(c.price));
    case GameCommandKind::Cancel: return tl_cancel_order(run, id, static_cast<std::uint32_t>(c.id));
    case GameCommandKind::Move: return tl_move_order(run, id, static_cast<std::uint32_t>(c.id), c.price.c_str());
    case GameCommandKind::Leverage: return tl_set_leverage(run, id, c.number);
    case GameCommandKind::Close: return tl_close_position(run, id);
    case GameCommandKind::Margin: return tl_adjust_margin(run, id, c.amount.c_str());
    case GameCommandKind::Plan: return run_plan(run, id, c);
    case GameCommandKind::Answer: return tl_answer_question(run, id, c.number, c.skipped ? 1 : 0);
    case GameCommandKind::Lesson:
      return tl_answer_lesson(run, id, static_cast<std::uint32_t>(c.id), static_cast<std::uint32_t>(c.number),
                              opt(c.amount), c.skipped ? 1 : 0);
    case GameCommandKind::Advice: return tl_request_advice(run, id);
    case GameCommandKind::None: break;
  }
  return nullptr;
}

}  // namespace

GameCommand GameCommand::order(OrderType type, bool buy, std::string qty, std::string price, bool reduce_only,
                               std::string stop, std::string tp, std::string trail) {
  GameCommand c;
  c.kind = GameCommandKind::Order;
  c.type = static_cast<int>(type);
  c.buy = buy;
  c.qty = std::move(qty);
  c.price = std::move(price);
  c.reduce_only = reduce_only;
  c.stop = std::move(stop);
  c.tp = std::move(tp);
  c.trail = std::move(trail);
  return c;
}

GameCommand GameCommand::trailing_rate(bool buy, std::string qty, std::string rate) {
  GameCommand c;
  c.kind = GameCommandKind::TrailingRate;
  c.buy = buy;
  c.qty = std::move(qty);
  c.reduce_only = true;
  c.trail = std::move(rate);
  return c;
}

GameCommand GameCommand::option_order(int series, bool buy, bool limit, std::string qty, std::string premium) {
  GameCommand c;
  c.kind = GameCommandKind::Option;
  c.id = series;
  c.buy = buy;
  c.type = limit ? 1 : 0;
  c.qty = std::move(qty);
  c.price = std::move(premium);
  return c;
}

GameCommand GameCommand::cancel(int order_id) {
  GameCommand c;
  c.kind = GameCommandKind::Cancel;
  c.id = order_id;
  return c;
}

GameCommand GameCommand::move(int order_id, std::string price) {
  GameCommand c;
  c.kind = GameCommandKind::Move;
  c.id = order_id;
  c.price = std::move(price);
  return c;
}

GameCommand GameCommand::leverage(int leverage) {
  GameCommand c;
  c.kind = GameCommandKind::Leverage;
  c.number = leverage;
  return c;
}

GameCommand GameCommand::close() {
  GameCommand c;
  c.kind = GameCommandKind::Close;
  return c;
}

GameCommand GameCommand::margin(std::string amount) {
  GameCommand c;
  c.kind = GameCommandKind::Margin;
  c.amount = std::move(amount);
  return c;
}

GameCommand GameCommand::plan(bool buy, bool skipped, bool option, bool lose_scenario, std::string invalidation,
                              std::string target, int confidence, std::string planned_risk) {
  GameCommand c;
  c.kind = GameCommandKind::Plan;
  c.buy = buy;
  c.skipped = skipped;
  c.option = option;
  c.lose_scenario = lose_scenario;
  c.stop = std::move(invalidation);
  c.tp = std::move(target);
  c.number = confidence;
  c.amount = std::move(planned_risk);
  return c;
}

GameCommand GameCommand::answer(int p_bp, bool skipped) {
  GameCommand c;
  c.kind = GameCommandKind::Answer;
  c.number = p_bp;
  c.skipped = skipped;
  return c;
}

GameCommand GameCommand::lesson(int question, int choice, std::string value, bool skipped) {
  GameCommand c;
  c.kind = GameCommandKind::Lesson;
  c.id = question;
  c.number = choice;
  c.amount = std::move(value);
  c.skipped = skipped;
  return c;
}

GameCommand GameCommand::advice() {
  GameCommand c;
  c.kind = GameCommandKind::Advice;
  return c;
}

CommandResult EngineRun::execute(const GameCommand& c) {
  return result_from(c.kind, dispatch(run_, next_client_id_++, c));
}

}  // namespace tradelab::desktop
