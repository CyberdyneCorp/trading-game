// A loopback HTTP/1.1 server for the libcurl client test: serves one file at any path (or another file at
// a path given to serve()), honours "Range: bytes=a-b" with 206 + Content-Range, and can drop the
// connection after N requests (a network that goes away mid-download). One request per connection.
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tradelab::desktop::testing {

class LoopbackHttpServer {
 public:
  explicit LoopbackHttpServer(std::vector<std::uint8_t> body, int serve_requests = 1 << 30)
      : body_(std::move(body)), remaining_(serve_requests) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    socklen_t len = sizeof addr;
    ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    ::listen(fd_, 16);
    thread_ = std::thread([this] { loop(); });
  }

  ~LoopbackHttpServer() {
    stop_ = true;
    thread_.join();  // the loop polls with a timeout, so it sees stop_ (closing alone does not wake accept on macOS)
    ::close(fd_);
  }

  LoopbackHttpServer(const LoopbackHttpServer&) = delete;
  LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

  [[nodiscard]] std::string url(const std::string& path = "/pack.tlpack") const {
    return "http://127.0.0.1:" + std::to_string(port_) + path;
  }
  [[nodiscard]] int requests() const { return requests_; }
  void set_remaining(int n) { remaining_ = n; }
  // `body` at exactly `path` (e.g. a catalog manifest next to its packs).
  void serve(const std::string& path, std::vector<std::uint8_t> body) {
    std::lock_guard lock(mu_);
    routes_[path] = std::move(body);
  }

 private:
  void loop() {
    while (!stop_) {
      pollfd p{fd_, POLLIN, 0};
      if (::poll(&p, 1, 50) <= 0) continue;
      const int c = ::accept(fd_, nullptr, nullptr);
      if (c < 0) continue;
#ifdef SO_NOSIGPIPE
      int one = 1;
      ::setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
      handle(c);
      ::close(c);
    }
  }

  static std::string read_request(int c) {
    std::string req;
    char buf[1024];
    while (req.find("\r\n\r\n") == std::string::npos) {
      const ssize_t n = ::recv(c, buf, sizeof buf, 0);
      if (n <= 0) break;
      req.append(buf, static_cast<std::size_t>(n));
    }
    return req;
  }

  void handle(int c) {
    const std::string req = read_request(c);
    ++requests_;
    if (remaining_-- <= 0) return;  // connection dropped without a response
    const std::vector<std::uint8_t> body = body_for(req);
    long long first = 0;
    long long last = static_cast<long long>(body.size()) - 1;
    std::string head;
    const auto at = req.find("Range: bytes=");
    if (at != std::string::npos && std::sscanf(req.c_str() + at, "Range: bytes=%lld-%lld", &first, &last) == 2) {
      last = std::min(last, static_cast<long long>(body.size()) - 1);
      head = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + std::to_string(first) + "-" + std::to_string(last) +
             "/" + std::to_string(body.size()) + "\r\n";
    } else {
      head = "HTTP/1.1 200 OK\r\n";
    }
    const auto n = static_cast<std::size_t>(last - first + 1);
    head += "Content-Length: " + std::to_string(n) + "\r\nConnection: close\r\n\r\n";
    send_all(c, head.data(), head.size());
    send_all(c, reinterpret_cast<const char*>(body.data()) + first, n);
  }

  // The routed file for "GET <path> HTTP/1.1", else the default one.
  std::vector<std::uint8_t> body_for(const std::string& req) {
    const auto start = req.find(' ');
    const auto end = start == std::string::npos ? start : req.find(' ', start + 1);
    if (end != std::string::npos) {
      std::lock_guard lock(mu_);
      const auto it = routes_.find(req.substr(start + 1, end - start - 1));
      if (it != routes_.end()) return it->second;
    }
    return body_;
  }

  static void send_all(int c, const char* p, std::size_t n) {
    while (n > 0) {
#ifdef MSG_NOSIGNAL
      const ssize_t k = ::send(c, p, n, MSG_NOSIGNAL);
#else
      const ssize_t k = ::send(c, p, n, 0);
#endif
      if (k <= 0) return;
      p += k;
      n -= static_cast<std::size_t>(k);
    }
  }

  std::vector<std::uint8_t> body_;
  std::mutex mu_;
  std::map<std::string, std::vector<std::uint8_t>> routes_;
  std::atomic<int> remaining_;
  std::atomic<int> requests_{0};
  std::atomic<bool> stop_{false};
  int fd_ = -1;
  int port_ = 0;
  std::thread thread_;
};

}  // namespace tradelab::desktop::testing
