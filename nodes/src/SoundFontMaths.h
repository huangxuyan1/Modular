#pragma once

#include <cmath>

// Every transcendental function whose result reaches the soundfont node's
// output is computed here, from operations IEEE 754 makes exact or correctly
// rounded: + - * /, floor, ldexp, frexp and conversions. Maths libraries
// disagree on exp2, pow, log10, sin and cos in the last place: MSVC's with
// Emscripten's, and MSVC's Debug build with its Release build. Map ticket 08
// decided that a browser render matches the native one bit for bit, so the node
// calls none of them. No build fuses these operations (the root
// CMakeLists.txt), so each routine gives the same bits in every build. Over the
// node's inputs each is within a few units in the last place of the true value,
// far inside the node's tolerances. A change here changes the rendered bits, so
// it updates the soundfont tests' golden values in the same commit.
//
// The series' coefficients are quotients of integers that doubles hold exactly,
// which a compiler folds with the correct rounding IEEE 754 requires of the
// division at run time.
namespace modular::nodes::soundfont_maths {

namespace detail {

// sin r for |r| a little over pi/4, by its Taylor series to r^17, whose
// remainder there is below 1e-19.
inline double sinNear(double r) {
  const double r2 = r * r;
  double p = 1.0 / 355687428096000.0; // 1/17!
  p = p * r2 - 1.0 / 1307674368000.0;
  p = p * r2 + 1.0 / 6227020800.0;
  p = p * r2 - 1.0 / 39916800.0;
  p = p * r2 + 1.0 / 362880.0;
  p = p * r2 - 1.0 / 5040.0;
  p = p * r2 + 1.0 / 120.0;
  p = p * r2 - 1.0 / 6.0;
  return r + r * r2 * p;
}

// cos r for |r| a little over pi/4, by its Taylor series to r^18.
inline double cosNear(double r) {
  const double r2 = r * r;
  double p = -1.0 / 6402373705728000.0; // -1/18!
  p = p * r2 + 1.0 / 20922789888000.0;
  p = p * r2 - 1.0 / 87178291200.0;
  p = p * r2 + 1.0 / 479001600.0;
  p = p * r2 - 1.0 / 3628800.0;
  p = p * r2 + 1.0 / 40320.0;
  p = p * r2 - 1.0 / 720.0;
  p = p * r2 + 1.0 / 24.0;
  return 1.0 - 0.5 * r2 + r2 * r2 * p;
}

// x less k pi/2, for the whole number k nearest x / (pi/2), and |x| below
// 2^20. pi/2 is split into its first 33 bits, whose product with k is then
// exact, and the rest (fdlibm's constants).
inline double quadrant(double x, long long& k) {
  const double n = std::floor(x * 0.63661977236758134308 + 0.5); // 2/pi
  k = static_cast<long long>(n);
  return (x - n * 1.57079632673412561417) - n * 6.07710050650619224932e-11;
}

} // namespace detail

// 2^x for |x| below 1000: x = n + f with n whole and |f| at most 1/2, so
// 2^f = e^(f ln 2), by its Taylor series to degree 13, whose remainder there
// is below 1e-17. ldexp applies 2^n exactly.
inline double exp2(double x) {
  const double n = std::floor(x + 0.5);
  const double t = (x - n) * 0.69314718055994530942; // ln 2
  double p = 1.0 / 6227020800.0; // 1/13!
  p = p * t + 1.0 / 479001600.0;
  p = p * t + 1.0 / 39916800.0;
  p = p * t + 1.0 / 3628800.0;
  p = p * t + 1.0 / 362880.0;
  p = p * t + 1.0 / 40320.0;
  p = p * t + 1.0 / 5040.0;
  p = p * t + 1.0 / 720.0;
  p = p * t + 1.0 / 120.0;
  p = p * t + 1.0 / 24.0;
  p = p * t + 1.0 / 6.0;
  p = p * t + 0.5;
  p = p * t + 1.0;
  p = p * t + 1.0;
  return std::ldexp(p, static_cast<int>(n));
}

// 10^x for |x| below 300, as 2^(x log2 10): centibels to gain.
inline double exp10(double x) { return exp2(x * 3.32192809488736234787); }

// log10 x for finite x above 0. x = m 2^e with m from sqrt(1/2) to sqrt(2), so
// ln m = 2 atanh s, with s = (m - 1) / (m + 1) below 0.172 in size, by its
// series to s^23, whose remainder is below 1e-19. log10 1 is exactly 0.
inline double log10(double x) {
  int e = 0;
  double m = std::frexp(x, &e); // from 1/2 to 1
  if (m < 0.70710678118654752440) {
    m *= 2;
    --e;
  }
  const double s = (m - 1) / (m + 1);
  const double s2 = s * s;
  double p = 1.0 / 23;
  p = p * s2 + 1.0 / 21;
  p = p * s2 + 1.0 / 19;
  p = p * s2 + 1.0 / 17;
  p = p * s2 + 1.0 / 15;
  p = p * s2 + 1.0 / 13;
  p = p * s2 + 1.0 / 11;
  p = p * s2 + 1.0 / 9;
  p = p * s2 + 1.0 / 7;
  p = p * s2 + 1.0 / 5;
  p = p * s2 + 1.0 / 3;
  const double lnM = 2 * s + 2 * s * s2 * p;
  return e * 0.30102999566398119521 + lnM * 0.43429448190325182765; // log10 2, log10 e
}

// sin x and cos x for |x| below 2^20.
inline double sin(double x) {
  long long k = 0;
  const double r = detail::quadrant(x, k);
  switch (k & 3) {
  case 0: return detail::sinNear(r);
  case 1: return detail::cosNear(r);
  case 2: return -detail::sinNear(r);
  default: return -detail::cosNear(r);
  }
}

inline double cos(double x) {
  long long k = 0;
  const double r = detail::quadrant(x, k);
  switch (k & 3) {
  case 0: return detail::cosNear(r);
  case 1: return -detail::sinNear(r);
  case 2: return -detail::cosNear(r);
  default: return detail::sinNear(r);
  }
}

} // namespace modular::nodes::soundfont_maths
