#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "model/packs/download.hpp"
#include "model/packs/files.hpp"
#include "model/storage/storage.hpp"
#include "support/http_server.hpp"
#include "support/storage_fixtures.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::packs;
using tradelab::desktop::testing::LoopbackHttpServer;
using tradelab::desktop::testing::TempDir;
using tradelab::desktop::testing::web_catalog_dir;

namespace {

constexpr const char* kSampleA = "ec9140b18db6c1997d9ebefa494be5d040fc91052236045d2a6336d2263602ee";
constexpr std::int64_t kChunk = 4096;  // small ranges so the 77 KB sample pack takes many requests

std::vector<std::uint8_t> sample_bytes() {
  auto b = read_file(web_catalog_dir() / "packs" / (std::string(kSampleA) + ".tlpack"));
  REQUIRE(b);
  return *b;
}

CatalogEntry sample_entry(const std::string& url = "https://cdn.example/packs/a.tlpack") {
  CatalogEntry e;
  e.id = "sample-a-1h";
  e.url = url;
  e.digest = kSampleA;
  e.size = 77312;
  e.title.pt_br = "Mercado A · 1h";
  return e;
}

// Serves the pack with Range; `drop_from` makes every request at or after that offset fail like a lost
// connection; `ignore_range` answers 200 with the whole file; `corrupt` flips a byte.
class FakeHttp final : public HttpClient {
 public:
  explicit FakeHttp(std::vector<std::uint8_t> body) : body_(std::move(body)) {}
  std::int64_t drop_from = -1;
  bool ignore_range = false;
  bool wrong_total = false;
  int status = 0;
  std::vector<std::int64_t> offsets;

  HttpResponse get(const std::string& /*url*/, const std::optional<ByteRange>& range) override {
    HttpResponse r;
    const std::int64_t first = range ? range->first : 0;
    offsets.push_back(first);
    if (drop_from >= 0 && first >= drop_from) return r;  // status 0
    if (status != 0) {
      r.status = status;
      return r;
    }
    if (ignore_range || !range) {
      r.status = 200;
      r.body = body_;
      return r;
    }
    const std::int64_t last = std::min<std::int64_t>(range->last, static_cast<std::int64_t>(body_.size()) - 1);
    r.status = 206;
    r.content_range = "bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" +
                      std::to_string(wrong_total ? body_.size() + 1 : body_.size());
    r.body.assign(body_.begin() + first, body_.begin() + last + 1);
    return r;
  }

 private:
  std::vector<std::uint8_t> body_;
};

}  // namespace

TEST_CASE("packs download: interrupted at 60% it resumes from the committed offset, then installs") {
  TempDir dir;
  storage::Storage s(dir.path());
  FakeHttp http(sample_bytes());
  const CatalogEntry e = sample_entry();
  const std::int64_t sixty = (e.size * 6 / 10) / kChunk * kChunk;  // 60% rounded down to whole chunks
  http.drop_from = sixty;
  PackDownloader dl(s.packs(), s.store(), http, kChunk);
  const DownloadResult first = dl.install(e, "Mercado A");
  CHECK(first.failure == DownloadFailure::Interrupted);
  CHECK(first.received == sixty);
  CHECK(dl.received(kSampleA) == sixty);
  CHECK(std::filesystem::file_size(s.packs().part_path(kSampleA)) == static_cast<std::uintmax_t>(sixty));
  CHECK_FALSE(s.store().pack(kSampleA));

  http.drop_from = -1;
  http.offsets.clear();
  storage::Storage reopened(dir.path());  // a restart in between
  PackDownloader again(reopened.packs(), reopened.store(), http, kChunk);
  const DownloadResult second = again.install(e, "Mercado A");
  REQUIRE(second.failure == DownloadFailure::None);
  CHECK(second.check.ok);
  CHECK(http.offsets.front() == sixty);  // only the missing bytes were asked for
  CHECK(reopened.store().pack(kSampleA)->origin == "catalog");
  CHECK_FALSE(std::filesystem::exists(reopened.packs().part_path(kSampleA)));
  CHECK_FALSE(reopened.store().download(kSampleA));
}

TEST_CASE("packs download: bytes appended after the last committed offset are cut on resume") {
  TempDir dir;
  storage::Storage s(dir.path());
  FakeHttp http(sample_bytes());
  const CatalogEntry e = sample_entry();
  http.drop_from = kChunk;
  PackDownloader dl(s.packs(), s.store(), http, kChunk);
  CHECK(dl.install(e, "A").received == kChunk);
  // A crash after the fsync of the next chunk but before its offset was committed.
  REQUIRE(append_file_durable(s.packs().part_path(kSampleA), std::vector<std::uint8_t>(500, 0xEE)));
  http.drop_from = -1;
  const DownloadResult r = dl.install(e, "A");
  CHECK(r.failure == DownloadFailure::None);
  CHECK(r.check.ok);
}

TEST_CASE("packs download: a server that ignores Range is accepted") {
  TempDir dir;
  storage::Storage s(dir.path());
  FakeHttp http(sample_bytes());
  http.ignore_range = true;
  PackDownloader dl(s.packs(), s.store(), http, kChunk);
  const DownloadResult r = dl.install(sample_entry(), "A");
  CHECK(r.failure == DownloadFailure::None);
  CHECK(http.offsets.size() == 1);
}

TEST_CASE("packs download: size and hash mismatches drop the partial data; HTTP errors keep it") {
  TempDir dir;
  storage::Storage s(dir.path());
  auto bytes = sample_bytes();
  CatalogEntry e = sample_entry();

  FakeHttp wrong_total(bytes);
  wrong_total.wrong_total = true;
  PackDownloader a(s.packs(), s.store(), wrong_total, kChunk);
  CHECK(a.install(e, "A").failure == DownloadFailure::SizeMismatch);
  CHECK(a.received(kSampleA) == 0);

  auto tampered = bytes;
  tampered[1000] ^= 0xFF;
  FakeHttp bad(tampered);
  PackDownloader b(s.packs(), s.store(), bad, kChunk);
  CHECK(b.install(e, "A").failure == DownloadFailure::HashMismatch);
  CHECK_FALSE(std::filesystem::exists(s.packs().part_path(kSampleA)));
  CHECK_FALSE(s.store().pack(kSampleA));

  FakeHttp gone(bytes);
  gone.drop_from = 2 * kChunk;
  PackDownloader c(s.packs(), s.store(), gone, kChunk);
  CHECK(c.install(e, "A").failure == DownloadFailure::Interrupted);
  gone.drop_from = -1;
  gone.status = 503;
  const DownloadResult r = c.install(e, "A");
  CHECK(r.failure == DownloadFailure::Http);
  CHECK(r.received == 2 * kChunk);
}

TEST_CASE("packs download: bytes that hash right but fail the engine's check are refused with its reason") {
  TempDir dir;
  storage::Storage s(dir.path());
  std::vector<std::uint8_t> junk(4096, 0x42);
  FakeHttp http(junk);
  CatalogEntry e = sample_entry();
  e.size = static_cast<std::int64_t>(junk.size());
  e.digest = sha256_hex(junk);
  PackDownloader dl(s.packs(), s.store(), http, kChunk);
  const DownloadResult r = dl.install(e, "junk");
  CHECK(r.failure == DownloadFailure::Refused);
  CHECK_FALSE(r.check.ok);
  CHECK_FALSE(r.check.reason.empty());
  CHECK(s.store().packs().empty());
}

TEST_CASE("packs download: stopping from the progress callback keeps what arrived") {
  TempDir dir;
  storage::Storage s(dir.path());
  FakeHttp http(sample_bytes());
  PackDownloader dl(s.packs(), s.store(), http, kChunk);
  const DownloadResult r = dl.install(sample_entry(), "A", [](std::int64_t got, std::int64_t) { return got < kChunk; });
  CHECK(r.failure == DownloadFailure::Aborted);
  CHECK(r.received == kChunk);
}

TEST_CASE("packs catalog: fetched online, then read from the cache offline") {
  TempDir dir;
  storage::Storage s(dir.path());
  const auto manifest = read_file(web_catalog_dir() / "catalog.json");
  REQUIRE(manifest);
  FakeHttp online(*manifest);
  PackDownloader dl(s.packs(), s.store(), online, kChunk);
  const auto c = dl.fetch_catalog("https://cdn.example/catalog/catalog.json");
  REQUIRE(c);
  CHECK(c->entries.size() == 2);
  FakeHttp offline(*manifest);
  offline.drop_from = 0;
  PackDownloader off(s.packs(), s.store(), offline, kChunk);
  const auto cached = off.fetch_catalog("https://cdn.example/catalog/catalog.json");
  REQUIRE(cached);
  CHECK(cached->entries[0].url == c->entries[0].url);
}

TEST_CASE("packs http: without libcurl the client is unavailable (offline play is unaffected)") {
  CHECK_FALSE(CurlHttpClient::load("libtradelab-no-such-curl.so"));
  CHECK(parse_content_range("bytes 0-99/200")->total == 200);
  CHECK(parse_content_range("bytes 5-9/*")->total == -1);
  CHECK_FALSE(parse_content_range("bytes 9-5/200"));
  CHECK_FALSE(parse_content_range("bytes 0-99/200 x"));
}

TEST_CASE("packs http: libcurl downloads with Range from a loopback server and resumes after a drop") {
  auto curl = CurlHttpClient::load();
  if (!curl) {
    MESSAGE("libcurl not found on this machine: skipped");
    return;
  }
  const auto bytes = sample_bytes();
  LoopbackHttpServer server(bytes, 1);  // serves one chunk, then drops every connection
  TempDir dir;
  storage::Storage s(dir.path());
  const CatalogEntry e = sample_entry(server.url());
  PackDownloader dl(s.packs(), s.store(), *curl);
  const DownloadResult first = dl.install(e, "A");
  CHECK(first.failure == DownloadFailure::Interrupted);
  CHECK(first.received == kChunkBytes);
  server.set_remaining(1 << 30);
  const DownloadResult second = dl.install(e, "A");
  REQUIRE(second.failure == DownloadFailure::None);
  CHECK(second.check.ok);
  CHECK(s.packs().bytes(kSampleA) == bytes);
}
