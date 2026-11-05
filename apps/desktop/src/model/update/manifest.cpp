#include "model/update/manifest.hpp"

#include <array>
#include <charconv>
#include <string>

#include "model/json.hpp"

namespace tradelab::desktop::update {

namespace {

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

template <std::size_t N>
std::optional<std::array<std::uint8_t, N>> from_hex(std::string_view hex) {
  if (hex.size() != N * 2) return std::nullopt;
  std::array<std::uint8_t, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    const int hi = hex_digit(hex[2 * i]);
    const int lo = hex_digit(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return std::nullopt;
    out[i] = static_cast<std::uint8_t>(hi * 16 + lo);
  }
  return out;
}

std::string_view trimmed(std::string_view s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.remove_suffix(1);
  return s;
}

bool lower_hex(std::string_view s, std::size_t n) {
  if (s.size() != n) return false;
  for (char c : s) {
    if (hex_digit(c) < 0 || (c >= 'A' && c <= 'F')) return false;
  }
  return true;
}

std::optional<Release> release_from(const json::Value& r) {
  Release out;
  out.version = r["version"].as_string();
  out.platform = r["platform"].as_string();
  out.url = r["url"].as_string();
  out.size = r["size"].as_int(-1);
  out.sha256 = r["sha256"].as_string();
  out.notes_pt = r["notes"]["pt-BR"].as_string();
  out.notes_en = r["notes"]["en"].as_string();
  const bool valid = parse_version(out.version) && !out.platform.empty() && allowed_url(out.url) && out.size > 0 &&
                     out.size <= kMaxPayloadBytes && lower_hex(out.sha256, 64);
  return valid ? std::optional<Release>(out) : std::nullopt;
}

ManifestCheck parse_manifest(std::span<const std::uint8_t> bytes) {
  const auto v = json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  if (!v || !v->has("releases")) return {ManifestError::Malformed, {}};
  if ((*v)["format"].as_int(-1) != kManifestFormat) return {ManifestError::UnsupportedFormat, {}};
  if ((*v)["product"].as_string() != kProduct) return {ManifestError::WrongProduct, {}};
  ManifestCheck out;
  for (const auto& r : (*v)["releases"].items()) {
    auto release = release_from(r);
    if (!release) return {ManifestError::Malformed, {}};
    out.manifest.releases.push_back(std::move(*release));
  }
  return out;
}

}  // namespace

std::string_view manifest_error_name(ManifestError e) {
  switch (e) {
    case ManifestError::None: return "none";
    case ManifestError::BadSignature: return "bad_signature";
    case ManifestError::Malformed: return "malformed";
    case ManifestError::UnsupportedFormat: return "unsupported_format";
    case ManifestError::WrongProduct: return "wrong_product";
  }
  return "malformed";
}

std::optional<PublicKey> public_key_from_hex(std::string_view hex) { return from_hex<32>(trimmed(hex)); }

ManifestCheck verify_manifest(std::span<const std::uint8_t> bytes, std::string_view signature_hex, const PublicKey& key) {
  const auto sig = from_hex<64>(trimmed(signature_hex));
  if (!sig || !ed25519_verify(key, bytes, *sig)) return {ManifestError::BadSignature, {}};
  return parse_manifest(bytes);
}

std::optional<std::array<std::uint32_t, 3>> parse_version(std::string_view v) {
  std::array<std::uint32_t, 3> out{};
  const char* p = v.data();
  const char* end = v.data() + v.size();
  for (std::size_t i = 0; i < out.size(); ++i) {
    const auto r = std::from_chars(p, end, out[i]);
    if (r.ec != std::errc() || r.ptr == p) return std::nullopt;
    p = r.ptr;
    if (i + 1 < out.size()) {
      if (p == end || *p != '.') return std::nullopt;
      ++p;
    }
  }
  return p == end ? std::optional(out) : std::nullopt;
}

bool version_newer(std::string_view candidate, std::string_view current) {
  const auto a = parse_version(candidate);
  const auto b = parse_version(current);
  return a && b && *a > *b;
}

std::optional<Release> pick_release(const Manifest& m, std::string_view platform, std::string_view current) {
  std::optional<Release> best;
  for (const Release& r : m.releases) {
    if (r.platform != platform || !version_newer(r.version, current)) continue;
    if (!best || version_newer(r.version, best->version)) best = r;
  }
  return best;
}

std::string host_platform() {
#if defined(__APPLE__)
  return "macos";
#elif defined(__aarch64__)
  return "linux-aarch64";
#else
  return "linux-x86_64";
#endif
}

bool allowed_url(std::string_view url) {
  if (url.starts_with("https://")) return url.size() > 8;
  for (std::string_view loopback : {"http://127.0.0.1:", "http://127.0.0.1/", "http://localhost:", "http://localhost/"}) {
    if (url.starts_with(loopback)) return true;
  }
  return false;
}

}  // namespace tradelab::desktop::update
