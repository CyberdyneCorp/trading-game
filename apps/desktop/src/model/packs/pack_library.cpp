#include "model/packs/pack_library.hpp"

#include <tradelab/c_api.h>

#include <random>
#include <system_error>

#include "model/json.hpp"
#include "model/packs/files.hpp"
#include "model/resources.hpp"
#include "model/storage/json_writer.hpp"

namespace tradelab::desktop::packs {

namespace {

json::Value parse_or_null(const char* text) {
  if (text == nullptr) return {};
  auto v = json::parse(text);
  return v ? *v : json::Value{};
}

storage::StoredPack entry_from_summary(const json::Value& s, std::size_t size, const std::string& label,
                                       const std::string& origin) {
  storage::StoredPack p;
  p.digest = s["digest"].as_string();
  p.scenario_id = s["scenarioId"].as_string();
  p.label = label;
  p.size = static_cast<std::int64_t>(size);
  p.is_private = s["private"].as_bool(false);
  p.total_bars = static_cast<int>(s["totalBars"].as_int());
  p.warmup_bars = static_cast<int>(s["warmupBars"].as_int());
  p.leverage_cap = static_cast<int>(s["leverageCap"].as_int());
  p.gaps = static_cast<int>(s["gaps"].as_int());
  p.added_at = storage::now_ms();
  p.origin = origin;
  if (const std::string mode = s["mode"].as_string(); !mode.empty()) p.mode = mode;
  return p;
}

PackCheck refused(std::string reason, std::string detail = {}) {
  return PackCheck{false, std::move(reason), std::move(detail), std::nullopt};
}

}  // namespace

std::string random_hex(std::size_t bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::random_device rd;
  std::string out;
  for (std::size_t i = 0; i < bytes; ++i) {
    const auto b = static_cast<unsigned>(rd()) & 0xFFU;
    out += kHex[b >> 4];
    out += kHex[b & 0xFU];
  }
  return out;
}

PackLibrary::PackLibrary(storage::RunStore& store, std::filesystem::path dir) : store_(store), dir_(std::move(dir)) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
}

std::filesystem::path PackLibrary::pack_path(const std::string& digest) const { return dir_ / (digest + ".tlpack"); }

std::filesystem::path PackLibrary::part_path(const std::string& digest) const { return dir_ / (digest + ".part"); }

const std::string& PackLibrary::device_seed() {
  if (device_seed_.empty()) device_seed_ = store_.device_seed([] { return random_hex(32); });
  return device_seed_;
}

std::string PackLibrary::trusted_keys_json() {
  if (device_key_entry_.empty()) {
    const json::Value k = parse_or_null(tl_device_key(device_seed().c_str()));
    storage::JsonWriter w;
    w.begin_object().field("id", k["id"].as_string()).field("key", k["publicKey"].as_string()).end_object();
    device_key_entry_ = w.text();
  }
  const auto pinned = json::parse(resource_text("packs/pinned_keys.json"));
  storage::JsonWriter w;
  w.begin_array();
  for (const json::Value& k : pinned ? pinned->items() : std::vector<json::Value>{}) {
    w.begin_object().field("id", k["id"].as_string()).field("key", k["key"].as_string()).end_object();
  }
  w.raw(device_key_entry_);
  return w.end_array().text();
}

PackCheck PackLibrary::install(std::span<const std::uint8_t> bytes, const std::string& label, const std::string& origin) {
  if (bytes.size() > kPackMaxBytes) return refused("too_large");
  const std::string keys = trusted_keys_json();
  const json::Value check =
      parse_or_null(tl_pack_validate(bytes.data(), static_cast<uint32_t>(bytes.size()), keys.c_str(), 0));
  if (!check["ok"].as_bool(false)) return refused(check["reason"].as_string("invalid"), check["detail"].as_string());
  storage::StoredPack entry = entry_from_summary(check["pack"], bytes.size(), label, origin);
  if (entry.digest != sha256_hex(bytes)) return refused("digest_mismatch");
  try {
    if (!write_file_atomic(pack_path(entry.digest), bytes)) return refused("storage_failed");
    store_.save_pack(entry);
  } catch (const storage::Error& e) {
    return refused("storage_failed", e.what());
  }
  return PackCheck{true, "none", {}, entry};
}

std::optional<std::vector<std::uint8_t>> PackLibrary::bytes(const std::string& digest) {
  const auto entry = store_.pack(digest);
  if (!entry) return std::nullopt;
  auto data = read_file(pack_path(digest));
  if (!data || static_cast<std::int64_t>(data->size()) != entry->size) return std::nullopt;
  return data;
}

std::vector<std::string> PackLibrary::missing() {
  std::vector<std::string> out;
  for (const storage::StoredPack& p : store_.packs()) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(pack_path(p.digest), ec);
    if (ec || static_cast<std::int64_t>(size) != p.size) out.push_back(p.digest);
  }
  return out;
}

void PackLibrary::remove(const std::string& digest) {
  store_.delete_pack(digest);
  std::error_code ec;
  std::filesystem::remove(pack_path(digest), ec);
}

std::string PackLibrary::validate_csv(const std::string& csv, const std::string& meta_json) {
  if (csv.size() > kCsvMaxBytes) return R"({"ok":false,"metaError":"file_too_large"})";
  const char* r = tl_csv_validate(csv.c_str(), meta_json.c_str());
  return r != nullptr ? r : "";
}

CsvImportResult PackLibrary::import_csv(const std::string& csv, const std::string& meta_json, const std::string& label) {
  CsvImportResult out;
  if (csv.size() > kCsvMaxBytes) {
    out.reason = "file_too_large";
    return out;
  }
  const json::Value built = parse_or_null(tl_pack_build_csv(csv.c_str(), meta_json.c_str(), device_seed().c_str()));
  out.report_json = storage::dump(built["report"]);
  out.reason = built["reason"].as_string("invalid");
  if (!built["ok"].as_bool(false)) return out;
  // The blob buffer is run-less and shared: copy it before the next engine call.
  const std::vector<std::uint8_t> pack(tl_blob_data(), tl_blob_data() + tl_blob_size());
  const PackCheck check = install(pack, label, "import");
  out.ok = check.ok;
  out.reason = check.reason;
  if (check.entry) out.digest = check.entry->digest;
  return out;
}

}  // namespace tradelab::desktop::packs
