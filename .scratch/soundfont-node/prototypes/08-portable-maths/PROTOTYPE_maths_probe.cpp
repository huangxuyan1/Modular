// PROTOTYPE, throwaway: soundfont-node ticket 08. On the inputs the soundfont
// node passes its maths seam (nodes/src/SoundFontMaths.h), and on a million
// random values per function, compares std:: with the seam. It prints each set's
// output hashes, so two builds can be compared, and the seam's distance from
// std:: in units in the last place, and writes every value to <dump.bin> for
// compare.py. Inputs computed with std:: (the filter's angles, the decay
// factors' arguments, the gains' velocity term) may differ between builds;
// compare.py says so.
//
//   PROTOTYPE_maths_probe <dump.bin>
//
// Built with nodes/src on the include path:
//   MSVC:   cl /std:c++20 /EHsc /I nodes/src PROTOTYPE_maths_probe.cpp, with CMake's
//             Release (/O2 /MD) or Debug (/Od /RTC1 /MDd) flags
//   wasm:   em++ -std=c++20 -O2 -ffp-contract=off -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1
//             -I nodes/src -o probe.js PROTOTYPE_maths_probe.cpp, run with node
//   then:   python -I compare.py <one dump.bin> <another dump.bin>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

#include "SoundFontMaths.h"

namespace {

namespace seam = modular::nodes::soundfont_maths;

std::uint64_t bits(double v) {
  std::uint64_t b;
  std::memcpy(&b, &v, 8);
  return b;
}

// Signed distance in ulps between two finite doubles of the same sign.
long long ulps(double a, double b) {
  const auto key = [](double v) {
    const auto u = static_cast<long long>(bits(v));
    return u < 0 ? static_cast<long long>(0x8000000000000000ull) - u : u;
  };
  return key(a) - key(b);
}

struct Set {
  std::string name;
  std::vector<double> in;
  std::function<double(double)> std_, seam_;
};

std::vector<double> uniform(std::uint64_t seed, std::size_t count, double lo, double hi) {
  std::vector<double> v(count);
  for (auto& x : v) {
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    x = lo + (hi - lo) * (static_cast<double>(seed >> 11) * 0x1p-53);
  }
  return v;
}

double stdConcave(int v) { return v > 0 ? -400 * std::log10(v / 127.0) : 960; }

} // namespace

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "dump.bin";
  const auto exp2s = [](double x) { return std::exp2(x); };
  const auto pow10s = [](double x) { return std::pow(10.0, x); };
  const auto log10s = [](double x) { return std::log10(x); };
  const auto sins = [](double x) { return std::sin(x); };
  const auto coss = [](double x) { return std::cos(x); };
  const auto exp2p = [](double x) { return seam::exp2(x); };
  const auto exp10p = [](double x) { return seam::exp10(x); };
  const auto log10p = [](double x) { return seam::log10(x); };
  const auto sinp = [](double x) { return seam::sin(x); };
  const auto cosp = [](double x) { return seam::cos(x); };

  std::vector<Set> sets;
  {
    Set s{"exp2 timecents", {}, exp2s, exp2p};
    for (int tc = -12000; tc <= 8000; ++tc) s.in.push_back(tc / 1200.0);
    sets.push_back(s);
  }
  {
    Set s{"exp2 semitones", {}, exp2s, exp2p};
    for (int d = -127; d <= 127; ++d) s.in.push_back(d / 12.0);
    sets.push_back(s);
  }
  {
    Set s{"exp2 cutoff", {}, exp2s, exp2p};
    for (int c = -6000; c <= 16000; ++c) s.in.push_back(static_cast<double>(c - 6900) / 1200);
    sets.push_back(s);
  }
  sets.push_back({"exp2 random", uniform(1, 1000000, -20, 20), exp2s, exp2p});
  {
    Set s{"log10 velocity", {}, log10s, log10p};
    for (int v = 1; v <= 127; ++v) s.in.push_back(v / 127.0);
    sets.push_back(s);
  }
  sets.push_back({"log10 levels", uniform(2, 1000000, 1e-9, 1), log10s, log10p});
  sets.push_back({"log10 wide", uniform(3, 1000000, 1e-300, 1e300), log10s, log10p});
  {
    Set s{"exp10 gain", {}, pow10s, exp10p};
    for (int a = 0; a <= 1440; ++a)
      for (int v = 1; v <= 127; ++v) {
        double att = a + stdConcave(100) + stdConcave(v);
        if (att > 1440) att = 1440;
        s.in.push_back(-att / 200);
      }
    sets.push_back(s);
  }
  {
    Set s{"exp10 sustain", {}, pow10s, exp10p};
    for (int cb = 0; cb <= 1440; ++cb) s.in.push_back(-(cb / 10.0) / 20);
    sets.push_back(s);
  }
  {
    Set s{"exp10 factors", {}, pow10s, exp10p};
    for (double rate : {22050.0, 44100.0, 48000.0})
      for (int tc = -12000; tc <= 8000; ++tc) {
        const double framesPerDb = std::exp2(tc / 1200.0) * rate / 100;
        s.in.push_back(-1 / (20 * framesPerDb));
      }
    sets.push_back(s);
  }
  sets.push_back({"exp10 random", uniform(4, 1000000, -7.2, 0), pow10s, exp10p});
  {
    Set s{"sin pan", {}, sins, sinp};
    for (int p = -500; p <= 500; ++p)
      s.in.push_back((500 - p) / 1000.0 * std::numbers::pi / 2);
    sets.push_back(s);
  }
  for (int which = 0; which < 2; ++which) {
    Set s{which == 0 ? "sin filter" : "cos filter", {}, sins, sinp};
    if (which == 1) {
      s.std_ = coss;
      s.seam_ = cosp;
    }
    for (double rate : {22050.0, 44100.0, 48000.0})
      for (int c = -6000; c <= 16000; ++c) {
        const double hertz =
            std::fmin(440 * std::exp2(static_cast<double>(c - 6900) / 1200), 0.45 * rate);
        s.in.push_back(std::numbers::pi * hertz / rate);
      }
    sets.push_back(s);
  }
  sets.push_back({"sin random", uniform(5, 1000000, -10, 10), sins, sinp});
  sets.push_back({"cos random", uniform(6, 1000000, -10, 10), coss, cosp});

  std::FILE* f = std::fopen(out, "wb");
  for (auto& s : sets) {
    std::uint64_t ih = 1469598103934665603ull, sh = ih, ph = ih;
    std::vector<double> rs(s.in.size()), rp(s.in.size());
    long long worst = 0;
    std::size_t differ = 0;
    for (std::size_t i = 0; i < s.in.size(); ++i) {
      rs[i] = s.std_(s.in[i]);
      rp[i] = s.seam_(s.in[i]);
      ih = (ih ^ bits(s.in[i])) * 1099511628211ull;
      sh = (sh ^ bits(rs[i])) * 1099511628211ull;
      ph = (ph ^ bits(rp[i])) * 1099511628211ull;
      long long d = 0;
      if (rs[i] != rp[i]) {
        // Near zero, measure against the larger value's ulp instead.
        d = (rs[i] > 0) == (rp[i] > 0) ? ulps(rp[i], rs[i]) : 1LL << 60;
        ++differ;
      }
      if (d < 0) d = -d;
      if (d > worst) worst = d;
    }
    const auto n = static_cast<std::uint32_t>(s.name.size());
    std::fwrite(&n, 4, 1, f);
    std::fwrite(s.name.data(), 1, n, f);
    const std::uint64_t count = rs.size();
    std::fwrite(&count, 8, 1, f);
    std::fwrite(s.in.data(), 8, count, f);
    std::fwrite(rs.data(), 8, count, f);
    std::fwrite(rp.data(), 8, count, f);
    std::printf("%-15s n=%7zu in=%016llx std=%016llx seam=%016llx seam!=std %7zu (%5.2f%%) max %lld ulp\n",
                s.name.c_str(), rs.size(), (unsigned long long)ih, (unsigned long long)sh,
                (unsigned long long)ph, differ, 100.0 * differ / rs.size(), worst);
  }
  std::fclose(f);
}
