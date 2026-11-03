#include <doctest/doctest.h>

#include <cmath>  // reference values in tests only; the engine itself must not use libm transcendentals
#include <cstring>
#include <limits>
#include <vector>

#include "tradelab/det_math.hpp"
#include "tradelab/sha256.hpp"

using namespace tradelab;

namespace {

double rel_err(double got, double want) {
  if (want == 0.0) return std::fabs(got);
  return std::fabs((got - want) / want);
}

// Bit pattern hash of kernel outputs over a fixed grid. The expected digest is
// frozen: any change in any bit on any target fails CI (cross-platform guard).
std::string kernel_grid_digest() {
  std::vector<std::uint8_t> bytes;
  auto push = [&bytes](double v) {
    std::uint64_t u = 0;
    std::memcpy(&u, &v, sizeof u);
    for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<std::uint8_t>(u >> (8 * i)));
  };
  for (int i = -2000; i <= 2000; ++i) {
    const double x = static_cast<double>(i) / 97.0;  // inexact grid points on purpose
    push(det::exp(x));
    push(det::log(std::fabs(x) + 1e-3));
    push(det::sin(x));
    push(det::cos(x));
    push(det::norm_cdf(x / 2.5));
    push(det::sqrt(std::fabs(x)));
  }
  return to_hex(sha256(bytes));
}

}  // namespace

TEST_CASE("exp matches reference within 1e-15 relative") {
  for (int i = -7000; i <= 7000; ++i) {
    const double x = static_cast<double>(i) / 10.0 + 0.0123;
    CHECK_MESSAGE(rel_err(det::exp(x), std::exp(x)) < 1e-15, "x=" << x);
  }
  CHECK(det::exp(0.0) == 1.0);
  CHECK(det::exp(1.0) == doctest::Approx(2.718281828459045).epsilon(1e-15));
  CHECK(det::exp(1000.0) == std::numeric_limits<double>::infinity());
  CHECK(det::exp(-1000.0) == 0.0);
  CHECK(std::isnan(det::exp(std::numeric_limits<double>::quiet_NaN())));
}

TEST_CASE("log matches reference within 1e-15 relative") {
  for (int i = 1; i <= 20000; ++i) {
    const double x = static_cast<double>(i) * 0.0137;
    CHECK_MESSAGE(rel_err(det::log(x), std::log(x)) < 1e-15, "x=" << x);
  }
  for (double x = 1e-300; x < 1e300; x *= 1e7) CHECK(rel_err(det::log(x), std::log(x)) < 1e-15);
  CHECK(det::log(1.0) == 0.0);
  CHECK(det::log(0.0) == -std::numeric_limits<double>::infinity());
  CHECK(std::isnan(det::log(-1.0)));
}

TEST_CASE("sin/cos match reference within 1e-15 absolute") {
  for (int i = -100000; i <= 100000; ++i) {
    const double x = static_cast<double>(i) * 0.000731;
    CHECK_MESSAGE(std::fabs(det::cos(x) - std::cos(x)) < 1e-15, "x=" << x);
    CHECK_MESSAGE(std::fabs(det::sin(x) - std::sin(x)) < 1e-15, "x=" << x);
  }
  CHECK(std::fabs(det::cos(1e6) - std::cos(1e6)) < 1e-12);
}

TEST_CASE("norm_cdf matches published values") {
  CHECK(det::norm_cdf(0.0) == 0.5);
  CHECK(det::norm_cdf(1.96) == doctest::Approx(0.9750021048517795).epsilon(1e-15));
  CHECK(det::norm_cdf(-1.0) == doctest::Approx(0.15865525393145707).epsilon(1e-14));
  CHECK(det::norm_cdf(-3.5) == doctest::Approx(2.3262907903552504e-04).epsilon(1e-13));
  CHECK(det::norm_cdf(-5.0) == doctest::Approx(2.866515718791939e-07).epsilon(1e-13));
  CHECK(det::norm_cdf(-10.0) == doctest::Approx(7.619853024160527e-24).epsilon(1e-12));
  CHECK(det::norm_cdf(50.0) == 1.0);
  CHECK(det::norm_cdf(-50.0) == 0.0);
}

TEST_CASE("norm_cdf matches erfc reference and is monotone, symmetric") {
  double prev = 0.0;
  for (int i = -800; i <= 800; ++i) {
    const double x = static_cast<double>(i) / 100.0;
    const double want = 0.5 * std::erfc(-x / std::sqrt(2.0));
    const double got = det::norm_cdf(x);
    CHECK_MESSAGE(std::fabs(got - want) < 5e-16, "x=" << x);  // <= ~2 ulp near 1
    if (x < -3.0) CHECK_MESSAGE(rel_err(got, want) < 1e-13, "tail x=" << x);
    CHECK(got >= prev);
    CHECK(std::fabs(got + det::norm_cdf(-x) - 1.0) < 5e-16);
    prev = got;
  }
}

TEST_CASE("kernel outputs are bit-frozen (cross-target golden)") {
  CHECK(kernel_grid_digest() == "248647cee27f00cb91a37e5a7b78f8b64561effce62923b212544667c0bd14d2");
}
