// libFuzzer target: run file verification (add-run-sharing-and-journal-stats design D2). Invariants:
// every input gets a JSON verdict, and an accepted file's canonical text verifies again to the same
// file digest.

#include <cstdint>
#include <cstdlib>
#include <string>

#include "../src/json_in.hpp"
#include "tradelab/c_api.h"

namespace {

std::string field(const tradelab::detail::JsonValue& v, const char* key) {
  const tradelab::detail::JsonValue* f = v.find(key);
  return f != nullptr ? f->text : std::string{};
}

tradelab::detail::JsonParse verdict(const std::string& text) {
  tradelab::detail::JsonLimits roomy{std::size_t{8} << 20U, 64, std::size_t{4} << 20U, std::size_t{1} << 20U};
  return tradelab::detail::parse_json(tl_run_file_verify(text.c_str(), nullptr, 0, nullptr), roomy);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string text(reinterpret_cast<const char*>(data), size);  // a C string ends at the first NUL
  const auto first = verdict(text);
  if (!first.ok || first.value.find("ok") == nullptr) std::abort();
  if (!first.value.find("ok")->boolean) return 0;
  const auto again = verdict(field(first.value, "run"));
  if (!again.ok || field(again.value, "fileDigest") != field(first.value, "fileDigest")) std::abort();
  return 0;
}
