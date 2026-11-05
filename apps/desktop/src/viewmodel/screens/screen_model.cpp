#include "viewmodel/screen_model.hpp"

#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop {

std::string_view tone_class(Tone tone) {
  switch (tone) {
    case Tone::Normal: return "";
    case Tone::Muted: return "muted";
    case Tone::Up: return "up";
    case Tone::Down: return "down";
    case Tone::Attention: return "attention";
    case Tone::Info: return "info";
    case Tone::Accent: return "accent";
    case Tone::Title: return "title";
  }
  return "";
}

std::string ScreenModel::text_of(std::string_view w) const {
  for (auto it = texts.rbegin(); it != texts.rend(); ++it) {
    if (it->widget == w) return it->text;
  }
  return {};
}

const WidgetLines* ScreenModel::lines_of(std::string_view w) const {
  for (const auto& l : lines) {
    if (l.widget == w) return &l;
  }
  return nullptr;
}

const WidgetTable* ScreenModel::table_of(std::string_view w) const {
  for (const auto& t : tables) {
    if (t.widget == w) return &t;
  }
  return nullptr;
}

bool ScreenModel::is_visible(std::string_view w, bool fallback) const {
  for (auto it = visible.rbegin(); it != visible.rend(); ++it) {
    if (it->widget == w) return it->on;
  }
  return fallback;
}

ScreenModel AppViewModel::screen_model() const {
  switch (screen_) {
    case Screen::Menu: return screens::menu(*this);
    case Screen::Briefing: return screens::briefing(*this);
    case Screen::Debrief: return screens::debrief(*this);
    case Screen::Curriculum: return screens::curriculum(*this);
    case Screen::Journal: return screens::journal(*this);
    case Screen::Lesson: return screens::lesson(*this);
    case Screen::Settings: return screens::settings(*this);
    case Screen::Game: break;
  }
  ScreenModel m;
  for (auto& b : bindings()) m.text(b.widget, std::move(b.text));
  return m;
}

namespace screens {

std::string mission_title(const AppViewModel& vm, const std::string& id) { return vm.tr("mission." + id + ".title"); }

}  // namespace screens

}  // namespace tradelab::desktop
