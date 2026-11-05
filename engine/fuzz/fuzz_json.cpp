// libFuzzer target: the strict JSON reader and RFC 8785 canonicalization. Invariant: the
// canonical form of any accepted document parses again and is a fixed point.

#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "../src/json_in.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  const auto parsed = tradelab::detail::parse_json(text);
  if (!parsed.ok) return 0;
  const std::string canonical = tradelab::detail::canonical_json(parsed.value);
  tradelab::detail::JsonLimits roomy;
  roomy.max_bytes = canonical.size() + 1;
  const auto again = tradelab::detail::parse_json(canonical, roomy);
  if (!again.ok || tradelab::detail::canonical_json(again.value) != canonical) std::abort();
  return 0;
}
