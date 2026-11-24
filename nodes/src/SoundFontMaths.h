#pragma once

#include <cmath>

// Every transcendental function whose result reaches the soundfont node's
// output goes through here. The standard libraries disagree on these
// functions in the last place: MSVC's and Emscripten's results differ. Map
// ticket 08 decided that a browser render must match the native one bit for
// bit, so the soundfont spec's ticket 08 replaces these with portable routines
// built only from operations IEEE 754 rounds exactly; until then they wrap the
// standard library. Rounding to integers, ceil, fmod, + - * / and sqrt are
// exact already and need no wrapper.
namespace modular::nodes::soundfont_maths {

inline double exp2(double x) { return std::exp2(x); }
inline double exp10(double x) { return std::pow(10.0, x); }
inline double log10(double x) { return std::log10(x); }
inline double sin(double x) { return std::sin(x); }
inline double cos(double x) { return std::cos(x); }

} // namespace modular::nodes::soundfont_maths
