#include "viewmodel/academy/ticket_sandbox.hpp"

#include <algorithm>
#include <cmath>

#include "viewmodel/game/sizing.hpp"

namespace tradelab::desktop::academy {

namespace {

game::Side side_of(bool buy) { return buy ? game::Side::Buy : game::Side::Sell; }

std::optional<double> input_value(std::string_view field, const SandboxInputs& x) {
  if (field == "side") return x.buy ? 1.0 : -1.0;
  if (field == "qty") return x.qty;
  if (field == "leverage") return static_cast<double>(x.leverage);
  if (field == "stop") return x.stop;
  return x.target;
}

}  // namespace

SandboxInputs initial_inputs(const TicketWidget& w) { return {w.buy, w.qty, w.leverage, w.stop, w.target}; }

SandboxOutputs sandbox_outputs(const TicketWidget& w, const SandboxInputs& x) {
  const double qty = std::max(0.0, x.qty);
  const int leverage = std::min(w.max_leverage, std::max(1, x.leverage));
  game::Costs costs;
  costs.maintenance_margin = w.mmr;
  const game::TicketEstimate e = game::estimate({side_of(x.buy), qty, w.entry, leverage, x.stop}, costs);
  SandboxOutputs out;
  out.notional = e.notional;
  out.margin = e.margin;
  if (qty > 0) out.liquidation = e.liquidation;
  out.risk_at_stop = e.planned_risk;
  if (e.planned_risk) out.risk_pct = (*e.planned_risk / w.equity) * 100;
  out.r_target = game::reward_risk(side_of(x.buy), w.entry, x.stop, x.target);
  out.stop_beyond_liquidation = e.stop_beyond_liquidation;
  return out;
}

std::optional<double> sandbox_value(std::string_view field, const SandboxInputs& x, const SandboxOutputs& out) {
  if (field == "side" || field == "qty" || field == "leverage" || field == "stop" || field == "target") return input_value(field, x);
  if (field == "notional") return out.notional;
  if (field == "margin") return out.margin;
  if (field == "liquidation") return out.liquidation;
  if (field == "riskAtStop") return out.risk_at_stop;
  if (field == "riskPct") return out.risk_pct;
  if (field == "rTarget") return out.r_target;
  return std::nullopt;
}

bool reaches_target(std::optional<double> value, double target, double tolerance) {
  return value && std::isfinite(*value) && std::abs(*value - target) <= tolerance + 1e-9;
}

}  // namespace tradelab::desktop::academy
