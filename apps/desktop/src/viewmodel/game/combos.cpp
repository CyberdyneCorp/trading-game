#include "viewmodel/game/combos.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <set>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

namespace {

bool same_qty(const std::string& a, const std::string& b) { return std::fabs(fmt::num(a) - fmt::num(b)) < 1e-9; }

// Only the log lines combos read: advances (the step) and tagged actions.
bool relevant(const std::string& line) {
  return line.find("\"combo\"") != std::string::npos || line.find("\"adv\"") != std::string::npos;
}

OrderLeg leg_of(const json::Value& a) {
  const bool option = a["k"].as_string() == "opt";
  OrderLeg leg{option ? static_cast<int>(a["series"].as_int()) : 0, a["side"].as_string("buy") != "sell", a["qty"].as_string("0"),
               std::nullopt};
  const std::string premium = a["premium"].as_string();
  if (option && a["type"].as_string() == "limit" && !premium.empty()) leg.premium = premium;
  return leg;
}

ComboSubmission submission_key(const json::Value& a, int step) {
  const std::string roll = a["roll"].as_string();
  const std::string adjust = a["adjust"].as_string();
  if (!roll.empty()) return {SubmissionKind::Roll, roll, step, {}, false};
  if (!adjust.empty()) return {SubmissionKind::Adjust, adjust, step, {}, false};
  return {SubmissionKind::Open, "", step, {}, false};
}

ComboSubmission& submission_of(ComboRecord& rec, const ComboSubmission& key) {
  for (auto& s : rec.submissions) {
    if (s.kind == key.kind && s.n == key.n) return s;
  }
  rec.submissions.push_back(key);
  return rec.submissions.back();
}

LegStatus leg_status(const std::vector<EngineEvent>& events, const OrderLeg& leg, std::optional<int> order_id) {
  LegStatus out{leg, order_id, LegState::Working, {}, std::nullopt, std::nullopt};
  if (!order_id) return out;
  const auto fill = std::find_if(events.begin(), events.end(),
                                 [&](const EngineEvent& e) { return e.type == "fill" && e.order_id == *order_id; });
  if (fill != events.end()) {
    out.state = LegState::Filled;
    out.price = fill->price;
    out.fill_step = fill->step;
    return out;
  }
  const auto end = std::find_if(events.begin(), events.end(), [&](const EngineEvent& e) {
    return e.type == "order" && e.order_id == *order_id && (e.status == "cancelled" || e.status == "expired");
  });
  if (end != events.end()) {
    out.state = LegState::Cancelled;
    out.reason = end->reason;
  }
  return out;
}

ComboState overall(bool withdrawn, const std::vector<LegStatus>& legs) {
  if (withdrawn) return ComboState::Withdrawn;
  if (std::any_of(legs.begin(), legs.end(), [](const LegStatus& l) { return l.state == LegState::Working; })) {
    return ComboState::Working;
  }
  const auto filled = std::count_if(legs.begin(), legs.end(), [](const LegStatus& l) { return l.state == LegState::Filled; });
  if (static_cast<std::size_t>(filled) == legs.size()) return ComboState::Filled;
  return filled == 0 ? ComboState::NotExecuted : ComboState::Incomplete;
}

// The accepted orders from `start` match the legs in order (side and quantity).
bool run_matches(const std::vector<const EngineEvent*>& accepted, std::size_t start, const std::vector<OrderLeg>& legs) {
  for (std::size_t i = 0; i < legs.size(); ++i) {
    const EngineEvent& e = *accepted[start + i];
    const OrderLeg& leg = legs[i];
    if (e.side != (leg.buy ? "buy" : "sell") || !same_qty(e.qty, leg.qty)) return false;
  }
  return true;
}

const ComboTrade* trade_of_leg(const std::vector<ComboTrade>& trades, const LegStatus& l) {
  const std::string side = l.leg.buy ? "long" : "short";
  for (const auto& t : trades) {
    if (t.series == l.leg.series && l.fill_step && t.entry_step == *l.fill_step && t.side == side) return &t;
  }
  return nullptr;
}

}  // namespace

std::string next_combo_id(const std::vector<ComboRecord>& combos) {
  int top = 0;
  for (const auto& c : combos) {
    if (c.id.size() < 2) continue;
    const double n = fmt::num(std::string_view(c.id).substr(1));
    if (std::isfinite(n)) top = std::max(top, static_cast<int>(n));
  }
  return "c" + std::to_string(top + 1);
}

std::vector<ComboRecord> combos_from_log(const std::vector<std::string>& log, int start_step) {
  std::vector<ComboRecord> out;
  std::map<std::string, std::size_t> index;
  int step = start_step;
  for (const auto& line : log) {
    if (!relevant(line)) continue;
    const auto a = json::parse(line);
    if (!a) continue;
    const std::string k = (*a)["k"].as_string();
    if (k == "adv") step += static_cast<int>((*a)["n"].as_int());
    const std::string id = (*a)["combo"].as_string();
    if (id.empty()) continue;
    auto [it, fresh] = index.try_emplace(id, out.size());
    if (fresh) out.push_back({id, (*a)["strategy"].as_string("custom"), step, {}, false, {}});
    ComboRecord& rec = out[it->second];
    ComboSubmission& sub = submission_of(rec, submission_key(*a, step));
    if (k == "cancel") sub.withdrawn = true;
    else sub.legs.push_back(leg_of(*a));
    if (sub.kind != SubmissionKind::Open) continue;
    rec.legs = sub.legs;
    rec.withdrawn = sub.withdrawn;
  }
  return out;
}

std::string_view submission_kind_code(SubmissionKind k) {
  switch (k) {
    case SubmissionKind::Open: return "open";
    case SubmissionKind::Roll: return "roll";
    case SubmissionKind::Adjust: break;
  }
  return "adjust";
}

std::string next_submission_number(const ComboRecord& rec, SubmissionKind kind) {
  const auto n = std::count_if(rec.submissions.begin(), rec.submissions.end(), [&](const ComboSubmission& s) { return s.kind == kind; });
  return std::to_string(n + 1);
}

std::string_view leg_state_code(LegState s) {
  switch (s) {
    case LegState::Working: return "working";
    case LegState::Filled: return "filled";
    case LegState::Cancelled: break;
  }
  return "cancelled";
}

std::string_view combo_state_code(ComboState s) {
  switch (s) {
    case ComboState::Working: return "working";
    case ComboState::Filled: return "filled";
    case ComboState::Withdrawn: return "withdrawn";
    case ComboState::NotExecuted: return "not_executed";
    case ComboState::Incomplete: break;
  }
  return "incomplete";
}

std::vector<std::optional<int>> combo_order_ids(const std::vector<EngineEvent>& events, int step, const std::vector<OrderLeg>& legs) {
  std::vector<const EngineEvent*> accepted;
  for (const auto& e : events) {
    if (e.type == "order" && e.status == "accepted" && e.step == step) accepted.push_back(&e);
  }
  const auto n = static_cast<std::ptrdiff_t>(legs.size());
  for (auto start = static_cast<std::ptrdiff_t>(accepted.size()) - n; start >= 0; --start) {
    if (!run_matches(accepted, static_cast<std::size_t>(start), legs)) continue;
    std::vector<std::optional<int>> ids;
    for (std::ptrdiff_t i = 0; i < n; ++i) ids.emplace_back(accepted[static_cast<std::size_t>(start + i)]->order_id);
    return ids;
  }
  return std::vector<std::optional<int>>(legs.size());
}

std::vector<std::optional<int>> combo_order_ids(const std::vector<EngineEvent>& events, const ComboRecord& rec) {
  return combo_order_ids(events, rec.step, rec.legs);
}

ComboStatus combo_status(const std::vector<EngineEvent>& events, const ComboRecord& rec) {
  const std::vector<ComboSubmission> subs =
      rec.submissions.empty() ? std::vector<ComboSubmission>{{SubmissionKind::Open, "", rec.step, rec.legs, rec.withdrawn}}
                              : rec.submissions;
  ComboStatus out{rec, ComboState::Working, {}, {}};
  for (const auto& sub : subs) {
    const auto ids = combo_order_ids(events, sub.step, sub.legs);
    out.legs.clear();
    for (std::size_t i = 0; i < sub.legs.size(); ++i) out.legs.push_back(leg_status(events, sub.legs[i], ids[i]));
    out.all_legs.insert(out.all_legs.end(), out.legs.begin(), out.legs.end());
    out.state = overall(sub.withdrawn, out.legs);
  }
  return out;
}

std::vector<HeldLeg> combo_holdings(const ComboStatus& status) {
  std::vector<HeldLeg> net;  // first-fill order, as the web's Map keeps it
  for (const auto& l : status.all_legs) {
    if (l.state != LegState::Filled) continue;
    const double q = fmt::num(l.leg.qty) * (l.leg.buy ? 1 : -1);
    auto it = std::find_if(net.begin(), net.end(), [&](const HeldLeg& h) { return h.series == l.leg.series; });
    if (it == net.end()) net.push_back({l.leg.series, q});
    else it->qty += q;
  }
  std::vector<HeldLeg> out;
  for (const auto& h : net) {
    if (std::fabs(h.qty) > 1e-9) out.push_back({h.series, std::round(h.qty * 1e8) / 1e8});
  }
  return out;
}

std::vector<int> working_legs(const ComboStatus& status) {
  std::vector<int> out;
  for (const auto& l : status.legs) {
    if (l.state == LegState::Working && l.order_id) out.push_back(*l.order_id);
  }
  return out;
}

double net_premium(const ComboStatus& status, double multiplier) {
  double total = 0;
  for (const auto& l : status.legs) {
    if (l.state != LegState::Filled || l.leg.series == 0 || !l.price) continue;
    const double cash = fmt::num(*l.price) * fmt::num(l.leg.qty) * multiplier;
    total += l.leg.buy ? -cash : cash;
  }
  return total;
}

std::vector<LegStatus> open_legs(const ComboStatus& status) {
  std::vector<LegStatus> out;
  if (status.state != ComboState::Incomplete) return out;
  std::copy_if(status.legs.begin(), status.legs.end(), std::back_inserter(out),
               [](const LegStatus& l) { return l.state == LegState::Filled; });
  return out;
}

std::string combo_contracts(const std::vector<OrderLeg>& orders) {
  const auto it = std::find_if(orders.begin(), orders.end(), [](const OrderLeg& o) { return o.series != 0; });
  if (it != orders.end()) return it->qty;
  return orders.empty() ? "0" : orders.front().qty;
}

std::vector<ComboTrade> combo_trades_from(const json::Value& trades) {
  std::vector<ComboTrade> out;
  for (const auto& t : trades.items()) {
    ComboTrade c;
    c.n = static_cast<int>(t["n"].as_int());
    c.series = static_cast<int>(t["series"].as_int());
    c.side = t["side"].as_string();
    c.entry_step = static_cast<int>(t["entryStep"].as_int());
    c.net = fmt::num(t["net"].as_string("0"));
    if (!t["rBp"].is_null()) c.r_bp = static_cast<int>(t["rBp"].as_int());
    out.push_back(std::move(c));
  }
  return out;
}

std::optional<double> combo_r(const std::vector<ComboTrade>& trades, double net) {
  const auto leg = std::find_if(trades.begin(), trades.end(),
                                [](const ComboTrade& t) { return t.r_bp && *t.r_bp != 0 && t.net != 0; });
  if (leg == trades.end()) return std::nullopt;
  const double share = leg->net / (*leg->r_bp / 10000.0);
  if (share <= 0) return std::nullopt;
  return net / (share * static_cast<double>(trades.size()));
}

GroupedTrades group_trades(const std::vector<ComboTrade>& trades, const std::vector<ComboStatus>& statuses) {
  std::set<int> used;
  GroupedTrades out;
  for (const auto& status : statuses) {
    ComboTrades group{status, {}, 0, std::nullopt};
    // A rolled or adjusted combo stays one row: every submission's legs (D3).
    for (const auto& l : status.all_legs.empty() ? status.legs : status.all_legs) {
      const ComboTrade* t = l.state == LegState::Filled ? trade_of_leg(trades, l) : nullptr;
      if (t == nullptr || used.count(t->n) != 0) continue;
      used.insert(t->n);
      group.trades.push_back(*t);
      group.net += t->net;
    }
    if (group.trades.empty()) continue;
    group.r = combo_r(group.trades, group.net);
    out.combos.push_back(std::move(group));
  }
  std::copy_if(trades.begin(), trades.end(), std::back_inserter(out.rest), [&](const ComboTrade& t) { return used.count(t.n) == 0; });
  return out;
}

}  // namespace tradelab::desktop::game
