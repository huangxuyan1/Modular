// PROTOTYPE, throwaway: web build ticket 08's native half. Renders the default piano's
// extract natively, through the render core, as the module renders it under Node
// (PROTOTYPE_piano_node.mjs), and prints the golden values of tests/GoldenRender.h:
//
//   piano_native clip <out.mid>
//   piano_native render <soundfont> <clip.mid> <out.json> <rate>...
//
// `clip` writes the clip below. `render` renders it at each rate through a fresh
// modular::web::Instrument, the C++ behind the module's exports, and writes each rate's
// frame count, block hashes, peak and non-zero samples, and the render's wall time, as
// JSON. Nothing it reads or writes belongs in the repository.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "GoldenRender.h"
#include "Instrument.h"
#include "MidiTestFiles.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes readFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", path);
    std::exit(1);
  }
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const char* path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
  if (!out) {
    std::fprintf(stderr, "cannot write %s\n", path);
    std::exit(1);
  }
}

// The piano's velocity layers, from its preset zones (soundfont_extract.py preset): below 65,
// then eight of eight velocities each, the last 121 to 127.
constexpr int kLayers[][2] = {{1, 64},   {65, 72},   {73, 80},   {81, 88},  {89, 96},
                              {97, 104}, {105, 112}, {113, 120}, {121, 127}};

// The root key of each of its twenty key ranges, 0-26 up to 105-108, from its instrument
// zones' overriding root keys.
constexpr int kRoots[] = {26, 30, 34, 38, 42, 46, 50, 54, 58, 62,
                          66, 70, 74, 78, 84, 90, 94, 99, 104, 108};

// The clip, at 480 ticks a quarter and 500,000 us a quarter, so 960 ticks make a second:
// - nine sweeps of every key the piano plays, 0 to 108, one per velocity layer, each key at
//   the layer's lowest velocity on even keys and its highest on odd ones, a note every 23
//   ticks, each held 149 ticks: every key range at both edges of every layer;
// - then the root key of every key range, staggered 37 ticks apart and held 8 s at
//   velocity 100, so that every sample plays into its loop (the longest starts its loop
//   about 5.3 s in at its root);
// - last, a chord across the keyboard at velocity 127, held 487 ticks; the file ends as it
//   is let go, as a preview's does, so its release runs into the render's two-second tail
//   and the trim cuts it.
Bytes clip() {
  std::vector<miditest::Event> events;
  std::uint32_t tick = 7;
  for (const auto& layer : kLayers)
    for (int key = 0; key <= 108; ++key) {
      const int velocity = key % 2 == 0 ? layer[0] : layer[1];
      events.push_back(miditest::noteOn(tick, key, velocity));
      events.push_back(miditest::noteOff(tick + 149, key));
      tick += 23;
    }
  tick += 701;
  for (const int key : kRoots) {
    events.push_back(miditest::noteOn(tick, key, 100));
    events.push_back(miditest::noteOff(tick + 8 * 960, key));
    tick += 37;
  }
  tick += 8 * 960;
  for (const int key : {21, 33, 48, 60, 64, 67, 72, 79, 96, 108}) {
    events.push_back(miditest::noteOn(tick, key, 127));
    events.push_back(miditest::noteOff(tick + 487, key));
  }
  return miditest::midiFile(events, tick + 487);
}

std::string renderJson(const modular::web::Rendering& rendering, double milliseconds) {
  const auto values = golden::measure(rendering.sampleRate, rendering.left, rendering.right);
  float peak = 0;
  std::size_t nonZero = 0, lastAudible = 0;
  for (const auto* channel : {&rendering.left, &rendering.right})
    for (std::size_t i = 0; i < channel->size(); ++i) {
      const float magnitude = std::fabs((*channel)[i]);
      peak = std::max(peak, magnitude);
      if (magnitude != 0) ++nonZero;
      if (magnitude >= 1e-4f) lastAudible = std::max(lastAudible, i);
    }
  char text[200];
  std::snprintf(text, sizeof text,
                "{\"rate\": %.0f, \"frames\": %llu, \"peak\": %.9g, \"nonZero\": %zu, "
                "\"lastAudibleFrame\": %zu, \"renderMs\": %.1f, \"blocks\": [",
                values.rate, static_cast<unsigned long long>(values.frames), peak, nonZero,
                lastAudible, milliseconds);
  std::string json = text;
  for (std::size_t b = 0; b < values.blocks.size(); ++b) {
    std::snprintf(text, sizeof text, "%s\"0x%016llx\"", b == 0 ? "" : ", ",
                  static_cast<unsigned long long>(values.blocks[b]));
    json += text;
  }
  return json + "]}";
}

} // namespace

int main(int argc, char** argv) {
  const std::string command = argc > 1 ? argv[1] : "";
  if (command == "clip" && argc == 3) {
    const Bytes bytes = clip();
    writeFile(argv[2], std::string(bytes.begin(), bytes.end()));
    return 0;
  }
  if (command != "render" || argc < 6) {
    std::fprintf(stderr, "usage: piano_native clip <out.mid>\n"
                         "       piano_native render <soundfont> <clip.mid> <out.json> "
                         "<rate>...\n");
    return 2;
  }
  const Bytes soundFont = readFile(argv[2]);
  const Bytes midi = readFile(argv[3]);
  std::string json = "{\"renders\": [\n";
  for (int i = 5; i < argc; ++i) {
    const double rate = std::atof(argv[i]);
    modular::web::Instrument instrument(soundFont);
    const auto start = std::chrono::steady_clock::now();
    const auto rendering = instrument.render(midi, rate);
    const std::chrono::duration<double, std::milli> took =
        std::chrono::steady_clock::now() - start;
    json += renderJson(rendering, took.count()) + (i + 1 < argc ? ",\n" : "\n");
    std::printf("%.0f Hz: %zu frames, %.1f ms\n", rate, rendering.left.size(), took.count());
  }
  writeFile(argv[4], json + "]}\n");
  return 0;
}
