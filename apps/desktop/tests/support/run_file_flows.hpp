// Run files played like a player (add-run-sharing-and-journal-stats): a tampered file refused from the
// journal, the web-checked shared file imported (cross-client), watched bar by bar on the docked
// terminal and debriefed without the player's controls, then an own run exported and read back.
// Shared by the headless tests (null platform) and the windowed tests (Vulkan, with captures).
#pragma once

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "model/run_files.hpp"
#include "support/shell_flows.hpp"
#include "viewmodel/run_file_names.hpp"

namespace tradelab::desktop::testing {

inline std::string runfile_path(const std::string& name) { return std::string(TRADELAB_RUNFILES_DIR) + "/" + name; }

inline std::string read_text(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

inline void import_path(App& app, Driver& d, const std::string& path) {
  d.type_into(names::kFilesPath, path);
  REQUIRE(app.vm().run_files().path == path);
  const auto before = app.vm().revision();
  REQUIRE(d.click(names::kFilesImport));
  REQUIRE(d.until([&] { return app.vm().revision() != before && !app.vm().run_files().message.empty(); }));
  d.frames(2);
}

inline void flow_run_files(App& app, Driver& d, const std::string& expected_digest, const Shot& shot = {}) {
  REQUIRE(d.click(names::kMenuJournal));
  REQUIRE(app.vm().screen() == Screen::Journal);
  import_path(app, d, runfile_path("tampered-digest.tlrun"));
  CHECK(d.text(names::kFilesMessage).find("não chega ao resultado gravado") != std::string::npos);
  CHECK(app.vm().run_files().imports.empty());
  import_path(app, d, runfile_path("m01-finished.tlrun"));
  REQUIRE(app.vm().run_files().imports.size() == 1);
  CHECK(d.text(names::kFilesMessage).find("verificada pelo motor") != std::string::npos);
  if (shot) shot("run-files-journal");

  REQUIRE(d.click(names::kImportWatch));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Game; }));
  CHECK(app.vm().viewing());
  d.frames(2);
  CHECK(shown_text(app, names::kGameReplayTag).find("Barra 0 de") != std::string::npos);
  d.focus(names::kGameAdvance);
  d.key(R_KEY_SPACE);
  REQUIRE(d.idle());
  REQUIRE(d.until([&] { return app.vm().snapshot() != nullptr && app.vm().snapshot()->watch_bar == 1; }));
  CHECK(shown_text(app, names::kGameReplayTag).find("Barra 1 de") != std::string::npos);
  if (shot) shot("replay-terminal");
  for (int guard = 0; guard < 200 && app.vm().screen() == Screen::Game; ++guard) {
    d.focus(names::kGameAdvance);
    d.key(R_KEY_SPACE, R_MOD_SHIFT);
    REQUIRE(d.idle());
  }
  REQUIRE(app.vm().screen() == Screen::Debrief);
  CHECK(app.vm().snapshot()->digest == expected_digest);  // the recorded run, bar for bar
  d.frames(2);
  CHECK(shown(d, names::kDebriefViewing));
  CHECK_FALSE(shown(d, names::kDebriefAgain));
  CHECK(shown_text(app, names::kDebriefMenu) == "Sair do replay");
  if (shot) shot("replay-debrief");
  REQUIRE(d.click(names::kDebriefMenu));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Journal; }));
  CHECK_FALSE(app.vm().viewing());
  CHECK(app.vm().progress().completed.empty());  // an imported run never counts
  CHECK(app.vm().progress().runs.empty());
}

// An own run played to its debrief, then exported from the journal into `folder` and verified by the
// engine (and the same run watched again from the journal).
inline void flow_export_own(App& app, Driver& d, Terminal& t, const std::filesystem::path& folder) {
  t.open(0);
  play_to_debrief(app, d, t);
  REQUIRE(d.click(names::kDebriefMenu));
  REQUIRE(d.click(names::kMenuJournal));
  d.type_into(names::kFilesPath, folder.string());
  const auto before = app.vm().revision();
  REQUIRE(d.click(names::kFilesExport));
  REQUIRE(d.until([&] { return app.vm().revision() != before && !app.vm().run_files().message.empty(); }));
  CHECK(app.vm().run_files().message_ok);
  std::filesystem::path file;
  for (const auto& e : std::filesystem::directory_iterator(folder)) file = e.path();
  REQUIRE(file.extension() == ".tlrun");
  CHECK(verify_run_file(read_text(file))["ok"].as_bool());
  REQUIRE(d.click(names::kFilesWatch));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Game && app.vm().viewing(); }));
  REQUIRE(d.click(names::kGameReplayLeave));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Journal; }));
}

}  // namespace tradelab::desktop::testing
