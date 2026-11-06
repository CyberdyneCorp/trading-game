// The terminal's docked panels (desktop-client "Terminal UX parity": dockable, resizable, persisted per
// installation, "Reset layout"). The panels are the web Terminal's regions; Raven's DockArea arranges
// them and describes an arrangement as a short text (its layout format, split ratios in thousandths):
//   V(<top>,H(<chart>,T(0;game-panel-chart),T(0;game-panel-rail)),
//           H(<ticket>,T(0;game-panel-ticket),H(<book>,T(0;game-panel-book),T(0;game-panel-feed))))
// Raven splits by ratio, so the default is computed for the area's size: the rail, the ticket and the
// feed keep the widths the web's columns have (290, 340, 300 px) and the bottom row its height, at any
// window size. A player's own arrangement is stored as Raven wrote it; until there is one, the default
// follows the window.
#pragma once

#include <array>
#include <string>
#include <string_view>

namespace tradelab::desktop::game {

inline constexpr std::string_view kPanelChart = "game-panel-chart";
inline constexpr std::string_view kPanelRail = "game-panel-rail";
inline constexpr std::string_view kPanelTicket = "game-panel-ticket";
inline constexpr std::string_view kPanelBook = "game-panel-book";
inline constexpr std::string_view kPanelFeed = "game-panel-feed";
inline constexpr std::array<std::string_view, 5> kPanels = {kPanelChart, kPanelRail, kPanelTicket, kPanelBook, kPanelFeed};

// Catalog key of each panel's tab title.
std::string_view panel_title_key(std::string_view panel);

struct DockMetrics {
  float gap = 4;         // the splitter between two panels
  float tab_bar = 30;    // a panel's tab row
  float rail = 290;      // web RightRail
  float ticket = 340;    // web OrderTicket column
  float feed = 300;      // advisor, news and events
  float bottom = 300;    // the ticket row's content height
};

// The default arrangement for a dock area of `width` x `height` (logical pixels).
std::string default_layout(float width, float height, const DockMetrics& m = {});

// What the terminal shows: the player's arrangement when there is one, else the default for the size.
// `apply` is true when the view must (re)load `text` into the dock area.
class DockLayout {
 public:
  struct Step {
    bool apply = false;
    std::string text;
  };
  // Each frame with the area's size and the stored arrangement ("" = none).
  Step frame(float width, float height, const std::string& stored, const DockMetrics& m = {});
  // The player moved or resized a panel (the view stores the area's description): it is on screen.
  void changed() { custom_shown_ = true; }
  // "Reset layout": the next frame loads the default again.
  void reset();

 private:
  float width_ = 0, height_ = 0;
  bool custom_shown_ = false;
  bool loaded_ = false;
};

}  // namespace tradelab::desktop::game
