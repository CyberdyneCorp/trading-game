#pragma once

// Enumerations shared by state, commands, events and the C ABI.
// Numeric values are part of the recorded-run format: never renumber, only append.

#include <cstdint>

namespace tradelab {

enum class OrderSide : std::uint8_t { Buy = 0, Sell = 1 };

enum class OrderType : std::uint8_t {
  Market = 0,
  Limit = 1,
  StopMarket = 2,
  TakeProfit = 3,  // limit order on the exit side, always reduce-only
  TrailingStop = 4,  // stop-market whose trigger ratchets with closes (effective next bar)
};

enum class OrderStatus : std::uint8_t {
  Accepted = 0,
  PartiallyFilled = 1,
  Filled = 2,
  Cancelled = 3,
  Rejected = 4,
  Expired = 5,
  Triggered = 6,
};

enum class Reason : std::uint8_t {
  None = 0,
  InsufficientMargin = 1,
  LeverageAboveCap = 2,
  InvalidTick = 3,
  InvalidQuantity = 4,
  UnsupportedOrderType = 5,
  ReduceOnlyWouldIncrease = 6,
  RunBankrupt = 7,
  RunFinished = 8,
  InsufficientCapacity = 9,
  Liquidation = 10,
  PositionClosed = 11,
  UserCancelled = 12,
  UnknownOrder = 13,
  InvalidSequence = 14,
  WrongDecisionStep = 15,
  NumericOverflow = 16,
  InvalidLeverage = 17,
  PositionOpen = 18,
  StopBeyondLiquidation = 19,  // informational flag on accepted orders
  ShortNotAllowed = 20,
  Expired = 21,
  InvalidBracket = 22,
  OcoSiblingFilled = 23,
  SeriesNotListed = 24,
  ShortOptionsNotAllowed = 25,
  NoBid = 26,
  MarginCall = 27,
  MarginBelowInitial = 28,
  NoQuestionDue = 29,
  InvalidPlan = 30,
  NoAdviceLeft = 31,  // RequestAdvice with no allowance left (changes no state)
  OrderMoved = 32,    // OrderUpdate reason: a working exit moved to a new trigger (MoveOrder)
};

enum class EventType : std::uint8_t {
  OrderUpdate = 0,  // status change of an order (see OrderStatus)
  Fill = 1,
  Funding = 2,
  Liquidation = 3,
  Bankrupt = 4,
  RunFinished = 5,
  LeverageChanged = 6,
  OptionSettlement = 7,
  MarginCall = 8,
  MarginAdjusted = 9,
};

enum class RunStatus : std::uint8_t { Active = 0, Finished = 1, Bankrupt = 2 };

// Advisor (spec advisor): what the advice says and why. Short/Wait/Long = -1/0/+1.
enum class AdviceStance : std::int8_t { Short = -1, Wait = 0, Long = 1 };

enum class AdviceReasonCode : std::uint8_t {
  RegimeTrendUp = 0,
  RegimeTrendDown = 1,
  RegimeRange = 2,
  RsiExtreme = 3,
  BandFade = 4,
  MacdMomentum = 5,
  Pattern = 6,
  NewsPriced = 7,
  NewsEarly = 8,
  FundingCrowded = 9,
  LowVolSqueeze = 10,
};

// Bit flags on events.
enum EventFlag : std::uint8_t {
  kFlagNone = 0,
  kFlagAmbiguousIntrabar = 1U << 0U,
  kFlagCapacityFromBarVolume = 1U << 1U,
  kFlagGapFill = 1U << 2U,
  kFlagMarkFromLastPrice = 1U << 3U,
};

constexpr int side_sign(OrderSide s) { return s == OrderSide::Buy ? 1 : -1; }
constexpr OrderSide opposite(OrderSide s) { return s == OrderSide::Buy ? OrderSide::Sell : OrderSide::Buy; }

}  // namespace tradelab
