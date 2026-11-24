#pragma once

#include <cmath>

// Every transcendental function whose result reaches the soundfont node's
// output goes through here. The node's output must be bit-identical across
// compilers, and the standard libraries disagree on these functions in the
// last place: MSVC's and Emscripten's results differ, and MSVC's even vary
// with the machine's FMA support. These wrap the standard library for now; a
// later ticket replaces them with portable routines built only from
// operations IEEE 754 rounds exactly. Rounding to integers, ceil, + - * / and
// sqrt are exact already and need no wrapper.
namespace modular::nodes::soundfont_maths {

inline double exp2(double x) { return std::exp2(x); }
inline double exp10(double x) { return std::pow(10.0, x); }
inline double log10(double x) { return std::log10(x); }
inline double sin(double x) { return std::sin(x); }

} // namespace modular::nodes::soundfont_maths
