// Port of apps/web/src/lib/content/ticketSandbox.ts (web parity fixture ticketSandbox.json): the
// lesson ticket sandbox, a perpetual ticket that never sends anything. Its numbers come from the
// terminal's own display mirrors (game/sizing) on a contract without fees, spread or slippage, so a
// lesson's arithmetic (risk = distance to the stop x quantity) reads exactly.
#pragma once

#include <optional>
#include <string_view>

#include "model/content.hpp"

namespace tradelab::desktop::academy {

struct SandboxInputs {
  bool buy = true;
  double qty = 0;
  int leverage = 1;
  std::optional<double> stop;
  std::optional<double> target;
  bool operator==(const SandboxInputs&) const = default;
};

struct SandboxOutputs {
  double notional = 0;
  double margin = 0;
  std::optional<double> liquidation;
  std::optional<double> risk_at_stop;
  std::optional<double> risk_pct;
  std::optional<double> r_target;
  bool stop_beyond_liquidation = false;
};

SandboxInputs initial_inputs(const TicketWidget& w);
SandboxOutputs sandbox_outputs(const TicketWidget& w, const SandboxInputs& x);
// The value a `set` question reads: an input (side = 1 buy, -1 sell) or an output of the sandbox.
std::optional<double> sandbox_value(std::string_view field, const SandboxInputs& x, const SandboxOutputs& out);
bool reaches_target(std::optional<double> value, double target, double tolerance);

}  // namespace tradelab::desktop::academy
