// Writes the web smoke test's fixtures, so the test needs no licensed file:
//
//   web_smoke_fixture <soundfont> <golden directory>
//
// The soundfont is one looping sine, panned full left, under a volume envelope
// with a half-second release. A note therefore sounds on the left channel only,
// and its release runs past the file's end.
//
// The golden directory receives the golden preview of WebGoldenClips.h, which the
// smoke test renders through JavaScript: its soundfont (preview.sf2), its MIDI
// bytes (preview.mid) and its golden values at 48 kHz (preview.json). The render
// core's tests check the same values natively and under Node.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "SoundFontTestFiles.h"
#include "WebGoldenClips.h"

using sf2test::set;
namespace gen = sf2test::gen;

namespace {

bool write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::FILE* out = std::fopen(path.string().c_str(), "wb");
  if (!out) {
    std::fprintf(stderr, "web_smoke_fixture: cannot write %s\n", path.string().c_str());
    return false;
  }
  const bool written = std::fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
  return std::fclose(out) == 0 && written;
}

std::vector<std::uint8_t> smokeSoundFont() {
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
  return sf2test::build(file);
}

// A golden render as JSON: its rate, its frame count and its block hashes, each
// as a hexadecimal string, since a JSON number cannot hold 64 bits exactly.
std::vector<std::uint8_t> goldenJson(const golden::Render& render) {
  char text[64];
  std::snprintf(text, sizeof text, "{\"rate\": %.0f, \"frames\": %llu, \"blocks\": [",
                render.rate, static_cast<unsigned long long>(render.frames));
  std::string json = text;
  for (std::size_t b = 0; b < render.blocks.size(); ++b) {
    std::snprintf(text, sizeof text, "%s\"0x%016llx\"", b == 0 ? "" : ", ",
                  static_cast<unsigned long long>(render.blocks[b]));
    json += text;
  }
  json += "]}\n";
  return {json.begin(), json.end()};
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: web_smoke_fixture <soundfont> <golden directory>\n");
    return 2;
  }
  const std::filesystem::path goldenDirectory = argv[2];
  std::error_code error;
  std::filesystem::create_directories(goldenDirectory, error);
  if (error) {
    std::fprintf(stderr, "web_smoke_fixture: cannot create %s\n", argv[2]);
    return 1;
  }
  const golden::Render* preview = nullptr;
  for (const auto& render : webgolden::kPreviewGolden)
    if (render.rate == 48000) preview = &render;
  if (!preview) {
    std::fprintf(stderr, "web_smoke_fixture: no golden preview at 48 kHz\n");
    return 1;
  }
  const bool written = write(argv[1], smokeSoundFont()) &&
                       write(goldenDirectory / "preview.sf2", webgolden::soundFont()) &&
                       write(goldenDirectory / "preview.mid", webgolden::previewClip()) &&
                       write(goldenDirectory / "preview.json", goldenJson(*preview));
  return written ? 0 : 1;
}
