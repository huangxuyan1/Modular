// The native half of the soundfont extract tool's test (scripts/soundfont_extract.py).
//
//   soundfont_extract_helper write <directory>
//     Writes the test's soundfonts, built with the test-side builder, into an
//     existing directory: source.sf2, with several presets and its own
//     copyright and sound engine; plain.sf2, the same with neither; and one
//     file for each refusal (sf3, compressed, sm24, truncated, bad-index).
//
//   soundfont_extract_helper compare <source.sf2> <bank> <program> <extract.sf2>
//     Renders one clip through SoundFontNode at 44.1 and 48 kHz, with the
//     preset selected in the source and with the extract's only preset, and
//     exits 0 only when every pair of renders is bit-identical and sounds.
//
// Neither mode reads a file it did not name; nothing here is a test of the
// node itself, which test_soundfont.cpp covers.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "modular/Engine.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/SoundFontNode.h"
#include "SoundFontTestFiles.h"

using namespace modular;
using namespace modular::nodes;
using namespace sf2test;

namespace {

// --- write -------------------------------------------------------------------

namespace more {
// Generators the shared builder does not name.
inline constexpr std::uint16_t kInitialFilterFc = 8;
inline constexpr std::uint16_t kFineTune = 52;
inline constexpr std::uint16_t kUnknown = 99; // no such generator: kept as it is
} // namespace more

std::vector<std::int16_t> wave(std::size_t points, int step, int seed) {
  std::vector<std::int16_t> result(points);
  int value = seed;
  for (auto& point : result) {
    value = (value * 75 + step) % 65537; // distinct, deterministic content
    point = static_cast<std::int16_t>(value % 24000 - 12000);
  }
  return result;
}

Modulator modulator(std::uint16_t source, std::uint16_t destination, int amount) {
  return {source, destination, static_cast<std::int16_t>(amount), 0, 0};
}

// Samples, by index: 0 "shared" (both presets below), 1 "other-only" (only
// preset 0:0), 2 and 3 a left/right pair linked to each other, 4 a left sample
// linked to "other-only", 5 "unused" (no preset).
// Instruments: 0 plays "other-only"; 1 has a global zone and plays "shared"
// and "linked-away"; 2 plays the pair, then holds a zone with no sample, which
// a reader ignores.
// Presets, in file order: "low" 0:0, the extracted "target" 1:5 with global
// zones at both levels and modulators, "high" 2:7, and a second 1:5.
File source() {
  File file;
  file.samples = {
      {"shared", wave(3001, 7, 11), 32000, 64},
      {"other-only", wave(1499, 3, 5), 44100, 60},
      {"left", wave(2503, 13, 17), 22050, 67},
      {"right", wave(2503, 19, 23), 22050, 67},
      {"linked-away", wave(1801, 29, 31), 48000, 55},
      {"unused", wave(997, 37, 41), 48000, 60},
  };
  file.samples[2].type = 4; // left
  file.samples[3].type = 2; // right
  file.samples[4].type = 4;

  file.instruments = {
      {"other-inst",
       {{{set(gen::kPan, 250), set(gen::kSampleId, 1)}, {modulator(0x0502, 48, 480)}}}},
      {"inst-a",
       {// Global zone: no sampleID.
        {{set(gen::kAttackVolEnv, -7000), set(gen::kReleaseVolEnv, -2400),
          set(gen::kInitialAttenuation, 30)},
         {modulator(0x0102, 8, -1200), modulator(0x00DB, 16, 150)}},
        {{range(gen::kKeyRange, 0, 63), set(gen::kOverridingRootKey, 60),
          set(gen::kSampleModes, 1), set(gen::kSampleId, 0)},
         {modulator(0x0081, 6, 40)}},
        {{range(gen::kKeyRange, 64, 127), set(more::kFineTune, -13),
          set(gen::kDecayVolEnv, -1500), set(gen::kSustainVolEnv, 300),
          set(gen::kSampleId, 4), set(gen::kPan, 77)}, // after the terminal: ignored
         {}}}},
      {"inst-stereo",
       {{{set(gen::kPan, -500), set(more::kUnknown, 1234), set(gen::kSampleId, 2)}, {}},
        {{set(gen::kPan, 500), set(gen::kSampleId, 3)}, {modulator(0x0587, 48, 960)}},
        {{set(gen::kPan, 0)}, {}}}}, // no sampleID and not first: ignored
  };

  file.presets = {
      {"low", 0, 0,
       {{{set(gen::kInstrument, 0)}, {modulator(0x000D, 6, 50)}},
        {{range(gen::kVelRange, 0, 90), set(gen::kInstrument, 1)}, {}}}},
      {"target", 5, 1,
       {// Global zone: no instrument.
        {{set(gen::kInitialAttenuation, 40), set(more::kInitialFilterFc, -300),
          set(gen::kReverbEffectsSend, 100)},
         {modulator(0x0502, 48, 900), modulator(0x020E, 52, 12700)}},
        {{range(gen::kVelRange, 0, 99), set(gen::kReleaseVolEnv, -300),
          set(gen::kInstrument, 1)},
         {modulator(0x058B, 48, 960)}},
        {{range(gen::kKeyRange, 30, 100), set(gen::kCoarseTune, 2), set(gen::kInstrument, 2)},
         {}}}},
      {"high", 7, 2, {{{set(gen::kInstrument, 2)}, {}}}},
      // The same number again: the node plays the first preset of a number.
      {"duplicate", 5, 1, {{{set(gen::kInstrument, 0)}, {}}}},
  };
  return file;
}

// Sample header fields the builder leaves at fixed values: loops inside each
// sample, a pitch correction and the links named above.
void editSampleHeaders(Parts& parts) {
  for (auto& chunk : parts.pdta) {
    if (chunk.tag != "shdr") continue;
    auto& headers = chunk.data;
    const auto field = [&](std::size_t sample, std::size_t offset) {
      return sample * 46 + offset;
    };
    const auto start = [&](std::size_t sample) {
      const auto at = field(sample, 20);
      std::uint32_t value = 0;
      for (int i = 0; i < 4; ++i) value |= std::uint32_t{headers[at + i]} << (8 * i);
      return value;
    };
    for (std::size_t s = 0; s < 6; ++s) {
      put32(headers, field(s, 28), start(s) + 101 + 3 * static_cast<std::uint32_t>(s));
      put32(headers, field(s, 32), start(s) + 803 + 5 * static_cast<std::uint32_t>(s));
    }
    headers[field(0, 41)] = static_cast<std::uint8_t>(-7); // pitch correction
    headers[field(3, 41)] = 9;
    put16(headers, field(2, 42), 3); // left -> right
    put16(headers, field(3, 42), 2); // right -> left
    put16(headers, field(4, 42), 1); // -> "other-only", which an extract leaves out
  }
}

// `info` adds the INFO fields a real bank carries and the builder leaves out.
Bytes sourceBytes(const File& file, bool info = true) {
  auto parts = sf2test::parts(file);
  editSampleHeaders(parts);
  if (info) {
    const auto text = [](const std::string& value) {
      Bytes bytes(value.begin(), value.end());
      bytes.push_back(0);
      return bytes;
    };
    parts.info.push_back({"isng", text("Test Engine")});
    parts.info.push_back({"ICOP", text("A Bank Author 2026")});
  }
  return assemble(parts);
}

void save(const std::filesystem::path& path, const Bytes& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  if (!out) throw std::runtime_error("cannot write " + path.string());
}

int write(const std::filesystem::path& directory) {
  const auto file = source();
  const auto bytes = sourceBytes(file);
  // The source must load, and play the preset the test extracts.
  (void)SoundFont::parse(bytes).zones({1, 5});
  save(directory / "source.sf2", bytes);
  save(directory / "plain.sf2", sourceBytes(file, false));

  auto sf3 = file;
  sf3.major = 3;
  save(directory / "sf3.sf2", sourceBytes(sf3));

  auto compressed = file;
  compressed.samples[5].type |= kCompressedSample; // a sample no preset plays
  save(directory / "compressed.sf2", sourceBytes(compressed));

  auto sm24 = file;
  sm24.sm24 = true;
  save(directory / "sm24.sf2", sourceBytes(sm24));

  save(directory / "truncated.sf2", Bytes(bytes.begin(), bytes.end() - 101));

  auto badIndex = file;
  badIndex.instruments[2].zones[1].generators.back() = set(gen::kSampleId, 6);
  save(directory / "bad-index.sf2", sourceBytes(badIndex));
  return 0;
}

// --- compare -----------------------------------------------------------------

struct Timed {
  std::uint64_t frame;
  MidiMsg message;
};

// MIDI at exact stream frames, in the order listed.
class MidiAt : public Node {
public:
  MidiAt(Graph& graph, std::vector<Timed> events) : Node(graph), events_(std::move(events)) {
    out = addOutput(PortType::event(subtype::Midi), "midi");
  }
  void process(ProcessContext& ctx) override {
    for (const auto& event : events_)
      if (event.frame >= ctx.blockStartFrame() &&
          event.frame < ctx.blockStartFrame() + ctx.numFrames())
        ctx.eventsOut(out).push(Event::make(
            static_cast<std::uint32_t>(event.frame - ctx.blockStartFrame()), event.message));
  }
  PortId out;

private:
  std::vector<Timed> events_;
};

// Every key from 0 to 127, one every 50 ms, held 300 ms, at velocities spread
// over 1 to 127, then 3.6 s for the last releases: every zone of a preset
// that covers a key sounds at some velocity.
std::vector<Timed> clip(double rate) {
  std::vector<Timed> events;
  const auto at = [&](double seconds) { return static_cast<std::uint64_t>(seconds * rate); };
  for (int key = 0; key < 128; ++key) {
    const auto velocity = static_cast<std::uint8_t>(1 + (key * 37) % 127);
    events.push_back({at(0.05 * key) + 3, {0x90, static_cast<std::uint8_t>(key), velocity}});
  }
  for (int key = 0; key < 128; ++key)
    events.push_back({at(0.05 * key + 0.3) + 3, {0x80, static_cast<std::uint8_t>(key), 0}});
  // MidiAt pushes each block's events in list order, which must be time order.
  std::stable_sort(events.begin(), events.end(),
                   [](const Timed& a, const Timed& b) { return a.frame < b.frame; });
  return events;
}

constexpr double kClipSeconds = 10;

struct Render {
  std::vector<float> samples; // interleaved left and right
  std::size_t sounding = 0;   // samples that are not zero
};

// `preset` empty plays the file's lowest preset, as a load selects it.
Render render(const SoundFont& font, const SoundFont::PresetNumber* preset, double rate) {
  Graph graph;
  const auto source = graph.addNode<MidiAt>(clip(rate));
  const auto synth = graph.addNode<SoundFontNode>(font);
  const auto sink = graph.addNode<CaptureSinkNode>(2);
  auto& node = static_cast<SoundFontNode&>(graph.node(synth));
  if (preset) node.setPreset(preset->bank, preset->program);
  node.setVoiceLimit(4096); // so no note of the clip is dropped for want of a voice
  graph.connect(source, static_cast<MidiAt&>(graph.node(source)).out, synth,
                node.midiIn());
  graph.connect(synth, node.out(), sink,
                static_cast<CaptureSinkNode&>(graph.node(sink)).in());

  constexpr std::uint32_t kBlock = 97;
  const auto frames = static_cast<std::uint64_t>(kClipSeconds * rate);
  Engine engine(graph, rate, kBlock);
  while (engine.streamTime() < frames)
    engine.process(static_cast<std::uint32_t>(
        std::min<std::uint64_t>(kBlock, frames - engine.streamTime())));
  if (const auto error = engine.errorMessage(); !error.empty())
    throw std::runtime_error("render: " + error);

  Render result;
  result.samples = static_cast<CaptureSinkNode&>(graph.node(sink)).samples();
  for (const float s : result.samples)
    if (s != 0) ++result.sounding;
  return result;
}

// FNV-1a over each sample's IEEE bits, for the record only.
std::uint64_t hash(const std::vector<float>& samples) {
  std::uint64_t h = 14695981039346656037ull;
  for (const float s : samples) {
    std::uint32_t bits;
    std::memcpy(&bits, &s, sizeof bits);
    for (int i = 0; i < 4; ++i) {
      h ^= (bits >> (8 * i)) & 0xff;
      h *= 1099511628211ull;
    }
  }
  return h;
}

// A bank or program number, 0 to 65535.
std::uint16_t number(const std::string& text) {
  const auto value = std::stoul(text);
  if (value > 0xffff) throw std::out_of_range("a bank or program above 65535");
  return static_cast<std::uint16_t>(value);
}

int compare(const std::filesystem::path& sourcePath, std::uint16_t bank,
            std::uint16_t program, const std::filesystem::path& extractPath) {
  const SoundFont::PresetNumber preset{bank, program};
  const auto source = SoundFont::read(sourcePath);
  const auto extract = SoundFont::read(extractPath);
  if (!(extract.lowestPreset() == preset)) {
    std::cerr << "the extract's lowest preset is not " << bank << ":" << program << "\n";
    return 1;
  }
  bool same = true;
  for (const double rate : {44100.0, 48000.0}) {
    const auto full = render(source, &preset, rate);
    const auto cut = render(extract, nullptr, rate);
    const bool identical =
        full.samples.size() == cut.samples.size() &&
        std::memcmp(full.samples.data(), cut.samples.data(),
                    full.samples.size() * sizeof(float)) == 0;
    std::printf("%.0f Hz: %zu frames, %zu sounding samples, FNV-1a %016llx %s %016llx%s\n",
                rate, full.samples.size() / 2, full.sounding,
                static_cast<unsigned long long>(hash(full.samples)),
                identical ? "==" : "!=", static_cast<unsigned long long>(hash(cut.samples)),
                identical ? ", bit-identical" : ", DIFFERENT");
    if (full.sounding == 0) {
      std::cerr << "the clip is silent at " << rate << " Hz, so nothing was compared\n";
      same = false;
    }
    same = same && identical;
  }
  return same ? 0 : 1;
}

int usage() {
  std::cerr << "usage: soundfont_extract_helper write <directory>\n"
               "       soundfont_extract_helper compare <source.sf2> <bank> <program> "
               "<extract.sf2>\n";
  return 2;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "write") return write(args[1]);
    if (args.size() == 5 && args[0] == "compare")
      return compare(args[1], number(args[2]), number(args[3]), args[4]);
    return usage();
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
