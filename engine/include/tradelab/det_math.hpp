#pragma once

// Deterministic transcendental kernels (design D3).
//
// Platform libm results differ in the last ulp between glibc, Apple libm and
// Emscripten/musl, which would break cross-target digests. These kernels use
// only IEEE-754 correctly-rounded operations (+ - * / sqrt) and exact
// power-of-two scaling, compiled with -ffp-contract=off, so the same input
// yields the same bits on every target. Accuracy target: ~1e-15 relative.
//
// Results that affect state must be quantized to fixed-point immediately.

namespace tradelab::det {

double sqrt(double x);   // IEEE correctly rounded; NaN for x < 0
double exp(double x);    // 0 for x < -745, +inf for x > 709.78
double log(double x);    // NaN for x < 0, -inf for 0
double cos(double x);    // accurate for |x| <= 1e6 (Box-Muller uses [0, 2pi))
double sin(double x);    // accurate for |x| <= 1e6
double norm_pdf(double x);
double norm_cdf(double x);  // standard normal CDF, absolute error ~1e-16

}  // namespace tradelab::det
