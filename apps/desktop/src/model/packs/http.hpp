// HTTP for the pack catalog and downloads (design "Storage" > Packs). The app uses CurlHttpClient: libcurl
// opened at run time (macOS: the system /usr/lib/libcurl.4.dylib; Linux: libcurl.so.4 from the host), so
// nothing links it and, when it is absent, the catalog reads "unavailable" while offline play is intact.
// Tests use fakes or the loopback server in tests/support.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tradelab::desktop::packs {

struct ByteRange {
  std::int64_t first = 0;
  std::int64_t last = 0;  // inclusive, as in the Range header
};

struct HttpResponse {
  int status = 0;              // 0: no response (offline, refused, timeout, too large)
  std::string content_range;   // Content-Range header value, if any
  std::vector<std::uint8_t> body;
  std::string error;           // transport error text when status is 0
};

class HttpClient {
 public:
  virtual ~HttpClient() = default;
  virtual HttpResponse get(const std::string& url, const std::optional<ByteRange>& range) = 0;
};

class CurlHttpClient final : public HttpClient {
 public:
  // nullptr when libcurl cannot be opened (`library` overrides the platform's candidates).
  static std::unique_ptr<CurlHttpClient> load(const char* library = nullptr);
  ~CurlHttpClient() override;
  CurlHttpClient(const CurlHttpClient&) = delete;
  CurlHttpClient& operator=(const CurlHttpClient&) = delete;

  HttpResponse get(const std::string& url, const std::optional<ByteRange>& range) override;

  struct Api;

 private:
  explicit CurlHttpClient(std::unique_ptr<Api> api);
  std::unique_ptr<Api> api_;
};

// "bytes a-b/total" -> parts; nullopt when malformed (total -1 for "*").
struct ContentRange {
  std::int64_t start = 0;
  std::int64_t end = 0;
  std::int64_t total = -1;
};
std::optional<ContentRange> parse_content_range(const std::string& header);

}  // namespace tradelab::desktop::packs
