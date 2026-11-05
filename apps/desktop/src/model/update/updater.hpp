// Desktop self-update (spec desktop-client "Updates", design "Updater"), in three steps that only the
// player's click connects:
//   check()            fetch manifest + signature, verify (manifest.hpp), pick a newer release for this
//                      platform. Nothing is downloaded.
//   download(release)  after the click: the payload to <staging>/<version>.part, checked for size and
//                      SHA-256, renamed to <staging>/<file>; then schedule() writes <staging>/pending.json.
//   apply_pending()    at the next start, before anything else: the staged file is checked again and moved
//                      over the installed copy by rename; the caller execs the new executable.
// Any failure leaves the installed version untouched and running. Signing keys and notarization are
// human-held (tasks 1.21); a build without a release public key has updates off.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "model/packs/http.hpp"
#include "model/update/manifest.hpp"

namespace tradelab::desktop::update {

struct Config {
  std::string manifest_url;
  PublicKey public_key{};
  std::string current_version;
  std::string platform = host_platform();
};

enum class CheckStatus : std::uint8_t { UpToDate, Available, Refused, Unavailable };

struct CheckResult {
  CheckStatus status = CheckStatus::Unavailable;
  std::optional<Release> release;  // Available
  std::string reason;              // Refused: manifest_error_name / "url"; Unavailable: transport detail
};

enum class DownloadError : std::uint8_t { None, Unavailable, SizeMismatch, HashMismatch, Storage, Url };
std::string_view download_error_name(DownloadError e);

struct Staged {
  DownloadError error = DownloadError::None;
  std::filesystem::path file;  // set when error == None
};

// What apply_pending replaces: the AppImage file itself, or the whole TradeLab.app bundle (the payload is
// a zip of it, unpacked with /usr/bin/ditto).
enum class TargetKind : std::uint8_t { File, AppBundle };

struct Target {
  TargetKind kind = TargetKind::File;
  std::filesystem::path path;  // the AppImage, or the .app folder
};

// The installed copy of the running `executable`: $APPIMAGE on Linux, the enclosing .app on macOS;
// nullopt for a dev build (updates off).
std::optional<Target> installed_target(const std::filesystem::path& executable);

class Updater {
 public:
  Updater(Config config, packs::HttpClient& http, std::filesystem::path staging_dir);

  CheckResult check();
  Staged download(const Release& release);
  // The player's click: the staged payload becomes the pending install of the next start.
  bool schedule(const Release& release, const Staged& staged, const Target& target);

  [[nodiscard]] const std::filesystem::path& staging_dir() const { return staging_; }

 private:
  Config config_;
  packs::HttpClient& http_;
  std::filesystem::path staging_;
};

struct Applied {
  bool applied = false;
  std::string version;
  std::filesystem::path executable;  // what to exec when applied
  std::string error;                 // a pending install that was refused (and dropped)
};

// At start-up. No pending install: applied == false, error empty.
Applied apply_pending(const std::filesystem::path& staging_dir);

}  // namespace tradelab::desktop::update
