// Writes the web smoke test's soundfont, so the test needs no licensed file:
//
//   web_smoke_fixture <path>
//
// One looping sine, panned full left, under a volume envelope with a half-second
// release. A note therefore sounds on the left channel only, and its release runs
// past the file's end.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "SoundFontTestFiles.h"

using sf2test::set;
namespace gen = sf2test::gen;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: web_smoke_fixture <path>\n");
    return 2;
  }
  std::vector<std::int16_t> points(4410);
  for (std::size_t i = 0; i < points.size(); ++i)
    points[i] = static_cast<std::int16_t>(
        std::lround(16000 * std::sin(2 * 3.14159265358979 * 0.01 * i)));
  sf2test::Sample tone{"tone", points, 44100, 57};
  tone.loop = sf2test::Loop{100, 4100};

  sf2test::File file;
  file.samples.push_back(tone);
  file.instruments.push_back(
      {"tone",
       {sf2test::Zone{{set(gen::kAttackVolEnv, -7000), set(gen::kReleaseVolEnv, -1200),
                       set(gen::kPan, -500), set(gen::kSampleModes, 1),
                       set(gen::kSampleId, 0)}}}});
  file.presets.push_back({"tone", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  const auto bytes = sf2test::build(file);

  std::FILE* out = std::fopen(argv[1], "wb");
  if (!out) {
    std::fprintf(stderr, "web_smoke_fixture: cannot write %s\n", argv[1]);
    return 1;
  }
  const bool written = std::fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
  return std::fclose(out) == 0 && written ? 0 : 1;
}
