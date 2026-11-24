// PROTOTYPE, throwaway: midishare-playback ticket 02. Why do the native and WebAssembly
// renders agree bit for bit? SineSynthNode works in double (std::pow for the pitch,
// std::sin for the wave) and only its output is a float. This replays that arithmetic for
// every note of midishare's roll (MIDI 36 to 95) at both rates for 30 seconds of samples,
// and prints, per note and rate, the pitch increment's bits and hashes of the double sine
// values and of the float outputs. build.ps1 builds it both ways; node_run.mjs compares.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
std::uint64_t fnv(std::uint64_t hash, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
  return hash;
}
} // namespace

int main(int argc, char** argv) {
  // With a path, also write every double sine value of notes 36, 69 and 95 at 48 kHz there,
  // so the differences can be counted (the WebAssembly build needs -sNODERAWFS=1 for it).
  std::FILE* dump = argc > 1 ? std::fopen(argv[1], "wb") : nullptr;
  const double rates[] = {44100.0, 48000.0};
  for (const double rate : rates) {
    for (int note = 36; note <= 95; ++note) {
      // SineSynthNode::handle and process, as written in MidiNodes.h.
      const double freq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
      const double phaseInc = 2.0 * 3.14159265358979323846 * freq / rate;
      double phase = 0.0;
      std::uint64_t doubles = 14695981039346656037ull, floats = doubles;
      const auto frames = static_cast<std::uint32_t>(rate * 30);
      for (std::uint32_t i = 0; i < frames; ++i) {
        const double s = std::sin(phase);
        const float out = 0.5f * static_cast<float>(s);
        doubles = fnv(doubles, &s, sizeof s);
        floats = fnv(floats, &out, sizeof out);
        if (dump && rate == 48000.0 && (note == 36 || note == 69 || note == 95))
          std::fwrite(&s, sizeof s, 1, dump);
        phase += phaseInc;
      }
      std::uint64_t incBits;
      std::memcpy(&incBits, &phaseInc, sizeof incBits);
      std::printf("%.0f %d %016llx %016llx %016llx\n", rate, note,
                  static_cast<unsigned long long>(incBits),
                  static_cast<unsigned long long>(doubles),
                  static_cast<unsigned long long>(floats));
    }
  }
  if (dump) std::fclose(dump);
  return 0;
}
