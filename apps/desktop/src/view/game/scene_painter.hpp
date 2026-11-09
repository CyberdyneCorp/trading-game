// Paints a ChartScene (the chart viewmodel's display list) and the option payoff curve with Raven's
// custom-widget drawing calls; no chart math here.
#pragma once

#include <raven/raven_draw.h>

#include "viewmodel/game/chart_vm.hpp"
#include "viewmodel/game/options_vm.hpp"

namespace tradelab::desktop::view {

void paint_scene(RPainter* painter, const game::ChartScene& scene);
// The settings' colorblind palette for the drawn charts, payoffs and heatmaps (gains blue, losses orange).
void set_scene_colorblind(bool on);
void paint_payoff(RPainter* painter, const game::OptionTicketView& v, float width, float height);

}  // namespace tradelab::desktop::view
