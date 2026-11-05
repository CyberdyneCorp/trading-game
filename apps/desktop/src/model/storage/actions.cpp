#include "model/storage/actions.hpp"

#include <tradelab/c_api.h>

#include <array>
#include <string_view>
#include <utility>

#include "model/storage/json_writer.hpp"

namespace tradelab::desktop::storage {

namespace {

const char* side_name(Side s) { return s == Side::Buy ? "buy" : "sell"; }

int32_t side_code(const json::Value& v) { return v.as_string() == "sell" ? 1 : 0; }

int32_t flag(const json::Value& v) { return v.as_bool(false) ? 1 : 0; }

// Optional decimal strings cross the C ABI as "" when absent (as the web worker does).
std::string text_or_empty(const json::Value& v) { return v.as_string(); }

int32_t order_type_code(std::string_view type) {
  constexpr std::array<std::string_view, 5> kTypes = {"market", "limit", "stop_market", "take_profit", "trailing_stop"};
  for (std::size_t i = 0; i < kTypes.size(); ++i) {
    if (kTypes[i] == type) return static_cast<int32_t>(i);
  }
  return -1;  // the engine rejects it
}

Action finish(JsonWriter& w) { return Action{w.end_object().text()}; }

JsonWriter begin(std::string_view kind) {
  JsonWriter w;
  w.begin_object().field("k", kind);
  return w;
}

// ---- engine calls, one per action kind ----

using Call = const char* (*)(EngineRun&, std::uint32_t, const json::Value&);

const char* call_order(EngineRun& r, std::uint32_t id, const json::Value& a) {
  const std::string type = a["type"].as_string();
  const int32_t side = side_code(a["side"]);
  const int32_t ro = flag(a["ro"]);
  const std::string qty = a["qty"].as_string();
  const std::string rate = text_or_empty(a["trailRate"]);
  if (type == "trailing_stop" && !rate.empty()) return tl_place_trailing_rate(r.handle(), id, side, qty.c_str(), ro, rate.c_str());
  const std::string price = text_or_empty(a["price"]);
  const std::string stop = text_or_empty(a["stop"]);
  const std::string tp = text_or_empty(a["tp"]);
  const std::string trail = text_or_empty(a["trail"]);
  return tl_place_order_ex(r.handle(), id, order_type_code(type), side, qty.c_str(), price.c_str(), ro, stop.c_str(),
                           tp.c_str(), trail.c_str());
}

const char* call_option(EngineRun& r, std::uint32_t id, const json::Value& a) {
  const std::string qty = a["qty"].as_string();
  const std::string premium = text_or_empty(a["premium"]);
  const int32_t type = a["type"].as_string() == "limit" ? 1 : 0;
  return tl_place_option_order(r.handle(), id, static_cast<std::uint32_t>(a["series"].as_int()), side_code(a["side"]), type,
                               qty.c_str(), premium.c_str());
}

const char* call_plan(EngineRun& r, std::uint32_t id, const json::Value& a) {
  const std::string inv = text_or_empty(a["inv"]);
  const std::string target = text_or_empty(a["target"]);
  const std::string risk = text_or_empty(a["risk"]);
  return tl_record_plan(r.handle(), id, side_code(a["side"]), flag(a["skipped"]), flag(a["option"]), flag(a["lose"]),
                        inv.c_str(), target.c_str(), static_cast<int32_t>(a["conf"].as_int()), risk.c_str());
}

const char* call_lesson(EngineRun& r, std::uint32_t id, const json::Value& a) {
  const std::string value = text_or_empty(a["value"]);
  return tl_answer_lesson(r.handle(), id, static_cast<std::uint32_t>(a["q"].as_int()),
                          static_cast<std::uint32_t>(a["choice"].as_int()), value.c_str(), flag(a["skipped"]));
}

const char* call_leverage(EngineRun& r, std::uint32_t id, const json::Value& a) {
  return tl_set_leverage(r.handle(), id, static_cast<int32_t>(a["lev"].as_int()));
}

const char* call_margin(EngineRun& r, std::uint32_t id, const json::Value& a) {
  const std::string amount = a["amount"].as_string();
  return tl_adjust_margin(r.handle(), id, amount.c_str());
}

const char* call_answer(EngineRun& r, std::uint32_t id, const json::Value& a) {
  return tl_answer_question(r.handle(), id, static_cast<int32_t>(a["pBp"].as_int()), flag(a["skipped"]));
}

const char* call_cancel(EngineRun& r, std::uint32_t id, const json::Value& a) {
  return tl_cancel_order(r.handle(), id, static_cast<std::uint32_t>(a["id"].as_int()));
}

const char* call_move(EngineRun& r, std::uint32_t id, const json::Value& a) {
  const std::string price = a["price"].as_string();
  return tl_move_order(r.handle(), id, static_cast<std::uint32_t>(a["id"].as_int()), price.c_str());
}

const char* call_close(EngineRun& r, std::uint32_t id, const json::Value& /*a*/) {
  return tl_close_position(r.handle(), id);
}

const char* call_advice(EngineRun& r, std::uint32_t id, const json::Value& /*a*/) {
  return tl_request_advice(r.handle(), id);
}

constexpr std::array<std::pair<std::string_view, Call>, 11> kCalls = {{
    {"order", call_order},
    {"opt", call_option},
    {"plan", call_plan},
    {"lesson", call_lesson},
    {"lev", call_leverage},
    {"margin", call_margin},
    {"ans", call_answer},
    {"cancel", call_cancel},
    {"move", call_move},
    {"close", call_close},
    {"advice", call_advice},
}};

Call find_call(std::string_view kind) {
  for (const auto& [name, call] : kCalls) {
    if (name == kind) return call;
  }
  return nullptr;
}

}  // namespace

namespace actions {

Action advance(int steps) {
  JsonWriter w = begin("adv");
  w.field("n", steps);
  return finish(w);
}

Action market(Side side, const std::string& qty) {
  OrderAction o;
  o.side = side;
  o.qty = qty;
  return order(o);
}

Action order(const OrderAction& o) {
  JsonWriter w = begin("order");
  w.field("type", o.type).field("side", side_name(o.side)).field("qty", o.qty).field("price", o.price);
  w.field("ro", o.reduce_only);
  if (o.stop) w.field("stop", o.stop);
  if (o.take_profit) w.field("tp", o.take_profit);
  if (o.trail) w.field("trail", o.trail);
  if (o.trail_rate) w.field("trailRate", o.trail_rate);
  return finish(w);
}

Action option(std::uint32_t series, Side side, bool limit, const std::string& qty, const std::optional<std::string>& premium) {
  JsonWriter w = begin("opt");
  w.field("series", series).field("side", side_name(side)).field("type", limit ? "limit" : "market");
  w.field("qty", qty).field("premium", premium);
  return finish(w);
}

Action leverage(int leverage) {
  JsonWriter w = begin("lev");
  w.field("lev", leverage);
  return finish(w);
}

Action margin(const std::string& amount) {
  JsonWriter w = begin("margin");
  w.field("amount", amount);
  return finish(w);
}

Action plan(const PlanAction& p) {
  JsonWriter w = begin("plan");
  w.field("side", side_name(p.side)).field("skipped", p.skipped).field("option", p.option).field("lose", p.lose);
  w.field("inv", p.invalidation).field("target", p.target).field("conf", p.confidence).field("risk", p.risk);
  return finish(w);
}

Action answer(int p_bp, bool skipped) {
  JsonWriter w = begin("ans");
  w.field("pBp", p_bp).field("skipped", skipped);
  return finish(w);
}

Action lesson(std::uint32_t question, std::uint32_t choice, const std::optional<std::string>& value, bool skipped) {
  JsonWriter w = begin("lesson");
  w.field("q", question).field("choice", choice).field("value", value).field("skipped", skipped);
  return finish(w);
}

Action cancel(std::uint32_t order_id) {
  JsonWriter w = begin("cancel");
  w.field("id", order_id);
  return finish(w);
}

Action move(std::uint32_t order_id, const std::string& price) {
  JsonWriter w = begin("move");
  w.field("id", order_id).field("price", price);
  return finish(w);
}

Action close() {
  JsonWriter w = begin("close");
  return finish(w);
}

Action advice() {
  JsonWriter w = begin("advice");
  return finish(w);
}

}  // namespace actions

bool is_advance(const json::Value& action) { return action["k"].as_string() == "adv"; }

ApplyResult apply_action(EngineRun& run, const json::Value& action) {
  ApplyResult out;
  if (is_advance(action)) {
    out.advance = true;
    out.accepted = true;
    run.advance(static_cast<int>(action["n"].as_int(1)));
    return out;
  }
  const Call call = find_call(action["k"].as_string());
  if (call == nullptr) {
    out.known = false;
    return out;
  }
  const char* result = call(run, run.take_client_id(), action);
  out.result = result != nullptr ? result : "";
  const auto parsed = json::parse(out.result);
  out.accepted = parsed && (*parsed)["accepted"].as_bool(false);
  out.reason = parsed ? (*parsed)["reason"].as_string("invalid") : "invalid";
  return out;
}

ApplyResult apply_action(EngineRun& run, const Action& action) {
  const auto parsed = json::parse(action.json);
  if (!parsed) return ApplyResult{false, false, {}, false, "invalid"};
  return apply_action(run, *parsed);
}

}  // namespace tradelab::desktop::storage
