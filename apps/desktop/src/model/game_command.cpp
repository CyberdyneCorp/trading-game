#include "model/game_command.hpp"

#include <utility>

#include "model/json.hpp"

namespace tradelab::desktop {

CommandResult game_result(GameCommandKind kind, const std::string& text) {
  CommandResult r{CommandKind::Game, false, "invalid"};
  r.game = kind;
  const auto v = json::parse(text);
  if (!v) return r;
  r.accepted = (*v)["accepted"].as_bool(false);
  r.reason = (*v)["reason"].as_string("invalid");
  r.order_id = static_cast<int>((*v)["orderId"].as_int(0));
  r.warning = (*v)["warning"].as_string();
  return r;
}

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

GameCommand GameCommand::tagged(std::string combo_id, std::string strategy_name) const {
  GameCommand c = *this;
  c.combo = std::move(combo_id);
  c.strategy = std::move(strategy_name);
  return c;
}

}  // namespace tradelab::desktop
