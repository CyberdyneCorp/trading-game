// libFuzzer target: CSV import (spec scenario-packs "Private CSV import", tasks 2.2).
// The first byte picks the declared units, the rest is the file. Invariants: no crash, no
// sanitizer report, and a report that says ok always comes with enough rows to build a pack.

#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "tradelab/pack.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) return 0;
  tradelab::CsvMeta meta;
  meta.timestamp_unit = (data[0] & 1U) != 0 ? tradelab::TimestampUnit::Milliseconds : tradelab::TimestampUnit::Seconds;
  meta.volume_unit = (data[0] & 2U) != 0 ? tradelab::VolumeUnit::Quote : tradelab::VolumeUnit::Base;
  meta.interval_seconds = (data[0] & 4U) != 0 ? 60 : 3600;
  const std::string_view text(reinterpret_cast<const char*>(data + 1), size - 1);
  std::vector<tradelab::CsvRow> rows;
  const tradelab::CsvReport r = tradelab::import_csv(text, meta, &rows);
  if (r.ok && rows.size() < tradelab::kCsvMinRows) std::abort();
  if (r.errors.size() > tradelab::kCsvMaxListed || r.gaps.size() > tradelab::kCsvMaxListed) std::abort();
  if (r.ok) {
    const tradelab::PackBuild b = tradelab::build_csv_pack(text, meta, {}, tradelab::SecretSeed{});
    if (b.ok()) {
      const auto k = tradelab::ed25519_keypair(tradelab::SecretSeed{});
      const std::vector<tradelab::TrustedKey> keys = {{tradelab::key_id("device", k.public_key), k.public_key}};
      if (!tradelab::open_pack(b.bytes, keys, tradelab::PackIntent::Install).ok()) std::abort();  // built packs always open
    }
  }
  return 0;
}
