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

bool ScreenModel::is_enabled(std::string_view w, bool fallback) const {
  for (auto it = enabled.rbegin(); it != enabled.rend(); ++it) {
    if (it->widget == w) return it->on;
  }
  return fallback;
}

std::string ScreenModel::tip_of(std::string_view w) const {
  for (auto it = tips.rbegin(); it != tips.rend(); ++it) {
    if (it->widget == w) return it->text;
  }
  return {};
}

const WidgetItems* ScreenModel::items_of(std::string_view w) const {
  for (const auto& i : items) {
    if (i.widget == w) return &i;
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
    case Screen::Tutorial: return screens::tutorial(*this);
    case Screen::Catalog: return screens::catalog(*this);
    case Screen::Import: return screens::csv_import(*this);
    case Screen::Academy: return screens::academy(*this);
    case Screen::Glossary: return screens::glossary(*this);
    case Screen::Stats: return screens::stats(*this);
    case Screen::Game: return screens::game(*this);
    case Screen::Lab: break;  // view/screens/lab_screen.cpp reads the lab viewmodel itself
  }
  ScreenModel m;
  for (auto& b : bindings()) m.text(b.widget, std::move(b.text));
  return m;
}

namespace screens {

// A built-in mission's title, else the label of the installed pack it plays, else its id.
std::string mission_title(const AppViewModel& vm, const std::string& id) {
  const std::string key = "mission." + id + ".title";
  if (vm.catalog().has(key)) return vm.tr(key);
  const std::string label = vm.pack_label_of(id);
  return label.empty() ? id : label;
}

}  // namespace screens

}  // namespace tradelab::desktop
