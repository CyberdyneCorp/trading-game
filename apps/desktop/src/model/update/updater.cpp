#include "model/update/updater.hpp"

#include <sys/stat.h>

#include <cstdlib>
#include <system_error>
#include <utility>
#include <vector>

#include "model/json.hpp"
#include "model/packs/files.hpp"
#include "model/storage/json_writer.hpp"

#if defined(__APPLE__)
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace tradelab::desktop::update {

namespace fs = std::filesystem;

namespace {

constexpr const char* kPending = "pending.json";

std::string transport_detail(const packs::HttpResponse& r) {
  return r.status == 0 ? (r.error.empty() ? std::string("offline") : r.error) : "http " + std::to_string(r.status);
}

std::string_view as_text(const std::vector<std::uint8_t>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// "https://host/a/TradeLab-x86_64.AppImage?x=1" -> "TradeLab-x86_64.AppImage"
std::string file_name_of(const std::string& url) {
  std::string path = url.substr(0, url.find_first_of("?#"));
  std::string name = path.substr(path.find_last_of('/') + 1);
  return name.empty() || name == "." || name == ".." ? std::string("payload") : name;
}

bool same_hash(const fs::path& file, const std::string& sha256) {
  const auto bytes = packs::read_file(file);
  return bytes && packs::sha256_hex(*bytes) == sha256;
}

// The staged AppImage replaces the installed one: copied next to it (the staging folder may be on another
// file system), made executable, then renamed over it. The running process keeps the old inode.
bool replace_file(const fs::path& staged, const fs::path& target) {
  const fs::path tmp = target.parent_path() / (".tradelab-update-" + target.filename().string());
  std::error_code ec;
  fs::copy_file(staged, tmp, fs::copy_options::overwrite_existing, ec);
  if (ec || ::chmod(tmp.c_str(), 0755) != 0) return false;
  fs::rename(tmp, target, ec);
  if (ec) fs::remove(tmp, ec);
  return !ec;
}

#if defined(__APPLE__)
bool unzip(const fs::path& zip, const fs::path& into) {
  const std::string z = zip.string();
  const std::string d = into.string();
  const char* argv[] = {"/usr/bin/ditto", "-x", "-k", z.c_str(), d.c_str(), nullptr};
  pid_t pid = 0;
  if (posix_spawn(&pid, argv[0], nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0) return false;
  int status = 0;
  return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#else
bool unzip(const fs::path& /*zip*/, const fs::path& /*into*/) { return false; }
#endif

// The zip holds TradeLab.app; the new bundle takes the old one's place by rename (old kept until then).
bool replace_bundle(const fs::path& staged, const fs::path& bundle) {
  const fs::path work = bundle.parent_path() / ".TradeLab.update";
  const fs::path old = bundle.parent_path() / ".TradeLab.app.old";
  std::error_code ec;
  fs::remove_all(work, ec);
  fs::remove_all(old, ec);
  const fs::path fresh = work / "TradeLab.app";
  if (!unzip(staged, work) || !fs::exists(fresh / "Contents/MacOS/tradelab", ec)) {
    fs::remove_all(work, ec);
    return false;
  }
  fs::rename(bundle, old, ec);
  if (ec) return false;
  fs::rename(fresh, bundle, ec);
  if (ec) {
    fs::rename(old, bundle, ec);  // put the installed version back
    return false;
  }
  fs::remove_all(old, ec);
  fs::remove_all(work, ec);
  return true;
}

struct Pending {
  std::string version;
  fs::path file;
  std::string sha256;
  Target target;
};

std::optional<Pending> read_pending(const fs::path& path) {
  const auto bytes = packs::read_file(path);
  if (!bytes) return std::nullopt;
  const auto v = json::parse(as_text(*bytes));
  if (!v) return Pending{};  // damaged: dropped by the caller
  Pending p;
  p.version = (*v)["version"].as_string();
  p.file = (*v)["file"].as_string();
  p.sha256 = (*v)["sha256"].as_string();
  p.target.kind = (*v)["kind"].as_string() == "app" ? TargetKind::AppBundle : TargetKind::File;
  p.target.path = (*v)["target"].as_string();
  return p;
}

}  // namespace

std::string_view download_error_name(DownloadError e) {
  switch (e) {
    case DownloadError::None: return "none";
    case DownloadError::Unavailable: return "unavailable";
    case DownloadError::SizeMismatch: return "size_mismatch";
    case DownloadError::HashMismatch: return "hash_mismatch";
    case DownloadError::Storage: return "storage";
    case DownloadError::Url: return "url";
  }
  return "unavailable";
}

std::optional<Target> installed_target(const fs::path& executable) {
#if defined(__APPLE__)
  const fs::path macos = executable.parent_path();
  const fs::path bundle = macos.parent_path().parent_path();
  if (macos.filename() != "MacOS" || bundle.extension() != ".app") return std::nullopt;
  return Target{TargetKind::AppBundle, bundle};
#else
  (void)executable;
  const char* image = std::getenv("APPIMAGE");  // set by the AppImage runtime
  if (image == nullptr || *image == '\0') return std::nullopt;
  return Target{TargetKind::File, fs::path(image)};
#endif
}

Updater::Updater(Config config, packs::HttpClient& http, fs::path staging_dir)
    : config_(std::move(config)), http_(http), staging_(std::move(staging_dir)) {}

CheckResult Updater::check() {
  if (!allowed_url(config_.manifest_url)) return {CheckStatus::Refused, std::nullopt, "url"};
  const packs::HttpResponse manifest = http_.get(config_.manifest_url, std::nullopt);
  if (manifest.status != 200) return {CheckStatus::Unavailable, std::nullopt, transport_detail(manifest)};
  const packs::HttpResponse sig = http_.get(config_.manifest_url + ".sig", std::nullopt);
  if (sig.status != 200) return {CheckStatus::Unavailable, std::nullopt, transport_detail(sig)};
  const ManifestCheck m = verify_manifest(manifest.body, as_text(sig.body), config_.public_key);
  if (m.error != ManifestError::None) return {CheckStatus::Refused, std::nullopt, std::string(manifest_error_name(m.error))};
  auto release = pick_release(m.manifest, config_.platform, config_.current_version);
  if (!release) return {CheckStatus::UpToDate, std::nullopt, {}};
  return {CheckStatus::Available, std::move(release), {}};
}

Staged Updater::download(const Release& release) {
  if (!allowed_url(release.url)) return {DownloadError::Url, {}};
  const packs::HttpResponse r = http_.get(release.url, std::nullopt);
  if (r.status != 200) return {DownloadError::Unavailable, {}};
  if (static_cast<std::int64_t>(r.body.size()) != release.size) return {DownloadError::SizeMismatch, {}};
  if (packs::sha256_hex(r.body) != release.sha256) return {DownloadError::HashMismatch, {}};
  std::error_code ec;
  fs::create_directories(staging_, ec);
  const fs::path file = staging_ / (release.version + "-" + file_name_of(release.url));
  if (!packs::write_file_atomic(file, r.body)) return {DownloadError::Storage, {}};
  return {DownloadError::None, file};
}

bool Updater::schedule(const Release& release, const Staged& staged, const Target& target) {
  if (staged.error != DownloadError::None || staged.file.empty()) return false;
  storage::JsonWriter w;
  w.begin_object().field("version", release.version).field("file", staged.file.string());
  w.field("sha256", release.sha256).field("kind", target.kind == TargetKind::AppBundle ? "app" : "file");
  w.field("target", target.path.string());
  const std::string& text = w.end_object().text();
  const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  return packs::write_file_atomic(staging_ / kPending, bytes);
}

Applied apply_pending(const fs::path& staging_dir) {
  const fs::path marker = staging_dir / kPending;
  const auto pending = read_pending(marker);
  if (!pending) return {};
  std::error_code ec;
  fs::remove(marker, ec);  // once: a failed install is not retried at every start
  Applied out;
  out.version = pending->version;
  if (pending->file.empty() || pending->target.path.empty()) {
    out.error = "damaged";
  } else if (!same_hash(pending->file, pending->sha256)) {
    out.error = "hash_mismatch";
  } else {
    const bool bundle = pending->target.kind == TargetKind::AppBundle;
    out.applied = bundle ? replace_bundle(pending->file, pending->target.path) : replace_file(pending->file, pending->target.path);
    if (!out.applied) out.error = "replace_failed";
    out.executable = bundle ? pending->target.path / "Contents/MacOS/tradelab" : pending->target.path;
  }
  fs::remove(pending->file, ec);
  return out;
}

}  // namespace tradelab::desktop::update
