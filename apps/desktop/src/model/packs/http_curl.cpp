// libcurl through dlopen: only the handful of easy-interface calls the downloader needs. Option and info
// numbers are libcurl's stable ABI values (curl/curl.h: CURLOPTTYPE_* + n).
#include <dlfcn.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <mutex>

#include "model/packs/http.hpp"
#include "model/packs/pack_library.hpp"

namespace tradelab::desktop::packs {

namespace {

using CurlHandle = void;
using CurlCode = int;

constexpr int kOptWriteData = 10001;
constexpr int kOptUrl = 10002;
constexpr int kOptRange = 10007;
constexpr int kOptUserAgent = 10018;
constexpr int kOptHeaderData = 10029;
constexpr int kOptProtocolsStr = 10318;
constexpr int kOptWriteFunction = 20011;
constexpr int kOptHeaderFunction = 20079;
constexpr int kOptTimeout = 13;
constexpr int kOptLowSpeedLimit = 19;
constexpr int kOptLowSpeedTime = 20;
constexpr int kOptFollowLocation = 52;
constexpr int kOptMaxRedirs = 68;
constexpr int kOptConnectTimeout = 78;
constexpr int kOptNoSignal = 99;
constexpr int kInfoResponseCode = 0x200002;
constexpr long kGlobalDefault = 3;  // CURL_GLOBAL_ALL

// Bodies larger than a pack (or a catalog) are cut off: a hostile server cannot fill memory.
constexpr std::size_t kMaxBody = kPackMaxBytes + 1024U * 1024U;

struct Transfer {
  std::vector<std::uint8_t> body;
  std::string content_range;
  bool too_large = false;
};

std::size_t on_body(char* data, std::size_t size, std::size_t count, void* user) {
  auto* t = static_cast<Transfer*>(user);
  const std::size_t n = size * count;
  if (t->body.size() + n > kMaxBody) {
    t->too_large = true;
    return 0;  // aborts the transfer
  }
  t->body.insert(t->body.end(), data, data + n);
  return n;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::size_t on_header(char* data, std::size_t size, std::size_t count, void* user) {
  auto* t = static_cast<Transfer*>(user);
  const std::size_t n = size * count;
  const std::string line(data, n);
  if (line.starts_with("HTTP/")) t->content_range.clear();  // a new response (after a redirect)
  const auto colon = line.find(':');
  if (colon != std::string::npos && lower(line.substr(0, colon)) == "content-range") {
    t->content_range = trim(line.substr(colon + 1));
  }
  return n;
}

#if defined(__APPLE__)
constexpr std::array<const char*, 3> kLibraries = {"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib"};
#else
constexpr std::array<const char*, 3> kLibraries = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"};
#endif

}  // namespace

struct CurlHttpClient::Api {
  void* lib = nullptr;
  CurlHandle* (*easy_init)() = nullptr;
  CurlCode (*easy_setopt)(CurlHandle*, int, ...) = nullptr;
  CurlCode (*easy_perform)(CurlHandle*) = nullptr;
  CurlCode (*easy_getinfo)(CurlHandle*, int, ...) = nullptr;
  void (*easy_cleanup)(CurlHandle*) = nullptr;
  const char* (*easy_strerror)(CurlCode) = nullptr;
  CurlCode (*global_init)(long) = nullptr;

  template <class Fn>
  bool bind(Fn& fn, const char* name) {
    fn = reinterpret_cast<Fn>(dlsym(lib, name));
    return fn != nullptr;
  }

  bool bind_all() {
    return bind(easy_init, "curl_easy_init") && bind(easy_setopt, "curl_easy_setopt") &&
           bind(easy_perform, "curl_easy_perform") && bind(easy_getinfo, "curl_easy_getinfo") &&
           bind(easy_cleanup, "curl_easy_cleanup") && bind(easy_strerror, "curl_easy_strerror") &&
           bind(global_init, "curl_global_init");
  }
};

std::unique_ptr<CurlHttpClient> CurlHttpClient::load(const char* library) {
  auto api = std::make_unique<Api>();
  if (library != nullptr) {
    api->lib = dlopen(library, RTLD_NOW | RTLD_LOCAL);
  } else {
    for (const char* name : kLibraries) {
      if ((api->lib = dlopen(name, RTLD_NOW | RTLD_LOCAL)) != nullptr) break;
    }
  }
  if (api->lib == nullptr) return nullptr;
  if (!api->bind_all()) {
    dlclose(api->lib);
    return nullptr;
  }
  static std::once_flag once;
  std::call_once(once, [&] { api->global_init(kGlobalDefault); });
  return std::unique_ptr<CurlHttpClient>(new CurlHttpClient(std::move(api)));
}

CurlHttpClient::CurlHttpClient(std::unique_ptr<Api> api) : api_(std::move(api)) {}

// The library stays loaded: curl_global_init has run and other handles may still reference it.
CurlHttpClient::~CurlHttpClient() = default;

HttpResponse CurlHttpClient::get(const std::string& url, const std::optional<ByteRange>& range) {
  HttpResponse out;
  CurlHandle* h = api_->easy_init();
  if (h == nullptr) {
    out.error = "curl_easy_init failed";
    return out;
  }
  Transfer t;
  const std::string range_text =
      range ? std::to_string(range->first) + "-" + std::to_string(range->last) : std::string();
  api_->easy_setopt(h, kOptUrl, url.c_str());
  api_->easy_setopt(h, kOptProtocolsStr, "https,http");
  api_->easy_setopt(h, kOptUserAgent, "TradeLab-desktop");
  api_->easy_setopt(h, kOptFollowLocation, 1L);
  api_->easy_setopt(h, kOptMaxRedirs, 5L);
  api_->easy_setopt(h, kOptNoSignal, 1L);
  api_->easy_setopt(h, kOptConnectTimeout, 15L);
  api_->easy_setopt(h, kOptTimeout, 120L);
  api_->easy_setopt(h, kOptLowSpeedLimit, 1L);
  api_->easy_setopt(h, kOptLowSpeedTime, 30L);
  api_->easy_setopt(h, kOptWriteFunction, &on_body);
  api_->easy_setopt(h, kOptWriteData, static_cast<void*>(&t));
  api_->easy_setopt(h, kOptHeaderFunction, &on_header);
  api_->easy_setopt(h, kOptHeaderData, static_cast<void*>(&t));
  if (range) api_->easy_setopt(h, kOptRange, range_text.c_str());
  const CurlCode rc = api_->easy_perform(h);
  long status = 0;
  api_->easy_getinfo(h, kInfoResponseCode, &status);
  api_->easy_cleanup(h);
  if (rc != 0 || t.too_large) {
    out.error = t.too_large ? "response too large" : api_->easy_strerror(rc);
    return out;
  }
  out.status = static_cast<int>(status);
  out.content_range = std::move(t.content_range);
  out.body = std::move(t.body);
  return out;
}

std::optional<ContentRange> parse_content_range(const std::string& header) {
  long long start = 0;
  long long end = 0;
  char total[32] = {};
  int consumed = 0;
  const std::string h = trim(header);
  if (std::sscanf(h.c_str(), "bytes %lld-%lld/%31s%n", &start, &end, total, &consumed) != 3) return std::nullopt;
  if (static_cast<std::size_t>(consumed) != h.size() || start < 0 || end < start) return std::nullopt;
  const std::string t(total);
  if (t == "*") return ContentRange{start, end, -1};
  if (t.empty() || t.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
  return ContentRange{start, end, std::stoll(t)};
}

}  // namespace tradelab::desktop::packs
