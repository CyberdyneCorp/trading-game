#pragma once

#include <cstdint>
#include <utility>

namespace tradelab {

// Stable error codes: values are part of the C ABI and of recorded runs. Never renumber.
enum class ErrorCode : std::uint16_t {
  Ok = 0,
  NumericOverflow = 1,
  DivisionByZero = 2,
  InvalidDecimal = 3,
  InvalidScale = 4,
  TruncatedInput = 5,
  NonCanonicalEncoding = 6,
  IncompatiblePolicy = 7,
  ScenarioMismatch = 8,
};

// Minimal expected-like type (C++20 has no std::expected; exceptions are disabled on wasm).
template <typename T>
class [[nodiscard]] Result {
 public:
  constexpr Result(T value) : value_(std::move(value)), error_(ErrorCode::Ok) {}  // NOLINT(google-explicit-constructor)
  constexpr Result(ErrorCode error) : value_(), error_(error) {}                   // NOLINT(google-explicit-constructor)

  [[nodiscard]] constexpr bool ok() const { return error_ == ErrorCode::Ok; }
  [[nodiscard]] constexpr ErrorCode error() const { return error_; }
  // Precondition: ok(). Callers must check; reading a failed result returns a default value.
  [[nodiscard]] constexpr const T& value() const { return value_; }

 private:
  T value_;
  ErrorCode error_;
};

}  // namespace tradelab
