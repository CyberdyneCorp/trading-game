#include "model/packs/download.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <system_error>

#include "model/packs/files.hpp"

namespace tradelab::desktop::packs {

namespace {

std::int64_t file_size_or_zero(const std::filesystem::path& p) {
  std::error_code ec;
  const auto n = std::filesystem::file_size(p, ec);
  return ec ? 0 : static_cast<std::int64_t>(n);
}

bool keeps_partial(DownloadFailure f) {
  return f == DownloadFailure::Interrupted || f == DownloadFailure::Http || f == DownloadFailure::Aborted ||
         f == DownloadFailure::Storage;
}

}  // namespace

std::string_view download_failure_name(DownloadFailure f) {
  constexpr std::array<std::string_view, 8> kNames = {"none",          "interrupted",   "http",    "aborted",
                                                      "size_mismatch", "hash_mismatch", "storage", "refused"};
  return kNames.at(static_cast<std::size_t>(f));
}

std::int64_t PackDownloader::received(const std::string& digest) {
  const auto row = store_.download(digest);
  return row ? row->received : 0;
}

void PackDownloader::drop_partial(const std::string& digest) {
  std::error_code ec;
  std::filesystem::remove(library_.part_path(digest), ec);
  try {
    store_.clear_download(digest);
  } catch (const storage::Error&) {  // the row is stale but harmless: prepare() re-checks it
  }
}

// The committed offset, reconciled with the .part file: bytes appended after the last committed offset
// (a crash between the fsync and the row update) are cut; a stale or foreign row starts over.
std::int64_t PackDownloader::prepare(const CatalogEntry& entry) {
  const auto row = store_.download(entry.digest);
  const std::filesystem::path part = library_.part_path(entry.digest);
  const std::int64_t on_disk = file_size_or_zero(part);
  if (!row || row->size != entry.size || row->received > on_disk || row->received > entry.size) {
    drop_partial(entry.digest);
    store_.save_download(storage::Download{entry.digest, entry.url, entry.size, 0});
    return 0;
  }
  if (on_disk > row->received) std::filesystem::resize_file(part, static_cast<std::uintmax_t>(row->received));
  return row->received;
}

DownloadFailure PackDownloader::store_whole(const CatalogEntry& entry, const std::vector<std::uint8_t>& body,
                                            std::int64_t& received) {
  if (static_cast<std::int64_t>(body.size()) != entry.size) return DownloadFailure::SizeMismatch;
  if (!write_file_atomic(library_.part_path(entry.digest), body)) return DownloadFailure::Storage;
  received = entry.size;
  store_.save_download(storage::Download{entry.digest, entry.url, entry.size, received});
  return DownloadFailure::None;
}

DownloadFailure PackDownloader::store_chunk(const CatalogEntry& entry, const HttpResponse& r, std::int64_t& received) {
  const auto range = parse_content_range(r.content_range);
  const auto n = static_cast<std::int64_t>(r.body.size());
  const bool fits = range && range->start == received && range->end - range->start + 1 == n && n > 0 &&
                    received + n <= entry.size && (range->total < 0 || range->total == entry.size);
  if (!fits) return DownloadFailure::SizeMismatch;
  if (!append_file_durable(library_.part_path(entry.digest), r.body)) return DownloadFailure::Storage;
  received += n;
  store_.save_download(storage::Download{entry.digest, entry.url, entry.size, received});
  return DownloadFailure::None;
}

// One request for the bytes after `received`.
DownloadFailure PackDownloader::step(const CatalogEntry& entry, std::int64_t& received) {
  const ByteRange range{received, std::min(entry.size, received + chunk_bytes_) - 1};
  const HttpResponse r = http_.get(entry.url, range);
  if (r.status == 0) return DownloadFailure::Interrupted;
  if (r.status == 200) return store_whole(entry, r.body, received);
  if (r.status == 206) return store_chunk(entry, r, received);
  return DownloadFailure::Http;
}

DownloadResult PackDownloader::finish(const CatalogEntry& entry, const std::string& label) {
  DownloadResult out;
  const auto bytes = read_file(library_.part_path(entry.digest));
  if (!bytes || static_cast<std::int64_t>(bytes->size()) != entry.size || sha256_hex(*bytes) != entry.digest) {
    drop_partial(entry.digest);
    out.failure = DownloadFailure::HashMismatch;
    return out;
  }
  out.check = library_.install(*bytes, label, "catalog");
  // Installed, or refused by the engine (retrying the same bytes cannot help): the partial goes either way.
  drop_partial(entry.digest);
  out.failure = out.check.ok ? DownloadFailure::None : DownloadFailure::Refused;
  out.received = 0;
  return out;
}

DownloadResult PackDownloader::install(const CatalogEntry& entry, const std::string& label,
                                       const DownloadProgress& progress) {
  DownloadResult out;
  try {
    std::int64_t received = prepare(entry);
    if (progress) progress(received, entry.size);
    while (received < entry.size) {
      const DownloadFailure f = step(entry, received);
      if (f != DownloadFailure::None) {
        if (!keeps_partial(f)) drop_partial(entry.digest);
        out.failure = f;
        out.received = keeps_partial(f) ? received : 0;
        return out;
      }
      if (progress && !progress(received, entry.size) && received < entry.size) {
        out.failure = DownloadFailure::Aborted;
        out.received = received;
        return out;
      }
    }
  } catch (const std::exception&) {  // storage::Error, or the filesystem refusing the .part file
    out.failure = DownloadFailure::Storage;
    return out;
  }
  return finish(entry, label);
}

std::optional<Catalog> PackDownloader::fetch_catalog(const std::string& url) { return read_catalog(url).catalog; }

PackDownloader::CatalogRead PackDownloader::read_catalog(const std::string& url) {
  CatalogRead out;
  const HttpResponse r = url.empty() ? HttpResponse{} : http_.get(url, std::nullopt);
  if (r.status == 200) {
    const std::string text(r.body.begin(), r.body.end());
    if (auto c = parse_catalog(text, url)) {
      try {
        store_.set_meta("catalog.url", url);
        store_.set_meta("catalog.json", text);
      } catch (const storage::Error&) {  // shown now; just not cached for offline use
      }
      out.catalog = std::move(c);
      out.fresh = true;
      return out;
    }
  }
  out.offline = r.status == 0 && !url.empty();
  const auto cached_url = store_.meta("catalog.url");
  const auto cached = store_.meta("catalog.json");
  if (cached_url && cached) out.catalog = parse_catalog(*cached, *cached_url);
  return out;
}

}  // namespace tradelab::desktop::packs
