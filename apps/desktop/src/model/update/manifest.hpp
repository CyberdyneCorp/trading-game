// Signed update manifest (spec desktop-client "Updates", design "Updater"). The release pipeline publishes
//   desktop-manifest.json       the bytes below, exactly as signed
//   desktop-manifest.json.sig   Ed25519 signature of those bytes, 128 hex digits
// and the app trusts a manifest only when the signature verifies against the release public key compiled
// into the build (engine's ed25519_verify). Format 1:
//   {"format":1,"product":"tradelab-desktop","releases":[
//     {"version":"0.2.0","platform":"linux-x86_64","url":"https://.../TradeLab-x86_64.AppImage",
//      "size":123,"sha256":"<64 hex>","notes":{"pt-BR":"...","en":"..."}}, ...]}
// platform: "macos" (a zip of TradeLab.app), "linux-x86_64" / "linux-aarch64" (the AppImage itself).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <tradelab/ed25519.hpp>

namespace tradelab::desktop::update {

constexpr std::int64_t kManifestFormat = 1;
constexpr std::string_view kProduct = "tradelab-desktop";
constexpr std::int64_t kMaxPayloadBytes = 512LL * 1024 * 1024;

struct Release {
  std::string version;  // x.y.z
  std::string platform;
  std::string url;
  std::int64_t size = 0;
  std::string sha256;  // lowercase hex
  std::string notes_pt;
  std::string notes_en;
};

struct Manifest {
  std::vector<Release> releases;
};

enum class ManifestError : std::uint8_t { None, BadSignature, Malformed, UnsupportedFormat, WrongProduct };
std::string_view manifest_error_name(ManifestError e);

struct ManifestCheck {
  ManifestError error = ManifestError::None;
  Manifest manifest;  // set when error == None
};

// Verifies `signature_hex` over the exact `bytes`, then parses them. Nothing is parsed before the
// signature verifies.
ManifestCheck verify_manifest(std::span<const std::uint8_t> bytes, std::string_view signature_hex, const PublicKey& key);

// 64 hex digits -> key; nullopt when malformed or empty (updates off).
std::optional<PublicKey> public_key_from_hex(std::string_view hex);

// "1.10.0" > "1.9.3"; nullopt for anything but three dot-separated numbers.
std::optional<std::array<std::uint32_t, 3>> parse_version(std::string_view v);
bool version_newer(std::string_view candidate, std::string_view current);

// The newest release for `platform` newer than `current`; nullopt when up to date.
std::optional<Release> pick_release(const Manifest& m, std::string_view platform, std::string_view current);

// This build's platform name ("macos", "linux-x86_64", "linux-aarch64").
std::string host_platform();

// https anywhere; plain http only to the loopback interface (tests). Trust still rests on the signature
// (manifest) and on size + SHA-256 (payload).
bool allowed_url(std::string_view url);

}  // namespace tradelab::desktop::update
