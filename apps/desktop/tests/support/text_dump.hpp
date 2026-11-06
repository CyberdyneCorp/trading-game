// What a player reads on a screen, for reports and checks: every visible text in tree order (labels,
// buttons, fields, table cells) and the widgets whose text runs past the right edge of the panel that
// holds them (clipped when the panel clips, as Raven's scroll areas do).
#pragma once

#include <raven/raven_controls.h>
#include <raven/raven_widget.h>

#include <algorithm>
#include <sstream>
#include <string>

#include "view/raven/raven_ui.hpp"

namespace tradelab::desktop::testing {

inline bool shown(Rid w) { return Rwidget_get_visibility(w) == R_VISIBLE; }

inline void dump_table(Rid table, std::ostringstream& out) {
  for (int r = 0; r < Rtable_get_row_count(table); ++r) {
    out << "  |";
    for (int c = 0; c < Rtable_get_column_count(table); ++c) {
      const char* cell = Rtable_get_cell(table, r, c);
      out << " " << (cell != nullptr ? cell : "") << " |";
    }
    out << "\n";
  }
}

inline void dump_into(Rid w, std::ostringstream& out) {
  if (w == R_INVALID_ID || !shown(w)) return;
  if (Rwidget_get_type(w) == R_WIDGET_TABLE) return dump_table(w, out);
  const std::string text = ui::text_of(w);
  if (!text.empty()) out << text << "\n";
  for (int i = 0; i < Rwidget_get_child_count(w); ++i) dump_into(Rwidget_get_child(w, i), out);
}

inline std::string dump_texts(Rid root) {
  std::ostringstream out;
  dump_into(root, out);
  return out.str();
}

struct Box {
  float x = 0, y = 0, w = 0, h = 0;
};

inline Box rect_of(Rid w) {
  Box b;
  Rwidget_get_rect(w, &b.x, &b.y, &b.w, &b.h);
  return b;
}

// The part of `b` inside `clip` (horizontally: what matters for a text cut on its right).
inline Box clip_to(Box b, Box clip) {
  if (b.w <= 0) return clip;
  const float right = std::min(b.x + b.w, clip.x + clip.w);
  b.x = std::max(b.x, clip.x);
  b.w = std::max(0.0F, right - b.x);
  return b;
}

// The right edge of what the widget draws: a label's text may run past its own box.
inline float drawn_right(Rid w, const Box& b) {
  if (Rwidget_get_type(w) != R_WIDGET_LABEL) return b.x + b.w;
  float want_w = 0;
  float want_h = 0;
  Rwidget_get_desired_size(w, &want_w, &want_h);
  return b.x + std::max(b.w, want_w);
}

// A label narrower than its text that was given one line: what wraps falls below its box, unseen.
// A button given less width than its text and padding need: the button clips its text.
inline bool squeezed(Rid w, const Box& b) {
  const RWidgetType type = Rwidget_get_type(w);
  if (type != R_WIDGET_LABEL && type != R_WIDGET_BUTTON) return false;
  float want_w = 0;
  float want_h = 0;
  Rwidget_get_desired_size(w, &want_w, &want_h);
  if (type == R_WIDGET_BUTTON) return want_w > b.w + 1.0F;
  return want_w > b.w + 4.0F && b.h < want_h * 1.5F;
}

// A visible text whose right end passes the visible part of the panels that hold it, or squeezed.
inline void overflow_into(Rid w, Box clip, std::ostringstream& out) {
  if (w == R_INVALID_ID || !shown(w)) return;
  const Box b = rect_of(w);
  const std::string text = ui::text_of(w);
  const float over = squeezed(w, b) ? 4.0F : drawn_right(w, b) - (clip.x + clip.w);
  if (!text.empty() && b.w > 0 && clip.w > 0 && over > 1.0F) {
    const char* name = Rwidget_get_name(w);
    out << "  [" << (name != nullptr ? name : "") << "] +" << static_cast<int>(over) << "px: " << text << "\n";
  }
  const Box inner = clip_to(b, clip);
  for (int i = 0; i < Rwidget_get_child_count(w); ++i) overflow_into(Rwidget_get_child(w, i), inner, out);
}

inline std::string overflow_report(Rid root) {
  std::ostringstream out;
  overflow_into(root, rect_of(root), out);
  return out.str();
}

}  // namespace tradelab::desktop::testing
