#include "tradelab/det_math.hpp"

#include <cmath>    // only exact operations: sqrt, floor, ldexp, frexp, isnan, isinf
#include <cstdint>
#include <limits>

namespace tradelab::det {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ln2 split so that k * kLn2Hi is exact for |k| < 2^11 (fdlibm constants).
constexpr double kLn2Hi = 6.93147180369123816490e-01;
constexpr double kLn2Lo = 1.90821492927058770002e-10;
constexpr double kInvLn2 = 1.44269504088896338700e+00;
constexpr double kSqrtHalf = 7.07106781186547524401e-01;

// pi/2 split in 33-bit pieces: q * piece is exact for |q| < 2^20.
constexpr double kPio2Part1 = 1.57079632673412561417e+00;
constexpr double kPio2Part2 = 6.07710050630396597660e-11;
constexpr double kPio2Part3 = 2.02226624871116645580e-21;
constexpr double kTwoOverPi = 6.36619772367581382433e-01;

constexpr double kInvSqrt2Pi = 3.98942280401432677940e-01;

// Polynomials evaluated with Horner's rule; coefficients are exact Taylor terms 1/n!.
double exp_poly(double r) {
  constexpr int kDegree = 14;
  double p = 1.0;
  for (int n = kDegree; n >= 1; --n) p = 1.0 + p * r / static_cast<double>(n);
  return p;
}

double sin_poly(double r) {  // |r| <= pi/4
  const double r2 = r * r;
  double p = 1.0;
  for (int n = 17; n >= 3; n -= 2) p = 1.0 - p * r2 / static_cast<double>(n * (n - 1));
  return r * p;
}

double cos_poly(double r) {  // |r| <= pi/4
  const double r2 = r * r;
  double p = 1.0;
  for (int n = 18; n >= 2; n -= 2) p = 1.0 - p * r2 / static_cast<double>(n * (n - 1));
  return p;
}

struct Reduced {
  double r;
  int quadrant;  // 0..3
};

Reduced reduce_pio2(double x) {
  const double q = std::floor(x * kTwoOverPi + 0.5);
  const double r = ((x - q * kPio2Part1) - q * kPio2Part2) - q * kPio2Part3;
  const auto qi = static_cast<std::int64_t>(q);
  return {r, static_cast<int>(((qi % 4) + 4) % 4)};
}

// Marsaglia (2004): Phi(x) = 1/2 + phi(x) * sum x^(2n+1) / (1*3*...*(2n+1)). Used for |x| <= 3.
double norm_cdf_series(double x) {
  double term = x;
  double sum = x;
  const double x2 = x * x;
  for (int n = 3; n < 400; n += 2) {
    term = term * x2 / static_cast<double>(n);
    const double next = sum + term;
    if (next == sum) break;
    sum = next;
  }
  return 0.5 + norm_pdf(x) * sum;
}

// Upper tail Q(t) = phi(t) / (t + 1/(t + 2/(t + 3/(t + ...)))) for t > 3, fixed depth, bottom-up.
double norm_upper_tail(double t) {
  constexpr int kDepth = 200;
  double cf = t;
  for (int k = kDepth; k >= 1; --k) cf = t + static_cast<double>(k) / cf;
  return norm_pdf(t) / cf;
}

}  // namespace

double sqrt(double x) { return std::sqrt(x); }

double exp(double x) {
  if (std::isnan(x)) return x;
  if (x > 709.782712893383973096) return kInf;
  if (x < -745.133219101941108420) return 0.0;
  const double k = std::floor(x * kInvLn2 + 0.5);
  const double r = (x - k * kLn2Hi) - k * kLn2Lo;
  return std::ldexp(exp_poly(r), static_cast<int>(k));
}

double log(double x) {
  if (std::isnan(x) || x < 0.0) return kNaN;
  if (x == 0.0) return -kInf;
  if (std::isinf(x)) return kInf;
  int e = 0;
  double m = std::frexp(x, &e);  // x = m * 2^e, m in [0.5, 1)
  if (m < kSqrtHalf) {
    m *= 2.0;
    e -= 1;
  }
  const double f = m - 1.0;
  const double s = f / (2.0 + f);  // |s| <= 0.1716
  const double s2 = s * s;
  double series = 0.0;
  for (int n = 25; n >= 3; n -= 2) series = (1.0 / static_cast<double>(n) + series) * s2;
  const double log_m = 2.0 * s + 2.0 * s * series;
  const double de = static_cast<double>(e);
  return de * kLn2Hi + (log_m + de * kLn2Lo);
}

double cos(double x) {
  if (std::isnan(x) || std::isinf(x)) return kNaN;
  const Reduced red = reduce_pio2(x);
  switch (red.quadrant) {
    case 0: return cos_poly(red.r);
    case 1: return -sin_poly(red.r);
    case 2: return -cos_poly(red.r);
    default: return sin_poly(red.r);
  }
}

double sin(double x) {
  if (std::isnan(x) || std::isinf(x)) return kNaN;
  const Reduced red = reduce_pio2(x);
  switch (red.quadrant) {
    case 0: return sin_poly(red.r);
    case 1: return cos_poly(red.r);
    case 2: return -sin_poly(red.r);
    default: return -cos_poly(red.r);
  }
}

double norm_pdf(double x) { return kInvSqrt2Pi * exp(-0.5 * x * x); }

double norm_cdf(double x) {
  if (std::isnan(x)) return x;
  if (x > 40.0) return 1.0;
  if (x < -40.0) return 0.0;
  if (x > 3.0) return 1.0 - norm_upper_tail(x);
  if (x < -3.0) return norm_upper_tail(-x);
  return norm_cdf_series(x);
}

}  // namespace tradelab::det
