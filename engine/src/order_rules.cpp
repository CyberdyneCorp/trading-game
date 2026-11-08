// Execution policy v18: time in force and player-built OCO links (design D6, D7 of
// add-practice-and-advanced-strategies).
//
// Time in force. gtc works as every order did before v18. bars:N is eligible on bars placed+1 ..
// placed+N and expires at the close of the last one. ioc and fok (limits only) are eligible in the
// next bar's open phase only, the one "immediate" price a bar engine has without future information;
// ioc keeps what that phase filled, fok fills its whole quantity at its turn or nothing.
//
// OCO links. Two working orders of any book share link_group (the lower id). The first fill on either
// cancels the other's remainder before its own turn, so both never fill on one bar; two reduce-only
// exits of the perpetual position reduce each other instead, so a partial exit keeps protection for
// the rest (as bracket legs do). A cancel or an expiry of one dissolves the link.

#include "order_rules.hpp"

#include <algorithm>

#include "accounting.hpp"

namespace tradelab::detail {
namespace {

CommandResult reject(Reason r) { return CommandResult{false, r, 0, Reason::None}; }

Order* find_order(RunState& st, std::uint32_t id) {
  const auto it = std::find_if(st.orders.begin(), st.orders.end(), [id](const Order& o) { return o.id == id; });
  return it == st.orders.end() ? nullptr : &*it;
}

Order* linked_sibling(RunState& st, const Order& o) {
  if (o.link_group == 0) return nullptr;
  for (Order& x : st.orders) {
    if (x.id != o.id && x.link_group == o.link_group && x.active()) return &x;
  }
  return nullptr;
}

bool bars_allowed(const Command& c, bool option) {
  if (option) return c.order_type == OrderType::Limit;
  return c.order_type != OrderType::Market;
}

bool ended_unfilled(const Order& o) {
  return o.status == OrderStatus::Cancelled || o.status == OrderStatus::Expired || o.status == OrderStatus::Rejected;
}

}  // namespace

Reason tif_refusal(const Scenario& sc, const RunState& st, const Command& c, bool option) {
  switch (c.tif) {
    case TimeInForce::Gtc: return c.tif_bars == 0 ? Reason::None : Reason::InvalidTimeInForce;
    case TimeInForce::Bars: {
      const std::uint32_t last_bar = static_cast<std::uint32_t>(sc.bars.size() - 1);
      const bool fits = c.tif_bars >= 1 && c.tif_bars <= last_bar - st.step;
      return fits && bars_allowed(c, option) ? Reason::None : Reason::InvalidTimeInForce;
    }
    case TimeInForce::Ioc:
    case TimeInForce::Fok: return c.tif_bars == 0 && c.order_type == OrderType::Limit ? Reason::None : Reason::InvalidTimeInForce;
  }
  return Reason::InvalidTimeInForce;
}

void set_tif(Order& o, const Command& c) {
  o.tif = c.tif;
  o.tif_bars = c.tif == TimeInForce::Bars ? c.tif_bars : 0;
  if (c.tif == TimeInForce::Bars) o.expire_step = o.placed_step + c.tif_bars;
  if (c.tif == TimeInForce::Ioc || c.tif == TimeInForce::Fok) o.expire_step = o.placed_step + 1;
}

bool immediate(const Order& o) { return o.tif == TimeInForce::Ioc || o.tif == TimeInForce::Fok; }

void cancel_immediate(RunState& st, bool options) {
  for (Order& o : st.orders) {
    if (!o.active() || !immediate(o) || (o.series != 0) != options) continue;
    o.status = OrderStatus::Cancelled;
    emit_order_update(st, o, Reason::TimeInForce);
  }
}

void expire_time_in_force(RunState& st, std::uint32_t bar) {
  for (Order& o : st.orders) {
    if (!o.active() || o.tif != TimeInForce::Bars || o.expire_step != bar) continue;
    o.status = OrderStatus::Expired;
    emit_order_update(st, o, Reason::TimeInForce);
  }
}

CommandResult link_oco(RunState& st, const Command& c) {
  Order* a = find_order(st, c.order_id);
  Order* b = find_order(st, c.order_id_b);
  const auto linkable = [](const Order* o) { return o != nullptr && o->active() && o->parent_id == 0 && o->link_group == 0; };
  if (c.order_id == c.order_id_b || !linkable(a) || !linkable(b)) return reject(Reason::InvalidOco);
  const std::uint32_t group = std::min(a->id, b->id);
  a->link_group = group;
  b->link_group = group;
  st.last_link_step = st.step;
  return CommandResult{true, Reason::None, group, Reason::None};
}

void resolve_link(RunState& st, const Order& filled, Qty q) {
  Order* sib = linked_sibling(st, filled);
  if (sib == nullptr) return;
  const bool exit_pair = filled.series == 0 && sib->series == 0 && filled.reduce_only && sib->reduce_only;
  if (exit_pair) {
    sib->qty = Qty::from_raw(std::max(sib->filled.raw, sib->qty.raw - q.raw));
    if (sib->remaining().raw > 0) return;
  }
  sib->status = OrderStatus::Cancelled;
  emit_order_update(st, *sib, Reason::OcoSiblingFilled);
}

void dissolve_link(RunState& st, const Order& ended) {
  if (!ended_unfilled(ended)) return;
  if (Order* sib = linked_sibling(st, ended); sib != nullptr) sib->link_group = 0;
}

}  // namespace tradelab::detail
