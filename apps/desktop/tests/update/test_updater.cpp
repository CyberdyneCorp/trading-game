// Updater (spec desktop-client "Updates"): a manifest is trusted only with a valid Ed25519 signature from
// the release key; tampered bytes, another key, a wrong size, a wrong hash and a truncated download are all
// refused with the installed version untouched; nothing is installed until the player's click scheduled
// it, and then only at the next start, by rename. A run that started before an update resumes on the new
// engine through the log (storage tests: "engine pinning").
#include <doctest/doctest.h>

#include <tradelab/ed25519.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "model/packs/files.hpp"
#include "model/storage/json_writer.hpp"
#include "model/update/update_service.hpp"
#include "model/update/updater.hpp"
#include "support/storage_fixtures.hpp"
#include "support/ui_queue.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/screen_names.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::update;
using tradelab::desktop::testing::TempDir;
using tradelab::desktop::testing::UiQueue;
namespace fs = std::filesystem;

namespace {

constexpr const char* kManifestUrl = "https://updates.example/desktop-manifest.json";
constexpr const char* kPayloadUrl = "https://updates.example/TradeLab-x86_64.AppImage";

tradelab::KeyPair keys(std::uint8_t fill) {
  tradelab::SecretSeed seed{};
  seed.fill(fill);
  return tradelab::ed25519_keypair(seed);
}

std::vector<std::uint8_t> bytes_of(const std::string& s) { return {s.begin(), s.end()}; }

std::string hex(std::span<const std::uint8_t> b) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  for (std::uint8_t v : b) {
    out += kDigits[v >> 4];
    out += kDigits[v & 15];
  }
  return out;
}

std::string manifest_text(const std::string& version, const std::vector<std::uint8_t>& payload,
                          const std::string& platform = "linux-x86_64") {
  storage::JsonWriter w;
  w.begin_object().field("format", 1).field("product", "tradelab-desktop").key("releases").begin_array();
  w.begin_object().field("version", version).field("platform", platform).field("url", kPayloadUrl);
  w.field("size", static_cast<std::int64_t>(payload.size())).field("sha256", packs::sha256_hex(payload));
  w.key("notes").begin_object().field("pt-BR", "Correções.").field("en", "Fixes.").end_object();
  w.end_object().end_array();
  return w.end_object().text();
}

std::string sign(const std::string& text, const tradelab::KeyPair& k) { return hex(tradelab::ed25519_sign(k, bytes_of(text))); }

class FakeHttp final : public packs::HttpClient {
 public:
  std::map<std::string, std::vector<std::uint8_t>> files;
  std::vector<std::string> requested;
  packs::HttpResponse get(const std::string& url, const std::optional<packs::ByteRange>& /*range*/) override {
    requested.push_back(url);
    const auto it = files.find(url);
    if (it == files.end()) return packs::HttpResponse{0, {}, {}, "offline"};
    return packs::HttpResponse{200, {}, it->second, {}};
  }
};

struct Fixture {
  TempDir dir;
  tradelab::KeyPair release_key = keys(7);
  std::vector<std::uint8_t> payload = bytes_of("new AppImage 0.2.0");
  FakeHttp http;
  Config config;
  Fixture() {
    config.manifest_url = kManifestUrl;
    config.public_key = release_key.public_key;
    config.current_version = "0.1.0";
    config.platform = "linux-x86_64";
    publish(manifest_text("0.2.0", payload), release_key);
    http.files[kPayloadUrl] = payload;
  }
  void publish(const std::string& manifest, const tradelab::KeyPair& k) {
    http.files[kManifestUrl] = bytes_of(manifest);
    http.files[std::string(kManifestUrl) + ".sig"] = bytes_of(sign(manifest, k));
  }
  Updater updater() { return Updater(config, http, dir.path() / "updates"); }
  // The installed copy (an AppImage stand-in) the update replaces.
  Target installed() {
    const fs::path app = dir.path() / "TradeLab.AppImage";
    REQUIRE(packs::write_file_atomic(app, bytes_of("old AppImage 0.1.0")));
    return Target{TargetKind::File, app};
  }
};

std::string read_text(const fs::path& p) {
  const auto b = packs::read_file(p);
  return b ? std::string(b->begin(), b->end()) : std::string();
}

}  // namespace

TEST_CASE("update manifest: only a valid signature from the release key is trusted") {
  Fixture f;
  const std::string text = manifest_text("0.2.0", f.payload);
  const auto ok = verify_manifest(bytes_of(text), sign(text, f.release_key), f.release_key.public_key);
  REQUIRE(ok.error == ManifestError::None);
  REQUIRE(ok.manifest.releases.size() == 1);
  CHECK(ok.manifest.releases[0].notes_pt == "Correções.");

  std::string tampered = text;
  tampered[tampered.find("0.2.0")] = '9';  // one byte changed after signing
  CHECK(verify_manifest(bytes_of(tampered), sign(text, f.release_key), f.release_key.public_key).error ==
        ManifestError::BadSignature);
  CHECK(verify_manifest(bytes_of(text), sign(text, keys(9)), f.release_key.public_key).error == ManifestError::BadSignature);
  CHECK(verify_manifest(bytes_of(text), "zz", f.release_key.public_key).error == ManifestError::BadSignature);
  const std::string other = R"({"format":2,"product":"tradelab-desktop","releases":[]})";
  CHECK(verify_manifest(bytes_of(other), sign(other, f.release_key), f.release_key.public_key).error ==
        ManifestError::UnsupportedFormat);
  const std::string http_url = R"({"format":1,"product":"tradelab-desktop","releases":[{"version":"0.2.0",)"
                               R"("platform":"macos","url":"http://evil.example/x.zip","size":1,"sha256":")" +
                               std::string(64, 'a') + R"("}]})";
  CHECK(verify_manifest(bytes_of(http_url), sign(http_url, f.release_key), f.release_key.public_key).error ==
        ManifestError::Malformed);
}

TEST_CASE("update versions and platforms: only a newer release for this platform is offered") {
  CHECK(version_newer("0.10.0", "0.9.9"));
  CHECK(version_newer("1.0.0", "0.99.99"));
  CHECK_FALSE(version_newer("0.1.0", "0.1.0"));
  CHECK_FALSE(version_newer("0.1", "0.0.1"));
  CHECK_FALSE(version_newer("0.2.0-rc1", "0.1.0"));
  Manifest m;
  m.releases.push_back(Release{"0.3.0", "macos", "https://x/a.zip", 1, std::string(64, 'a'), {}, {}});
  m.releases.push_back(Release{"0.2.0", "linux-x86_64", "https://x/a", 1, std::string(64, 'a'), {}, {}});
  m.releases.push_back(Release{"0.2.5", "linux-x86_64", "https://x/b", 1, std::string(64, 'a'), {}, {}});
  CHECK(pick_release(m, "linux-x86_64", "0.1.0")->version == "0.2.5");
  CHECK_FALSE(pick_release(m, "linux-x86_64", "0.2.5"));
  CHECK_FALSE(pick_release(m, "linux-aarch64", "0.1.0"));
  CHECK(allowed_url("http://127.0.0.1:8080/m.json"));
  CHECK_FALSE(allowed_url("http://example.com/m.json"));
  CHECK_FALSE(allowed_url("file:///etc/passwd"));
}

TEST_CASE("updater check: offline, bad signature and up to date are not offers") {
  Fixture f;
  CHECK(f.updater().check().status == CheckStatus::Available);
  f.publish(manifest_text("0.2.0", f.payload), keys(9));  // signed with another key
  const CheckResult refused = f.updater().check();
  CHECK(refused.status == CheckStatus::Refused);
  CHECK(refused.reason == "bad_signature");
  CHECK_FALSE(refused.release);
  f.publish(manifest_text("0.1.0", f.payload), f.release_key);
  CHECK(f.updater().check().status == CheckStatus::UpToDate);
  f.http.files.clear();
  CHECK(f.updater().check().status == CheckStatus::Unavailable);
  CHECK(f.http.requested.back() == kManifestUrl);  // never asked for the payload
}

TEST_CASE("updater download: wrong size, wrong hash and truncated payloads are refused; nothing is pending") {
  Fixture f;
  Updater u = f.updater();
  const Release release = *u.check().release;
  f.http.files[kPayloadUrl] = bytes_of("new AppImage 0.2.0 plus junk");
  CHECK(u.download(release).error == DownloadError::SizeMismatch);
  f.http.files[kPayloadUrl] = bytes_of("new AppImage 0.2.");  // truncated
  CHECK(u.download(release).error == DownloadError::SizeMismatch);
  std::vector<std::uint8_t> tampered = f.payload;
  tampered[0] ^= 1;
  f.http.files[kPayloadUrl] = tampered;
  CHECK(u.download(release).error == DownloadError::HashMismatch);
  f.http.files.erase(kPayloadUrl);
  CHECK(u.download(release).error == DownloadError::Unavailable);
  f.http.files[kPayloadUrl] = f.payload;
  const Staged staged = u.download(release);
  REQUIRE(staged.error == DownloadError::None);
  CHECK(read_text(staged.file) == "new AppImage 0.2.0");
  // Downloaded is not installed: without the click's schedule() the next start changes nothing.
  const Target app = f.installed();
  CHECK_FALSE(apply_pending(u.staging_dir()).applied);
  CHECK(read_text(app.path) == "old AppImage 0.1.0");
}

TEST_CASE("updater install: scheduled by the click, applied at the next start by rename") {
  Fixture f;
  Updater u = f.updater();
  const Release release = *u.check().release;
  const Staged staged = u.download(release);
  const Target app = f.installed();
  REQUIRE(u.schedule(release, staged, app));
  const Applied applied = apply_pending(u.staging_dir());
  CHECK(applied.applied);
  CHECK(applied.version == "0.2.0");
  CHECK(applied.executable == app.path);
  CHECK(read_text(app.path) == "new AppImage 0.2.0");
  CHECK((fs::status(app.path).permissions() & fs::perms::owner_exec) != fs::perms::none);
  CHECK_FALSE(apply_pending(u.staging_dir()).applied);  // once
  CHECK_FALSE(fs::exists(staged.file));
}

TEST_CASE("updater install: a staged file changed after the click is refused and the installed copy stays") {
  Fixture f;
  Updater u = f.updater();
  const Release release = *u.check().release;
  const Staged staged = u.download(release);
  const Target app = f.installed();
  REQUIRE(u.schedule(release, staged, app));
  REQUIRE(packs::write_file_atomic(staged.file, bytes_of("evil AppImage 0.2.0")));
  const Applied applied = apply_pending(u.staging_dir());
  CHECK_FALSE(applied.applied);
  CHECK(applied.error == "hash_mismatch");
  CHECK(read_text(app.path) == "old AppImage 0.1.0");
  CHECK_FALSE(apply_pending(u.staging_dir()).applied);  // dropped, not retried at every start
}

#if defined(__APPLE__)
TEST_CASE("updater install (macOS): the zipped TradeLab.app replaces the installed bundle") {
  TempDir dir;
  const fs::path src = dir.path() / "src" / "TradeLab.app" / "Contents" / "MacOS";
  fs::create_directories(src);
  REQUIRE(packs::write_file_atomic(src / "tradelab", bytes_of("new bundle")));
  const fs::path zip = dir.path() / "TradeLab-macos.zip";
  const std::string cmd = "cd '" + (dir.path() / "src").string() + "' && /usr/bin/ditto -c -k --keepParent TradeLab.app '" +
                          zip.string() + "'";
  REQUIRE(std::system(cmd.c_str()) == 0);
  const auto payload = *packs::read_file(zip);

  const fs::path bundle = dir.path() / "Applications" / "TradeLab.app";
  fs::create_directories(bundle / "Contents" / "MacOS");
  REQUIRE(packs::write_file_atomic(bundle / "Contents/MacOS/tradelab", bytes_of("old bundle")));
  REQUIRE(installed_target(bundle / "Contents/MacOS/tradelab")->path == bundle);

  FakeHttp http;
  const tradelab::KeyPair k = keys(7);
  storage::JsonWriter w;
  w.begin_object().field("format", 1).field("product", "tradelab-desktop").key("releases").begin_array();
  w.begin_object().field("version", "0.2.0").field("platform", "macos").field("url", "https://u.example/TradeLab-macos.zip");
  w.field("size", static_cast<std::int64_t>(payload.size())).field("sha256", packs::sha256_hex(payload)).end_object();
  const std::string text = w.end_array().end_object().text();
  http.files[kManifestUrl] = bytes_of(text);
  http.files[std::string(kManifestUrl) + ".sig"] = bytes_of(sign(text, k));
  http.files["https://u.example/TradeLab-macos.zip"] = payload;
  Updater u(Config{kManifestUrl, k.public_key, "0.1.0", "macos"}, http, dir.path() / "updates");
  const Release release = *u.check().release;
  REQUIRE(u.schedule(release, u.download(release), Target{TargetKind::AppBundle, bundle}));
  const Applied applied = apply_pending(dir.path() / "updates");
  CHECK(applied.applied);
  CHECK(applied.executable == bundle / "Contents/MacOS/tradelab");
  CHECK(read_text(bundle / "Contents/MacOS/tradelab") == "new bundle");
  CHECK_FALSE(fs::exists(dir.path() / "Applications" / ".TradeLab.app.old"));
}
#endif

namespace {

struct FakeUpdates final : UpdateCommands {
  int installs = 0;
  void install() override { ++installs; }
};

struct NoEngine final : EngineCommands {
  void start(const std::string&, std::uint32_t) override {}
  void advance(int) override {}
  void market(Side, const std::string&) override {}
};

}  // namespace

TEST_CASE("update offer: shown in the menu, installed only by its button, never by itself") {
  NoEngine engine;
  AppViewModel vm(builtin_catalog(), engine_missions(), engine, [] { return 1u; });
  FakeUpdates updates;
  vm.attach_updates(&updates);
  ScreenModel m = vm.screen_model();
  CHECK_FALSE(m.is_visible(names::kMenuUpdateRow, true));
  vm.on_update(UpdateState{UpdateState::Status::Available, "0.2.0", "Correções.", "Fixes.", {}});
  CHECK(updates.installs == 0);  // an offer is not an install
  m = vm.screen_model();
  CHECK(m.is_visible(names::kMenuUpdateRow, false));
  CHECK(m.is_visible(names::kMenuUpdateInstall, false));
  CHECK(m.text_of(names::kMenuUpdateText) == "Nova versão disponível. Versão 0.2.0. Correções.");
  CHECK(m.text_of(names::kMenuUpdateInstall) == "Baixar e instalar ao reabrir");
  vm.open_settings();
  vm.install_update();  // not from another screen
  CHECK(updates.installs == 0);
  vm.back_to_menu();
  vm.install_update();
  CHECK(updates.installs == 1);
  vm.install_update();  // already on its way
  CHECK(updates.installs == 1);
  vm.on_update(UpdateState{UpdateState::Status::Scheduled, "0.2.0", {}, {}, {}});
  m = vm.screen_model();
  CHECK_FALSE(m.is_visible(names::kMenuUpdateInstall, true));
  CHECK(m.text_of(names::kMenuUpdateText) == "A versão 0.2.0 será instalada quando o TradeLab for reaberto.");
  vm.on_update(UpdateState{UpdateState::Status::Failed, "0.2.0", {}, {}, "hash_mismatch"});
  CHECK(vm.screen_model().text_of(names::kMenuUpdateText) ==
        "A atualização 0.2.0 foi recusada (hash_mismatch); esta versão continua instalada.");
}

TEST_CASE("update service: check reports on the UI thread; install downloads and schedules") {
  Fixture f;
  UiQueue ui;
  std::vector<UpdateState> seen;
  auto http = std::make_unique<FakeHttp>(f.http);
  const Target app = f.installed();
  {
    UpdateService service(std::move(http), f.config, f.dir.path() / "updates", app,
                          [&](UpdateService::Task t) { ui.post(std::move(t)); },
                          [&](const UpdateState& s) { seen.push_back(s); });
    service.check();
    REQUIRE(ui.pump_until([&] { return !seen.empty() && seen.back().status == UpdateState::Status::Available; }));
    service.install();
    REQUIRE(ui.pump_until([&] { return seen.back().status == UpdateState::Status::Scheduled; }));
  }
  CHECK(read_text(app.path) == "old AppImage 0.1.0");  // not before the next start
  CHECK(apply_pending(f.dir.path() / "updates").applied);
  CHECK(read_text(app.path) == "new AppImage 0.2.0");
}
