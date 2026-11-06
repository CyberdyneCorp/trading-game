#include "viewmodel/game/dock_layout.hpp"

#include <algorithm>
#include <cmath>

namespace tradelab::desktop::game {

namespace {

// Share of a split given to its first part, in Raven's thousandths: `first` pixels of `length`, the
// splitter excluded (Raven: first = room * ratio, room = length - gap).
int share(float first, float length, float gap) {
  const float room = std::max(1.0F, length - gap);
  return static_cast<int>(std::lround(std::clamp(first / room, 0.02F, 0.98F) * 1000.0F));
}

std::string tabs(std::string_view panel) { return "T(0;" + std::string(panel) + ")"; }

std::string split(char dir, int ratio, const std::string& a, const std::string& b) {
  return std::string(1, dir) + "(" + std::to_string(ratio) + "," + a + "," + b + ")";
}

}  // namespace

std::string_view panel_title_key(std::string_view panel) {
  if (panel == kPanelChart) return "desktop.panel.chart";
  if (panel == kPanelRail) return "desktop.panel.account";
  if (panel == kPanelTicket) return "ticket.title";
  if (panel == kPanelBook) return "desktop.panel.book";
  return "desktop.panel.feed";
}

std::string default_layout(float width, float height, const DockMetrics& m) {
  const float bottom = m.bottom + m.tab_bar;
  const int top = share(height - m.gap - bottom, height, m.gap);
  const int chart = share(width - m.gap - m.rail, width, m.gap);
  const int ticket = share(m.ticket, width, m.gap);
  const float rest = width - m.gap - m.ticket;  // book + feed
  const int book = share(rest - m.gap - m.feed, rest, m.gap);
  const std::string upper = split('H', chart, tabs(kPanelChart), tabs(kPanelRail));
  const std::string lower = split('H', ticket, tabs(kPanelTicket), split('H', book, tabs(kPanelBook), tabs(kPanelFeed)));
  return split('V', top, upper, lower);
}

DockLayout::Step DockLayout::frame(float width, float height, const std::string& stored, const DockMetrics& m) {
  if (width <= 0 || height <= 0) return {};
  if (!stored.empty()) {
    if (custom_shown_) return {};
    custom_shown_ = true;
    return {true, stored};
  }
  const bool resized = std::abs(width - width_) >= 1 || std::abs(height - height_) >= 1;
  if (loaded_ && !resized) return {};
  loaded_ = true;
  custom_shown_ = false;
  width_ = width;
  height_ = height;
  return {true, default_layout(width, height, m)};
}

void DockLayout::reset() {
  custom_shown_ = false;
  loaded_ = false;
}

}  // namespace tradelab::desktop::game
