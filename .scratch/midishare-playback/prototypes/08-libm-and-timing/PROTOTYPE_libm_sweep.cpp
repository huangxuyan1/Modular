// PROTOTYPE, throwaway: midishare-playback ticket 08. Do the library calls a soundfont node
// would make return the same bits under MSVC's runtime and Emscripten's musl? And does a
// small routine written with only + - * / and ldexp return the same bits in both?
//
//   PROTOTYPE_libm_sweep <out-folder> [nofma3]
//
// Writes one file of raw doubles per test into <out-folder> and prints, per test, a hash of
// its inputs (so compare.py can confirm both builds fed the same values), and timings.
// "nofma3" (MSVC only) turns off the CRT's FMA3 code paths first, to show that the native
// runtime's own results depend on the CPU path it takes.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <math.h> // _set_FMA3_enable
#endif

namespace {

std::uint64_t splitmix(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

// A uniform double in [lo, hi) from 53 random bits: exact steps, the same on every target.
std::vector<double> uniform(std::uint64_t seed, std::size_t count, double lo, double hi) {
  std::vector<double> values(count);
  for (auto& v : values) {
    const double u = static_cast<double>(splitmix(seed) >> 11) * 0x1p-53;
    v = lo + (hi - lo) * u;
  }
  return values;
}

std::uint64_t fnv(const std::vector<double>& values) {
  std::uint64_t hash = 14695981039346656037ull;
  const auto* bytes = reinterpret_cast<const unsigned char*>(values.data());
  for (std::size_t i = 0; i < values.size() * sizeof(double); ++i)
    hash = (hash ^ bytes[i]) * 1099511628211ull;
  return hash;
}

// --- portable routines: only + - * /, floor and ldexp, which IEEE 754 makes exact or
// correctly rounded on every target, so they cannot differ between runtimes. -------------

// 2^x for |x| < 1000: x = n + f with f in [-0.5, 0.5], then e^(f ln 2) by its Taylor
// series to degree 13, whose truncation error is below 1e-17 there.
double portableExp2(double x) {
  const double n = std::floor(x + 0.5);
  const double t = (x - n) * 0.69314718055994530942;
  double p = 1.0 / 6227020800.0;
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

// sin x for 0 <= x <= 4: reduce by pi/2 (split in two parts so the reduction is exact
// enough), then a Taylor polynomial on [-pi/4, pi/4] picked by quadrant.
double sinPoly(double r) {
  const double r2 = r * r;
  double p = -1.0 / 1307674368000.0;
  p = p * r2 + 1.0 / 6227020800.0;
  p = p * r2 - 1.0 / 39916800.0;
  p = p * r2 + 1.0 / 362880.0;
  p = p * r2 - 1.0 / 5040.0;
  p = p * r2 + 1.0 / 120.0;
  p = p * r2 - 1.0 / 6.0;
  return r + r * r2 * p;
}
double cosPoly(double r) {
  const double r2 = r * r;
  double p = 1.0 / 20922789888000.0;
  p = p * r2 - 1.0 / 87178291200.0;
  p = p * r2 + 1.0 / 479001600.0;
  p = p * r2 - 1.0 / 3628800.0;
  p = p * r2 + 1.0 / 40320.0;
  p = p * r2 - 1.0 / 720.0;
  p = p * r2 + 1.0 / 24.0;
  return 1.0 - 0.5 * r2 + r2 * r2 * p;
}
double portableSin(double x) {
  constexpr double kPio2Hi = 1.57079632673412561417e+00; // 33 bits of pi/2
  constexpr double kPio2Lo = 6.07710050650619224932e-11; // the rest
  const double k = std::floor(x * 0.63661977236758134308 + 0.5);
  const double r = (x - k * kPio2Hi) - k * kPio2Lo;
  switch (static_cast<int>(k) & 3) {
  case 0: return sinPoly(r);
  case 1: return cosPoly(r);
  case 2: return -sinPoly(r);
  default: return -cosPoly(r);
  }
}

struct Test {
  std::string name;
  std::vector<double> inputs;
  double (*fn)(double);
};

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: PROTOTYPE_libm_sweep <out-folder> [nofma3]\n");
    return 2;
  }
  const std::string out = argv[1];
#if defined(_MSC_VER)
  if (argc > 2 && std::strcmp(argv[2], "nofma3") == 0) _set_FMA3_enable(0);
  std::printf("fma3 %d\n", _get_FMA3_enable());
#endif
  constexpr std::size_t kCount = 1000000;

  // The domains a SoundFont 2 voice converts: integer cents and timecents (-12000 to
  // 12000) and fractional ones from modulation, centibels to gain, the concave velocity
  // curve's log, equal-power pan angles, and a filter's angular frequency.
  std::vector<double> cents;
  for (int c = -12000; c <= 12000; ++c) cents.push_back(c / 1200.0);
  std::vector<double> centibels;
  for (int cb = 0; cb <= 1440; ++cb) centibels.push_back(-cb / 200.0);
  std::vector<double> panAngles;
  for (int p = -500; p <= 500; ++p) panAngles.push_back((p + 500) / 1000.0 * 1.5707963267948966);
  std::vector<double> velocity;
  for (int v = 1; v <= 127; ++v) velocity.push_back((127.0 * 127.0 - v * v) / (127.0 * 127.0) + 1e-9);
  const auto octaves = uniform(1, kCount, -20.0, 20.0);
  const auto decades = uniform(2, kCount, -7.2, 0.0);
  const auto naturals = uniform(3, kCount, -16.6, 0.0);
  const auto unit = uniform(4, kCount, 1e-6, 1.0);
  const auto angles = uniform(5, kCount, 0.0, 3.14159265358979);
  const auto halfAngles = uniform(6, kCount, 1e-6, 1.5707);

  const std::vector<Test> tests = {
      {"exp2_int_cents", cents, [](double x) { return std::exp2(x); }},
      {"pow2_int_cents", cents, [](double x) { return std::pow(2.0, x); }},
      {"exp2_rand", octaves, [](double x) { return std::exp2(x); }},
      {"pow2_rand", octaves, [](double x) { return std::pow(2.0, x); }},
      {"pow10_int_cb", centibels, [](double x) { return std::pow(10.0, x); }},
      {"pow10_rand", decades, [](double x) { return std::pow(10.0, x); }},
      {"exp_rand", naturals, [](double x) { return std::exp(x); }},
      {"log10_velocity", velocity, [](double x) { return std::log10(x); }},
      {"log10_rand", unit, [](double x) { return std::log10(x); }},
      {"sin_pan", panAngles, [](double x) { return std::sin(x); }},
      {"cos_pan", panAngles, [](double x) { return std::cos(x); }},
      {"sin_rand", angles, [](double x) { return std::sin(x); }},
      {"cos_rand", angles, [](double x) { return std::cos(x); }},
      {"tan_rand", halfAngles, [](double x) { return std::tan(x); }},
      {"exp2f_rand", octaves,
       [](double x) { return static_cast<double>(std::exp2(static_cast<float>(x))); }},
      {"powf10_rand", decades,
       [](double x) { return static_cast<double>(std::pow(10.0f, static_cast<float>(x))); }},
      {"sinf_rand", angles,
       [](double x) { return static_cast<double>(std::sin(static_cast<float>(x))); }},
      {"portable_exp2_int_cents", cents, portableExp2},
      {"portable_exp2_rand", octaves, portableExp2},
      {"portable_sin_pan", panAngles, portableSin},
      {"portable_sin_rand", angles, portableSin},
  };

  for (const auto& test : tests) {
    std::vector<double> results(test.inputs.size());
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < results.size(); ++i) results[i] = test.fn(test.inputs[i]);
    const auto ns = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - start).count();
    const std::string path = out + "/" + test.name + ".bin";
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
    std::fwrite(results.data(), sizeof(double), results.size(), file);
    std::fclose(file);
    std::printf("%s inputs=%zu input_hash=%016llx ns_per_call=%.2f\n", test.name.c_str(),
                test.inputs.size(), static_cast<unsigned long long>(fnv(test.inputs)),
                ns / static_cast<double>(results.size()));
  }
  return 0;
}
