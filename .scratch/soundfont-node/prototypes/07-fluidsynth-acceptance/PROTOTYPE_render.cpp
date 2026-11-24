// PROTOTYPE, throwaway: soundfont-node ticket 07. Renders MIDI files through the soundfont
// node natively, for the comparison with FluidSynth, and times the render surface's
// Instrument (apps/web) natively. Outside the build: run.ps1 compiles it with MSVC and
// CMake's Release flags. The results are in the ticket's comments
// (../../issues/07-acceptance-the-default-piano-against-fluidsynth.md).
//
//   PROTOTYPE_render render <font.sf2> <bank> <program> <clip.mid> <rate> <tail s> <out.wav>
//       TriggerAt(0) -> MidiFile -> SoundFont (the preset named) -> a stereo sink, in blocks
//       of 512 frames, for the file's frames plus the tail. Writes 32-bit float WAV.
//   PROTOTYPE_render ignored <font.sf2>
//       Prints the file's presets count, its lowest preset and its ignored list.
//   PROTOTYPE_render time <full.sf2> <extract.sf2> <preview.mid> <clip.mid>...
//       Times loading each soundfont into an Instrument, then, at 44.1 and 48 kHz on an
//       Instrument loaded with the extract: a note preview (the first render, then the median
//       of 51 more) and each clip (the median of 5). Wall-clock milliseconds from
//       std::chrono::steady_clock.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "Instrument.h"
#include "StereoSinkNode.h"
#include "modular/Engine.h"
#include "modular/Graph.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/MidiFileNode.h"
#include "modular/nodes/SoundFontNode.h"

using namespace modular;
using namespace modular::nodes;

namespace {

std::vector<std::uint8_t> readBytes(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error(std::string("cannot read ") + path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void put32(std::ofstream& out, std::uint32_t v) { out.write(reinterpret_cast<char*>(&v), 4); }
void put16(std::ofstream& out, std::uint16_t v) { out.write(reinterpret_cast<char*>(&v), 2); }

// WAVE_FORMAT_IEEE_FLOAT, two channels, interleaved.
void writeFloatWav(const char* path, const std::vector<float>& left,
                   const std::vector<float>& right, std::uint32_t rate) {
  std::ofstream out(path, std::ios::binary);
  const auto bytes = static_cast<std::uint32_t>(left.size() * 8);
  out.write("RIFF", 4);
  put32(out, 4 + 26 + 12 + 8 + bytes);
  out.write("WAVEfmt ", 8);
  put32(out, 18);
  put16(out, 3);
  put16(out, 2);
  put32(out, rate);
  put32(out, rate * 8);
  put16(out, 8);
  put16(out, 32);
  put16(out, 0);
  out.write("fact", 4);
  put32(out, 4);
  put32(out, static_cast<std::uint32_t>(left.size()));
  out.write("data", 4);
  put32(out, bytes);
  std::vector<float> frames(left.size() * 2);
  for (std::size_t i = 0; i < left.size(); ++i) {
    frames[2 * i] = left[i];
    frames[2 * i + 1] = right[i];
  }
  out.write(reinterpret_cast<const char*>(frames.data()),
            static_cast<std::streamsize>(frames.size() * 4));
}

int render(int argc, char** argv) {
  if (argc != 9) throw std::invalid_argument("render: wrong arguments");
  const auto font = readBytes(argv[2]);
  const auto bank = static_cast<std::uint16_t>(std::atoi(argv[3]));
  const auto program = static_cast<std::uint16_t>(std::atoi(argv[4]));
  const auto clip = readBytes(argv[5]);
  const double rate = std::atof(argv[6]);
  const double tail = std::atof(argv[7]);

  Graph graph;
  const auto startId = graph.addNode<TriggerAtNode>(0);
  const auto midiId = graph.addNode<MidiFileNode>();
  const auto synthId = graph.addNode<SoundFontNode>();
  const auto sinkId = graph.addNode<web::StereoSinkNode>();
  auto& start = static_cast<TriggerAtNode&>(graph.node(startId));
  auto& midi = static_cast<MidiFileNode&>(graph.node(midiId));
  auto& synth = static_cast<SoundFontNode&>(graph.node(synthId));
  auto& sink = static_cast<web::StereoSinkNode&>(graph.node(sinkId));
  synth.load(font);
  synth.setPreset(bank, program);
  midi.import(clip);
  graph.connect(startId, start.trigger(), midiId, midi.trigger());
  graph.connect(midiId, midi.midi(), synthId, synth.midiIn());
  graph.connect(synthId, synth.out(), sinkId, sink.in());

  constexpr std::uint32_t kBlock = 512;
  Engine engine(graph, rate, kBlock);
  const auto frames = midi.durationFrames() + static_cast<std::uint64_t>(tail * rate);
  sink.setCapacity(frames);
  while (engine.streamTime() < frames)
    engine.process(static_cast<std::uint32_t>(
        std::min<std::uint64_t>(kBlock, frames - engine.streamTime())));
  if (auto error = engine.errorMessage(); !error.empty()) throw std::runtime_error(error);
  const auto channels = sink.take();
  writeFloatWav(argv[8], channels.left, channels.right, static_cast<std::uint32_t>(rate));
  std::printf("rendered preset %u:%u, %llu frames at %.0f Hz -> %s\n", synth.bank(),
              synth.program(), static_cast<unsigned long long>(frames), rate, argv[8]);
  return 0;
}

int ignored(int argc, char** argv) {
  if (argc != 3) throw std::invalid_argument("ignored: wrong arguments");
  const auto font = SoundFont::parse(readBytes(argv[2]));
  const auto lowest = font.lowestPreset();
  std::printf("lowest preset %u:%u\n", lowest.bank, lowest.program);
  std::printf("ignored (%zu):\n", font.ignored().size());
  for (const auto& line : font.ignored()) std::printf("  %s\n", line.c_str());
  return 0;
}

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// The median, least and most of a set of timings.
struct Spread {
  double median = 0, least = 0, most = 0;
};

Spread spread(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const auto n = values.size();
  return {n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2, values.front(),
          values.back()};
}

// Keeps a result alive so the optimizer cannot drop a render.
volatile double keepAlive = 0;

int timings(int argc, char** argv) {
  if (argc < 6) throw std::invalid_argument("time: wrong arguments");
  const auto full = readBytes(argv[2]);
  const auto extract = readBytes(argv[3]);
  const auto preview = readBytes(argv[4]);
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> clips;
  for (int i = 5; i < argc; ++i)
    clips.emplace_back(std::filesystem::path(argv[i]).filename().string(), readBytes(argv[i]));
  std::printf("bytes: full %zu, extract %zu\n", full.size(), extract.size());

  // Loading: constructing an Instrument parses and checks the whole file once.
  for (const auto& [name, bytes, runs] :
       {std::tuple{"full", &full, 5}, std::tuple{"extract", &extract, 21}}) {
    std::vector<double> times;
    for (int i = 0; i < runs; ++i) {
      const auto t = Clock::now();
      web::Instrument instrument(*bytes);
      times.push_back(msSince(t));
    }
    const auto s = spread(times);
    std::printf("load %-7s median %8.2f ms (min %.2f, max %.2f, %d runs)\n", name, s.median,
                s.least, s.most, runs);
  }

  for (const double rate : {44100.0, 48000.0}) {
    web::Instrument instrument(extract);
    // The note preview: the first render on a freshly loaded instrument, then later ones.
    auto t = Clock::now();
    auto first = instrument.render(preview, rate);
    const double firstMs = msSince(t);
    std::vector<double> times;
    for (int i = 0; i < 51; ++i) {
      t = Clock::now();
      auto r = instrument.render(preview, rate);
      times.push_back(msSince(t));
      keepAlive = keepAlive + r.left.size();
    }
    const auto s = spread(times);
    std::printf("%.1f kHz preview: %zu frames (%.3f s); first %.3f ms, then median %.3f ms "
                "(min %.3f, max %.3f, 51 runs)\n",
                rate / 1000, first.left.size(), first.left.size() / rate, firstMs, s.median,
                s.least, s.most);

    for (const auto& [name, bytes] : clips) {
      times.clear();
      std::size_t frames = 0;
      for (int i = 0; i < 5; ++i) {
        t = Clock::now();
        auto r = instrument.render(bytes, rate);
        times.push_back(msSince(t));
        frames = r.left.size();
      }
      const auto c = spread(times);
      std::printf("%.1f kHz %s: %zu frames (%.1f s); median %.1f ms (min %.1f, max %.1f, "
                  "5 runs), %.2f ms per second of audio\n",
                  rate / 1000, name.c_str(), frames, frames / rate, c.median, c.least, c.most,
                  c.median / (frames / rate));
    }
  }
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::strcmp(argv[1], "render") == 0) return render(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "ignored") == 0) return ignored(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "time") == 0) return timings(argc, argv);
    std::fprintf(stderr, "usage: see the comment at the top of PROTOTYPE_render.cpp\n");
    return 2;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
}
