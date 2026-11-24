// PROTOTYPE, throwaway: midishare-playback ticket 08. MidiFile::parse sums event times in
// `long double` seconds and MidiFileNode::prepare rounds seconds * rate to a frame, also in
// `long double`. MSVC's long double is 64-bit, wasm32's a 128-bit software float. Do the
// two builds put every event on the same frame? And does the same code in `double` agree?
//
//   PROTOTYPE_frames > <out.txt>
//
// For each tempo of a whole-number quarter-note BPM from 30 to 300 (abcjs writes the tempo
// as whole microseconds), 480 ticks per quarter as abcjs writes, and four rates, it places
// an event every 40 ticks (the grid of sixteenths, eighth triplets and sextuplets) up to
// ten minutes, and every tick up to one minute. It sums the times exactly as
// MidiFile::parse does and rounds them exactly as MidiFileNode::prepare does, then prints
// every event whose frame differs from the exact frame (rational arithmetic, round half
// away from zero, as std::round), and totals.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace {

template <class Real>
void run(const char* label, std::uint32_t grid, std::uint64_t maxSeconds,
         std::uint64_t& events, std::uint64_t& wrong, std::uint64_t& ties) {
  constexpr std::uint64_t kDivision = 480;
  const double rates[] = {22050.0, 44100.0, 48000.0, 96000.0};
  for (std::uint32_t bpm = 30; bpm <= 300; ++bpm) {
    const std::uint32_t tempo = (60000000u + bpm / 2) / bpm;
    // maxSeconds of ticks at this tempo.
    const std::uint64_t lastTick = maxSeconds * 1000000ull * kDivision / tempo;
    for (const double rate : rates) {
      const auto irate = static_cast<std::uint64_t>(rate);
      std::uint64_t previous = 0;
      Real seconds = 0;
      for (std::uint64_t tick = grid; tick <= lastTick; tick += grid) {
        // MidiFile::parse, as written.
        seconds += static_cast<Real>(tick - previous) * tempo /
                   (static_cast<Real>(kDivision) * 1000000);
        previous = tick;
        // MidiFileNode::prepare's frameAt, as written.
        const Real frame = std::round(seconds * rate);
        const auto got = static_cast<std::uint64_t>(frame);
        // Exact: tick * tempo * rate / (division * 1e6), rounded half away from zero.
        const std::uint64_t num = tick * tempo * irate;
        const std::uint64_t den = kDivision * 1000000ull;
        const std::uint64_t exact = (2 * num + den) / (2 * den);
        const bool tie = (2 * num) % (2 * den) == den;
        ++events;
        if (tie) ++ties;
        if (got != exact) {
          ++wrong;
          std::printf("%s grid=%u bpm=%u rate=%.0f tick=%llu frame=%llu exact=%llu tie=%d\n",
                      label, grid, bpm, rate, static_cast<unsigned long long>(tick),
                      static_cast<unsigned long long>(got),
                      static_cast<unsigned long long>(exact), tie ? 1 : 0);
        }
      }
    }
  }
}

} // namespace

int main() {
  std::printf("sizeof(long double)=%zu\n", sizeof(long double));
  // Every 40 ticks for ten minutes, then every tick for the first minute.
  for (const std::uint32_t grid : {40u, 1u}) {
    const std::uint64_t maxSeconds = grid == 40 ? 600 : 60;
    std::uint64_t events = 0, wrong = 0, ties = 0;
    run<long double>("long_double", grid, maxSeconds, events, wrong, ties);
    std::printf("TOTAL long_double grid=%u events=%llu ties=%llu off_by_one=%llu\n", grid,
                static_cast<unsigned long long>(events), static_cast<unsigned long long>(ties),
                static_cast<unsigned long long>(wrong));
    events = wrong = ties = 0;
    run<double>("double", grid, maxSeconds, events, wrong, ties);
    std::printf("TOTAL double grid=%u events=%llu ties=%llu off_by_one=%llu\n", grid,
                static_cast<unsigned long long>(events), static_cast<unsigned long long>(ties),
                static_cast<unsigned long long>(wrong));
  }
  return 0;
}
