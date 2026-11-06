// Settings (the web menu's settings block + ShortcutSettings.svelte; desktop additions: the CRT
// effects one by one, text scale and the bundled fonts' licenses).
#include <cmath>

#include "model/content.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

std::string action_label(const AppViewModel& vm, const std::string& action) {
  return vm.tr(action == "mute" ? "help.sound" : "help." + action);  // ShortcutSettings ACTION_LABEL
}

void crt(const AppViewModel& vm, ScreenModel& m) {
  const Settings& s = vm.settings();
  const bool reduced = vm.shell().reduce_motion;
  m.text(names::kSettingsCrt, vm.tr("settings.crt"));
  m.text(names::kSettingsScanlines, vm.tr("desktop.crt.scanlines"));
  m.text(names::kSettingsBloom, vm.tr("desktop.crt.bloom"));
  m.text(names::kSettingsNoise, vm.tr("desktop.crt.noise"));
  m.check(names::kSettingsCrt, s.crt.enabled);
  m.check(names::kSettingsScanlines, s.crt.scanlines);
  m.check(names::kSettingsBloom, s.crt.bloom);
  m.check(names::kSettingsNoise, s.crt.noise);
  for (auto w : {names::kSettingsScanlines, names::kSettingsBloom, names::kSettingsNoise}) m.enable(w, s.crt.enabled && !reduced);
  m.enable(names::kSettingsCrt, !reduced);
  m.show(names::kSettingsCrtNote, reduced);
  m.text(names::kSettingsCrtNote, vm.tr("desktop.crt.reducedMotion"));
}

// The web menu's sound block: on/off, music and effects volumes (stored with the terminal's prefs).
void sound(const AppViewModel& vm, ScreenModel& m) {
  const AudioPrefs& a = vm.game().prefs().audio;
  m.text(names::kSettingsSound, vm.tr("sound.on"));
  m.check(names::kSettingsSound, !a.muted);
  const auto pct = [&](double v) { return fmt::fmt_num(std::round(v * 100.0), vm.locale(), 0) + "%"; };
  m.text(names::kSettingsMusicLabel, vm.tr("sound.music") + " " + pct(a.music));
  m.text(names::kSettingsSfxLabel, vm.tr("sound.effects") + " " + pct(a.sfx));
  m.name(names::kSettingsMusic, vm.tr("sound.music"));
  m.name(names::kSettingsSfx, vm.tr("sound.effects"));
}

void keys(const AppViewModel& vm, ScreenModel& m) {
  const ShellState& s = vm.shell();
  m.text(names::kSettingsKeysTitle, vm.tr("keys.title"));
  WidgetTable t{names::kSettingsKeys, {vm.tr("help.title"), vm.tr("desktop.keys.colKey")}, {}};
  for (const auto& action : web_content().shortcut_actions) {
    const auto it = s.keymap.find(action);
    t.rows.push_back({action_label(vm, action), key_label(it == s.keymap.end() ? std::string() : it->second)});
  }
  m.tables.push_back(std::move(t));
  m.name(names::kSettingsKeys, vm.tr("keys.title"));
  m.text(names::kSettingsKeyChange, vm.tr(s.capturing_key ? "keys.press" : "keys.change"));
  m.name(names::kSettingsKeyChange,
         vm.tr("keys.change") + ": " + action_label(vm, web_content().shortcut_actions.at(static_cast<std::size_t>(s.key_row))));
  m.text(names::kSettingsKeyReset, vm.tr("keys.reset"));
  m.text(names::kSettingsKeyMessage, s.key_message);
  m.show(names::kSettingsKeyMessage, !s.key_message.empty());
}

}  // namespace

ScreenModel settings(const AppViewModel& vm) {
  ScreenModel m;
  const Settings& s = vm.settings();
  m.text(names::kSettingsTitle, vm.tr("menu.settings"));
  m.text(names::kSettingsBack, vm.tr("debrief.menu"));
  m.text(names::kSettingsLanguage, vm.tr("settings.language"));
  m.text(names::kSettingsLangPt, std::string(vm.locale() == Locale::PtBR ? "\xE2\x97\x8F " : "") + "PT-BR");  // ●
  m.text(names::kSettingsLangEn, std::string(vm.locale() == Locale::En ? "\xE2\x97\x8F " : "") + "EN");
  m.name(names::kSettingsLangPt, vm.tr("settings.language") + ": Português (Brasil)");
  m.name(names::kSettingsLangEn, vm.tr("settings.language") + ": English");
  crt(vm, m);
  m.text(names::kSettingsTextScaleLabel,
         vm.tr("desktop.textScale", {{"pct", fmt::fmt_num(std::round(s.text_scale * 100.0f), vm.locale(), 0) + "%"}}));
  m.name(names::kSettingsTextScale, vm.tr("desktop.textScale", {{"pct", ""}}));
  m.text(names::kSettingsPalette, vm.tr("settings.palette"));
  m.check(names::kSettingsPalette, s.colorblind);
  sound(vm, m);
  keys(vm, m);
  m.text(names::kSettingsAbout, vm.tr("desktop.about.fonts"));
  return m;
}

}  // namespace tradelab::desktop::screens
