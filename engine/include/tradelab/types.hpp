#pragma once

// Enumerations shared by state, commands, events and the C ABI: generated from engine/idl/tradelab.idl
// into idl_types.hpp (numeric values are part of the recorded-run format: never renumber, only append).

#include <cstdint>

#include "tradelab/idl_types.hpp"

namespace tradelab {

// Bit flags on events.
enum EventFlag : std::uint8_t {
  kFlagNone = 0,
  kFlagAmbiguousIntrabar = 1U << 0U,
  kFlagCapacityFromBarVolume = 1U << 1U,
  kFlagGapFill = 1U << 2U,
  kFlagApproximate = 1U << 3U,  // option settlement approximated from coarse bars
};

constexpr int side_sign(OrderSide s) { return s == OrderSide::Buy ? 1 : -1; }
constexpr OrderSide opposite(OrderSide s) { return s == OrderSide::Buy ? OrderSide::Sell : OrderSide::Buy; }

}  // namespace tradelab
