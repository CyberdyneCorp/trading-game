// Paints ChartGeometry with Raven's custom-widget drawing calls (raven_draw.h); no chart math here.
#pragma once

#include <raven/raven_draw.h>

#include "viewmodel/chart_geometry.hpp"

namespace tradelab::desktop {

void paint_chart(RPainter* painter, const ChartGeometry& g, float width, float height);

}  // namespace tradelab::desktop
