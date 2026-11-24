#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
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

struct Timed {
  std::uint64_t frame;
  MidiMsg message;
};

MidiMsg noteOn(int channel, int key, int velocity) {
  return {static_cast<std::uint8_t>(0x90 | channel), static_cast<std::uint8_t>(key),
          static_cast<std::uint8_t>(velocity)};
}

MidiMsg noteOff(int channel, int key) {
  return {static_cast<std::uint8_t>(0x80 | channel), static_cast<std::uint8_t>(key), 0};
}

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

struct Stereo {
  std::vector<float> left, right;
};

struct Run {
  double rate = 48000;
  std::uint32_t block = 128;
  std::uint64_t frames = 4800;
};

// A soundfont node, empty or holding `font`, between MIDI at exact frames and
// a two-channel capture. Each run is a fresh Engine on the same graph.
class Rig {
public:
  explicit Rig(std::vector<Timed> midi, const std::optional<Bytes>& font = std::nullopt) {
    const auto source = graph_.addNode<MidiAt>(std::move(midi));
    synth_ = font ? graph_.addNode<SoundFontNode>(SoundFont::parse(*font))
                  : graph_.addNode<SoundFontNode>();
    sink_ = graph_.addNode<CaptureSinkNode>(2);
    graph_.connect(source, static_cast<MidiAt&>(graph_.node(source)).out, synth_,
                   node().midiIn());
    graph_.connect(synth_, node().out(), sink_, capture().in());
  }

  SoundFontNode& node() { return static_cast<SoundFontNode&>(graph_.node(synth_)); }

  Stereo run(Run run = {}) {
    const auto from = capture().samples().size();
    Engine engine(graph_, run.rate, run.block);
    while (engine.streamTime() < run.frames)
      engine.process(static_cast<std::uint32_t>(
          std::min<std::uint64_t>(run.block, run.frames - engine.streamTime())));
    Stereo result;
    const auto& samples = capture().samples();
    for (std::size_t i = from; i + 1 < samples.size(); i += 2) {
      result.left.push_back(samples[i]);
      result.right.push_back(samples[i + 1]);
    }
    return result;
  }

private:
  CaptureSinkNode& capture() { return static_cast<CaptureSinkNode&>(graph_.node(sink_)); }

  Graph graph_;
  NodeId synth_ = kNoNode, sink_ = kNoNode;
};

// Renders a fresh graph holding `font`. `setup` authors the node before the
// Engine prepares it.
Stereo play(const Bytes& font, const std::vector<Timed>& midi, Run run = {},
            const std::function<void(SoundFontNode&)>& setup = {}) {
  Rig rig(midi, font);
  if (setup) setup(rig.node());
  return rig.run(run);
}

std::vector<std::int16_t> steady(std::int16_t value, std::size_t points) {
  return std::vector<std::int16_t>(points, value);
}

// One preset, one instrument, one zone playing one sample.
File oneZone(std::vector<std::int16_t> points, std::vector<Generator> generators = {},
             std::uint32_t rate = 48000) {
  File file;
  file.samples.push_back({"sample", std::move(points), rate});
  generators.push_back(set(gen::kSampleId, 0));
  file.instruments.push_back({"instrument", {{generators}}});
  file.presets.push_back({"preset", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  return file;
}

std::vector<std::int16_t> sine(double cyclesPerPoint, std::size_t points) {
  std::vector<std::int16_t> result(points);
  for (std::size_t i = 0; i < points; ++i)
    result[i] = static_cast<std::int16_t>(std::lround(16000 * std::sin(2 * 3.14159265358979 *
                                                                       cyclesPerPoint * i)));
  return result;
}

// A steady tone's frequency, from its first and last rising zero crossings in
// [from, to), each placed between frames by linear interpolation.
double frequency(const std::vector<float>& s, std::size_t from, std::size_t to, double rate) {
  double first = -1, last = -1;
  int crossings = 0;
  for (std::size_t i = from; i + 1 < to; ++i)
    if (s[i] <= 0 && s[i + 1] > 0) {
      const double at = i + s[i] / (s[i] - s[i + 1]);
      if (crossings++ == 0) first = at;
      last = at;
    }
  REQUIRE(crossings > 10);
  return (crossings - 1) * rate / (last - first);
}

double cents(double measured, double expected) { return 1200 * std::log2(measured / expected); }

std::size_t firstSounding(const std::vector<float>& samples, std::size_t from = 0) {
  for (std::size_t i = from; i < samples.size(); ++i)
    if (samples[i] != 0) return i;
  return samples.size();
}

std::size_t lastSounding(const std::vector<float>& samples) {
  for (std::size_t i = samples.size(); i > 0; --i)
    if (samples[i - 1] != 0) return i - 1;
  return samples.size();
}

} // namespace

TEST_CASE("a loaded soundfont plays a note in both channels until its sample ends") {
  // A centred mono zone whose sample is recorded at the graph's rate and rooted
  // at the key played, so it plays point for point.
  const auto out = play(build(oneZone(steady(4000, 600))), {{11, noteOn(0, 60, 100)}});
  CHECK(out.left == out.right);
  // The default delay is -12000 timecents, 2^-10 s: 46.875 frames at 48 kHz.
  const auto onset = firstSounding(out.left);
  REQUIRE(onset + 600 < out.left.size());
  CHECK(std::abs(static_cast<double>(onset) - (11 + 46.875)) <= 1);
  // The 600 points play from the onset, then the voice ends: exact zeros.
  CHECK(lastSounding(out.left) == onset + 599);
  // After the default attack and hold (1 ms each), a centred zone is 3 dB
  // down: 4000 / 32768 times 1/sqrt(2).
  CHECK(out.left[onset + 300] == doctest::Approx(0.0863167).epsilon(1e-5));
}

TEST_CASE("a note sounds at its pitch from the root key and the sample's rate at two graph rates") {
  // A 1 kHz sine recorded at 32 kHz, rooted at key 64 by overridingRootKey
  // although its header says 60.
  auto file = oneZone(sine(1000.0 / 32000, 32000), {set(gen::kOverridingRootKey, 64)}, 32000);
  const auto font = build(file);
  for (const double rate : {44100.0, 48000.0}) {
    // Five semitones down and seven up: 1000 * 2^(-5/12) and 1000 * 2^(7/12) Hz.
    for (const auto& [key, hertz] : {std::pair{59, 749.1535}, std::pair{71, 1498.3071}}) {
      CAPTURE(rate);
      CAPTURE(key);
      const auto frames = static_cast<std::uint64_t>(rate / 2);
      const auto out = play(font, {{3, noteOn(0, key, 100)}}, {rate, 128, frames});
      const auto from = static_cast<std::size_t>(rate / 50);
      CHECK(std::abs(cents(frequency(out.left, from, out.left.size(), rate), hertz)) < 1);
    }
  }
}

TEST_CASE("without overridingRootKey the header's pitch is the root, and 255 or an illegal one means 60") {
  for (const auto& [headerPitch, key] : {std::pair{69, 69}, std::pair{255, 60},
                                         std::pair{128, 60}}) {
    CAPTURE(headerPitch);
    auto file = oneZone(sine(1000.0 / 48000, 24000));
    file.samples[0].originalPitch = static_cast<std::uint8_t>(headerPitch);
    const auto out = play(build(file), {{3, noteOn(0, key, 100)}}, {48000, 128, 12000});
    CHECK(std::abs(cents(frequency(out.left, 960, out.left.size(), 48000), 1000)) < 1);
  }
}

TEST_CASE("a sample played between its points is interpolated") {
  // A ramp an octave down plays every point and every midpoint between them.
  std::vector<std::int16_t> ramp(400);
  for (std::size_t i = 0; i < ramp.size(); ++i)
    ramp[i] = static_cast<std::int16_t>(64 * i + 64);
  auto file = oneZone(ramp, {set(gen::kPan, -500), set(gen::kOverridingRootKey, 72)});
  const auto out = play(build(file), {{0, noteOn(0, 60, 100)}}, {48000, 128, 1200});
  const auto onset = firstSounding(out.left);
  // Past the attack and hold, frame k of the voice reads point k / 2.
  for (std::size_t k = 200; k < 600; ++k) {
    CAPTURE(k);
    CHECK(out.left[onset + k] == doctest::Approx((32.0 * k + 64) / 32768).epsilon(1e-6));
  }
}

namespace {

// A full-left zone playing a steady half-scale sample at the graph's rate, so
// its left channel divided by 0.5 is the volume envelope's level.
Bytes envelopeFont(std::vector<Generator> generators, std::size_t points = 60000) {
  generators.push_back(set(gen::kPan, -500));
  return build(oneZone(steady(16384, points), generators));
}

double level(const Stereo& out, std::size_t frame) { return out.left.at(frame) / 0.5; }

double db(double level) { return 20 * std::log10(level); }

std::size_t lastAtFullLevel(const Stereo& out) {
  std::size_t last = 0;
  for (std::size_t i = 0; i < out.left.size(); ++i)
    if (level(out, i) == 1) last = i;
  return last;
}

} // namespace

TEST_CASE("the volume envelope's delay attack hold decay and sustain take their derived times") {
  // Delay -4800 timecents is 62.5 ms, attack -6000 is 31.25 ms and hold -7200
  // is 15.625 ms: 3000, 1500 and 750 frames at 48 kHz. Decay -1200 falls
  // 100 dB in 0.5 s, so reaching a 200 cB (20 dB) sustain takes 0.1 s.
  const auto font = envelopeFont({set(gen::kDelayVolEnv, -4800), set(gen::kAttackVolEnv, -6000),
                                  set(gen::kHoldVolEnv, -7200), set(gen::kDecayVolEnv, -1200),
                                  set(gen::kSustainVolEnv, 200)});
  const auto out = play(font, {{7, noteOn(0, 60, 100)}}, {48000, 128, 16000});
  const auto attack = firstSounding(out.left);
  CHECK(std::abs(static_cast<double>(attack) - (7 + 3000)) <= 1);
  // The attack rises linearly in amplitude.
  CHECK(level(out, attack + 750) == doctest::Approx(0.5).epsilon(0.002));
  std::size_t peak = attack;
  while (level(out, peak) < 1) ++peak;
  CHECK(std::abs(static_cast<double>(peak) - (7 + 3000 + 1500)) <= 1);
  // Hold is flat; the decay starts after it.
  const auto decay = lastAtFullLevel(out);
  CHECK(std::abs(static_cast<double>(decay) - (7 + 3000 + 1500 + 750)) <= 1);
  // 0.05 s into the decay, the level is 10 dB down.
  CHECK(db(level(out, decay + 2400)) == doctest::Approx(-10).epsilon(0.005));
  std::size_t sustain = decay;
  while (level(out, sustain) > 0.1 + 1e-6) ++sustain;
  CHECK(std::abs(static_cast<double>(sustain) - (decay + 4800)) <= 1);
  for (std::size_t i = sustain; i < out.left.size(); ++i)
    CHECK(level(out, i) == doctest::Approx(0.1).epsilon(1e-5));
}

TEST_CASE("a note-off releases from the current level so a release below full level ends sooner") {
  // Release -1200 timecents falls 100 dB in 0.5 s. From full level the voice
  // ends 96 dB down, 0.48 s after its note-off; from a 20 dB sustain, 0.38 s.
  const std::vector<Generator> shape{set(gen::kDecayVolEnv, -1200),
                                     set(gen::kReleaseVolEnv, -1200)};
  for (const auto& [sustainCb, seconds] : {std::pair{0, 0.48}, std::pair{200, 0.38}}) {
    CAPTURE(sustainCb);
    auto generators = shape;
    generators.push_back(set(gen::kSustainVolEnv, sustainCb));
    const auto out = play(envelopeFont(generators),
                          {{5, noteOn(0, 60, 100)}, {12007, noteOff(0, 60)}},
                          {48000, 128, 48000});
    const auto sustainLevel = sustainCb == 0 ? 1.0 : 0.1;
    CHECK(level(out, 12006) == doctest::Approx(sustainLevel).epsilon(1e-5));
    // 0.1 s into the release, the level is 20 dB further down.
    CHECK(db(level(out, 12007 + 4800)) ==
          doctest::Approx(db(sustainLevel) - 20).epsilon(0.005));
    CHECK(std::abs(static_cast<double>(lastSounding(out.left) + 1) -
                   (12007 + seconds * 48000)) <= 1);
  }
}

TEST_CASE("a held voice ends on the frame its decay has fallen 96 dB below its peak") {
  // A 1000 cB (100 dB) sustain lies below the floor, as on the default piano.
  // Decay -2400 timecents falls 100 dB in 0.25 s: 96 dB takes 0.24 s.
  const auto out = play(envelopeFont({set(gen::kDecayVolEnv, -2400),
                                      set(gen::kSustainVolEnv, 1000)}),
                        {{5, noteOn(0, 60, 100)}}, {48000, 128, 14000});
  const auto decay = lastAtFullLevel(out);
  CHECK(lastSounding(out.left) + 1 == decay + 11520);
  CHECK(db(level(out, decay + 11519)) > -96);
}

TEST_CASE("equal-power pan puts a centred zone 3 dB down on each side and +-500 on one side") {
  for (const int pan : {-500, 0, 500}) {
    CAPTURE(pan);
    const auto out = play(build(oneZone(steady(16384, 2000), {set(gen::kPan, pan)})),
                          {{0, noteOn(0, 60, 100)}}, {48000, 128, 1000});
    const double left = out.left[500] / 0.5, right = out.right[500] / 0.5;
    if (pan == 0) {
      CHECK(db(left) == doctest::Approx(-3.0103).epsilon(1e-4));
      CHECK(right == left);
    } else {
      CHECK((pan < 0 ? left : right) == 1);
      CHECK((pan < 0 ? right : left) == 0);
    }
  }
}

TEST_CASE("initialAttenuation lowers a voice by its centibels") {
  // 60 cB is 6 dB.
  const auto out = play(envelopeFont({set(gen::kInitialAttenuation, 60)}),
                        {{0, noteOn(0, 60, 100)}}, {48000, 128, 1000});
  CHECK(db(level(out, 500)) == doctest::Approx(-6).epsilon(1e-5));
}

TEST_CASE("a stereo pair plays as two voices and keeps its image") {
  File file;
  file.samples = {{"left", steady(8000, 2000)}, {"right", steady(-4000, 2000)}};
  file.instruments.push_back({"pair", {{{set(gen::kPan, -500), set(gen::kSampleId, 0)}},
                                       {{set(gen::kPan, 500), set(gen::kSampleId, 1)}}}});
  file.presets.push_back({"preset", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  const auto out = play(build(file), {{0, noteOn(0, 60, 100)}}, {48000, 128, 1000});
  for (std::size_t i = 300; i < 1000; ++i) {
    CHECK(out.left[i] == doctest::Approx(8000.0 / 32768));
    CHECK(out.right[i] == doctest::Approx(-4000.0 / 32768));
  }
}

namespace {

// The left level once a note's envelope is flat, in sample-data units.
double plays(const Bytes& font, int key, int velocity) {
  const auto out = play(font, {{0, noteOn(0, key, velocity)}}, {48000, 128, 400});
  return out.left[300] * 32768.0;
}

} // namespace

TEST_CASE("a note plays every instrument zone whose ranges at both levels contain it") {
  // Samples are told apart by level, all full left. Instrument 0's global zone
  // pans its zones; its zones split by key and then by velocity. The preset's
  // global zone gives its first zone keys 0-100; its second zone layers
  // instrument 1 from key 70.
  File file;
  // Long enough to outlast frame 300 at key 110, 50 semitones up.
  file.samples = {{"a", steady(1000, 8000)}, {"b", steady(2000, 8000)},
                  {"c", steady(4000, 8000)}, {"d", steady(8000, 8000)}};
  file.instruments.push_back(
      {"split", {{{set(gen::kPan, -500)}},
                 {{range(gen::kKeyRange, 0, 59), set(gen::kSampleId, 0)}},
                 {{range(gen::kKeyRange, 60, 127), range(gen::kVelRange, 0, 63),
                   set(gen::kSampleId, 1)}},
                 {{range(gen::kVelRange, 64, 127), range(gen::kKeyRange, 60, 127),
                   set(gen::kSampleId, 2)}}}});
  file.instruments.push_back({"layer", {{{set(gen::kPan, -500), set(gen::kSampleId, 3)}}}});
  file.presets.push_back({"preset", 0, 0,
                          {{{range(gen::kKeyRange, 0, 100)}},
                           {{set(gen::kInstrument, 0)}},
                           {{range(gen::kKeyRange, 70, 127), set(gen::kInstrument, 1)}}}});
  const auto font = build(file);
  CHECK(plays(font, 40, 100) == doctest::Approx(1000));
  CHECK(plays(font, 65, 30) == doctest::Approx(2000));
  CHECK(plays(font, 65, 100) == doctest::Approx(4000));
  CHECK(plays(font, 80, 100) == doctest::Approx(4000 + 8000));
  CHECK(plays(font, 110, 100) == doctest::Approx(8000));
}

TEST_CASE("preset-level values add to instrument-level ones and instrument-only ones are ignored") {
  // 100 cB in the instrument and 140 cB from the preset's global zone make
  // 240 cB, 24 dB. The preset's overridingRootKey is instrument-only, so the
  // 600-point sample still plays point for point.
  File file = oneZone(steady(16384, 600), {set(gen::kPan, -500),
                                           set(gen::kInitialAttenuation, 100)});
  file.presets[0].zones.insert(file.presets[0].zones.begin(),
                               {{set(gen::kInitialAttenuation, 140),
                                 set(gen::kOverridingRootKey, 72)}});
  const auto out = play(build(file), {{0, noteOn(0, 60, 100)}}, {48000, 128, 2000});
  CHECK(db(level(out, 400)) == doctest::Approx(-24).epsilon(1e-5));
  CHECK(lastSounding(out.left) - firstSounding(out.left) == 599);
}

TEST_CASE("a preset-level release per velocity zone adds to the instrument's release") {
  // The instrument's release is -1200 timecents, 100 dB in 0.5 s. The soft
  // zone adds -1200 more: 100 dB in 0.25 s. From full level the voice ends
  // 96 dB down: 0.24 s or 0.48 s after the note-off.
  File file = oneZone(steady(16384, 60000), {set(gen::kPan, -500),
                                             set(gen::kReleaseVolEnv, -1200)});
  file.presets[0].zones = {
      {{range(gen::kVelRange, 0, 63), set(gen::kReleaseVolEnv, -1200),
        set(gen::kInstrument, 0)}},
      {{range(gen::kVelRange, 64, 127), set(gen::kInstrument, 0)}}};
  const auto font = build(file);
  for (const auto& [velocity, seconds] : {std::pair{40, 0.24}, std::pair{100, 0.48}}) {
    CAPTURE(velocity);
    const auto out = play(font, {{0, noteOn(0, 60, velocity)}, {1009, noteOff(0, 60)}},
                          {48000, 128, 30000});
    CHECK(std::abs(static_cast<double>(lastSounding(out.left) + 1) -
                   (1009 + seconds * 48000)) <= 1);
  }
}

TEST_CASE("generator values outside the specification's ranges are clamped once added") {
  // Pan 400 plus a preset's 300 is clamped to 500: fully right.
  File file = oneZone(steady(16384, 2000), {set(gen::kPan, 400)});
  file.presets[0].zones[0].generators.insert(file.presets[0].zones[0].generators.begin(),
                                             set(gen::kPan, 300));
  auto out = play(build(file), {{0, noteOn(0, 60, 100)}}, {48000, 128, 1000});
  CHECK(out.left[500] == 0);
  CHECK(out.right[500] == 0.5f);
  // FluidR3's attack of -32768 timecents is clamped to -12000: 46.875 frames.
  out = play(envelopeFont({set(gen::kAttackVolEnv, -32768)}), {{0, noteOn(0, 60, 100)}},
             {48000, 128, 1000});
  const auto onset = firstSounding(out.left);
  std::size_t peak = onset;
  while (level(out, peak) < 1) ++peak;
  CHECK(std::abs(static_cast<double>(peak - onset + 1) - 46.875) <= 1);
}

namespace {

// Three single-zone presets, listed out of order and told apart by level:
// bank 1 program 0, bank 0 program 5 and bank 0 program 3.
Bytes threePresets() {
  File file;
  file.samples = {{"a", steady(1000, 2000)}, {"b", steady(2000, 2000)},
                  {"c", steady(4000, 2000)}};
  for (int i = 0; i < 3; ++i)
    file.instruments.push_back({"i", {{{set(gen::kPan, -500), set(gen::kSampleId, i)}}}});
  file.presets = {{"bank 1", 0, 1, {{{set(gen::kInstrument, 2)}}}},
                  {"program 5", 5, 0, {{{set(gen::kInstrument, 1)}}}},
                  {"program 3", 3, 0, {{{set(gen::kInstrument, 0)}}}}};
  return build(file);
}

const std::vector<Timed> kOneNote{{0, noteOn(0, 60, 100)}};

double steadyLevel(const Stereo& out) { return out.left.at(300) * 32768.0; }

// A file written for one test and removed with it.
struct TemporaryFile {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
      ("modular-soundfont-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".sf2");
  explicit TemporaryFile(const Bytes& bytes) {
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }
  ~TemporaryFile() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
};

} // namespace

TEST_CASE("an empty soundfont node is not ready and prepare asks for a soundfont") {
  Rig rig(kOneNote);
  CHECK_FALSE(rig.node().ready());
  CHECK_THROWS_WITH_AS(rig.run(), "soundfont: load a soundfont first", std::invalid_argument);
}

TEST_CASE("a load selects the lowest bank then its lowest program and keeps the voice limit") {
  Rig rig(kOneNote);
  rig.node().setVoiceLimit(7);
  rig.node().load(threePresets());
  CHECK(rig.node().ready());
  CHECK(rig.node().bank() == 0);
  CHECK(rig.node().program() == 3);
  CHECK(rig.node().voiceLimit() == 7);
  CHECK(steadyLevel(rig.run()) == doctest::Approx(1000));
}

TEST_CASE("a failed load throws and keeps the previous soundfont and preset") {
  Rig rig(kOneNote);
  rig.node().load(threePresets());
  rig.node().setPreset(0, 5);
  auto truncated = threePresets();
  truncated.pop_back();
  CHECK_THROWS_AS(rig.node().load(truncated), std::invalid_argument);
  CHECK_THROWS_AS(rig.node().load(std::filesystem::path("missing.sf2")), std::invalid_argument);
  CHECK(rig.node().program() == 5);
  CHECK(steadyLevel(rig.run()) == doctest::Approx(2000));
}

TEST_CASE("the preset setting chooses another preset and throws for one the file lacks") {
  Rig rig(kOneNote, threePresets());
  rig.node().setPreset(1, 0);
  CHECK_THROWS_WITH_AS(rig.node().setPreset(0, 4), "soundfont: no preset at bank 0 program 4",
                       std::invalid_argument);
  CHECK(rig.node().bank() == 1);
  CHECK(rig.node().program() == 0);
  CHECK(steadyLevel(rig.run()) == doctest::Approx(4000));
  Rig empty(kOneNote);
  CHECK_THROWS_AS(empty.node().setPreset(0, 0), std::invalid_argument);
}

TEST_CASE("the voice limit accepts 1 to 65535 and defaults to 256") {
  Rig rig(kOneNote, threePresets());
  CHECK(rig.node().voiceLimit() == 256);
  for (const std::uint32_t limit : {0u, 65536u, 100000u}) {
    CAPTURE(limit);
    CHECK_THROWS_AS(rig.node().setVoiceLimit(limit), std::invalid_argument);
    CHECK(rig.node().voiceLimit() == 256);
  }
  for (const std::uint32_t limit : {1u, 65535u}) {
    rig.node().setVoiceLimit(limit);
    CHECK(rig.node().voiceLimit() == limit);
    CHECK(steadyLevel(rig.run()) == doctest::Approx(1000));
  }
}

TEST_CASE("prepare throws when the voice limit is below the voices one note of the preset starts") {
  // Program 0 plays a stereo pair everywhere and layers a third zone on keys
  // 60 to 70, so a note can start three voices. Program 1 starts one.
  File file;
  file.samples = {{"s", steady(1000, 2000)}};
  file.instruments.push_back({"pair", {{{set(gen::kPan, -500), set(gen::kSampleId, 0)}},
                                       {{set(gen::kPan, 500), set(gen::kSampleId, 0)}},
                                       {{range(gen::kKeyRange, 60, 70),
                                         set(gen::kSampleId, 0)}}}});
  file.instruments.push_back({"one", {{{set(gen::kSampleId, 0)}}}});
  file.presets = {{"three", 0, 0, {{{set(gen::kInstrument, 0)}}}},
                  {"one", 1, 0, {{{set(gen::kInstrument, 1)}}}}};
  Rig rig(kOneNote, build(file));
  rig.node().setVoiceLimit(2);
  CHECK_THROWS_WITH_AS(rig.run(), "soundfont: voice limit is below the voices one note starts",
                       std::invalid_argument);
  rig.node().setVoiceLimit(3);
  CHECK_NOTHROW(rig.run());
  rig.node().setVoiceLimit(1);
  rig.node().setPreset(0, 1);
  CHECK_NOTHROW(rig.run());
}

TEST_CASE("a node loaded from a path renders after the file is deleted") {
  Rig rig(kOneNote);
  {
    TemporaryFile file(threePresets());
    rig.node().load(file.path);
  }
  CHECK(steadyLevel(rig.run()) == doctest::Approx(1000));
}

TEST_CASE("a second Engine on the same graph renders the same samples as the first") {
  // The first run ends with the note still sounding; the second starts clean.
  Rig rig({{5, noteOn(0, 60, 100)}, {701, noteOn(0, 64, 90)}, {1301, noteOff(0, 60)}},
          envelopeFont({set(gen::kReleaseVolEnv, -1200)}));
  const auto first = rig.run({48000, 127, 2000});
  const auto second = rig.run({48000, 127, 2000});
  CHECK(lastSounding(first.left) == 1999);
  CHECK(first.left == second.left);
  CHECK(first.right == second.right);
}

namespace {

// Two velocity zones, full left, told apart by level: soft notes play 1000
// and loud ones 2000. Each releases 100 dB in 0.5 s.
Bytes twoVelocities() {
  File file;
  file.samples = {{"soft", steady(1000, 60000)}, {"loud", steady(2000, 60000)}};
  file.instruments.push_back({"i", {{{set(gen::kPan, -500), set(gen::kReleaseVolEnv, -1200)}},
                                    {{range(gen::kVelRange, 0, 63), set(gen::kSampleId, 0)}},
                                    {{range(gen::kVelRange, 64, 127), set(gen::kSampleId, 1)}}}});
  file.presets.push_back({"p", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  return build(file);
}

double levelAt(const Stereo& out, std::size_t frame) { return out.left.at(frame) * 32768.0; }

} // namespace

TEST_CASE("a note-on with velocity 0 is a note-off and note-off velocity is ignored") {
  const auto font = twoVelocities();
  const auto withZero = play(font, {{0, noteOn(0, 60, 100)}, {2003, MidiMsg{0x90, 60, 0}}},
                             {48000, 128, 30000});
  const auto withOff = play(font, {{0, noteOn(0, 60, 100)}, {2003, MidiMsg{0x80, 60, 64}}},
                            {48000, 128, 30000});
  CHECK(withZero.left == withOff.left);
  // From full level the release ends 96 dB down, 0.48 s later.
  CHECK(std::abs(static_cast<double>(lastSounding(withZero.left) + 1) - (2003 + 23040)) <= 1);
}

TEST_CASE("every channel plays the preset and other channel messages change nothing") {
  const auto font = twoVelocities();
  const auto reference = play(font, {{0, noteOn(0, 60, 100)}, {1500, noteOff(0, 60)}},
                              {48000, 128, 30000});
  // Channel 10 (status nibble 9), with a program change, volume, the sustain
  // pedal, pitch bend and pressure among its notes.
  const auto drums = play(font, {{0, MidiMsg{0xc9, 40, 0}},
                                 {0, noteOn(9, 60, 100)},
                                 {300, MidiMsg{0xb9, 7, 20}},
                                 {500, MidiMsg{0xe9, 0, 127}},
                                 {700, MidiMsg{0xc9, 1, 0}},
                                 {900, MidiMsg{0xd9, 100, 0}},
                                 {1000, MidiMsg{0xb9, 64, 127}},
                                 {1200, MidiMsg{0xa9, 60, 90}},
                                 {1500, noteOff(9, 60)}},
                          {48000, 128, 30000});
  CHECK(levelAt(reference, 1000) == doctest::Approx(2000));
  CHECK(drums.left == reference.left);
  CHECK(drums.right == reference.right);
}

TEST_CASE("a key struck again sounds beside its earlier note and note-offs end the oldest first") {
  const auto out = play(twoVelocities(),
                        {{0, noteOn(0, 60, 100)}, {1000, noteOn(0, 60, 30)},
                         {2000, noteOff(0, 60)}, {30000, noteOff(0, 60)}},
                        {48000, 128, 54000});
  CHECK(levelAt(out, 1500) == doctest::Approx(3000));
  // The loud note, struck first, is released first: 0.1 s later it is 20 dB
  // down, while the soft one still holds.
  CHECK(levelAt(out, 2000 + 4800) == doctest::Approx(1000 + 200).epsilon(1e-4));
  CHECK(levelAt(out, 29000) == doctest::Approx(1000));
  CHECK(std::abs(static_cast<double>(lastSounding(out.left) + 1) - (30000 + 23040)) <= 1);
}

TEST_CASE("a note-off with no held note on its key does nothing") {
  const auto font = twoVelocities();
  const auto reference = play(font, {{0, noteOn(0, 60, 100)}, {1000, noteOff(0, 60)}},
                              {48000, 128, 30000});
  const auto strays = play(font, {{0, noteOn(0, 60, 100)}, {500, noteOff(0, 61)},
                                  {600, noteOff(1, 60)}, {1000, noteOff(0, 60)},
                                  {1500, noteOff(0, 60)}},
                           {48000, 128, 30000});
  CHECK(strays.left == reference.left);
}

TEST_CASE("two parts in unison on two channels both sound and end separately") {
  const auto out = play(twoVelocities(),
                        {{0, noteOn(0, 60, 100)}, {0, noteOn(1, 60, 100)},
                         {1000, noteOff(1, 60)}},
                        {48000, 128, 30000});
  CHECK(levelAt(out, 900) == doctest::Approx(4000));
  CHECK(levelAt(out, 1000 + 4800) == doctest::Approx(2000 + 200).epsilon(1e-4));
  CHECK(levelAt(out, 29000) == doctest::Approx(2000));
}

TEST_CASE("for now a note-on that finds too few free voices is not started") {
  // Interim rule until voices are taken (ticket 03). A stereo pair needs two
  // voices; with three, a second note finds one free and does not start.
  File file;
  file.samples = {{"s", steady(1000, 60000)}};
  file.instruments.push_back({"pair", {{{set(gen::kPan, -500), set(gen::kSampleId, 0)}},
                                       {{set(gen::kPan, 500), set(gen::kSampleId, 0)}}}});
  file.presets.push_back({"p", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  const auto font = build(file);
  const auto limit = [](SoundFontNode& node) { node.setVoiceLimit(3); };
  const auto alone = play(font, {{0, noteOn(0, 60, 100)}}, {48000, 128, 3000}, limit);
  const auto crowded = play(font, {{0, noteOn(0, 60, 100)}, {500, noteOn(0, 62, 100)}},
                            {48000, 128, 3000}, limit);
  CHECK(crowded.left == alone.left);
  CHECK(crowded.right == alone.right);
}

namespace {

// A small file using both levels' global zones, two instruments, a modulator
// and three samples, so a load walks every list.
File richFile() {
  File file;
  file.samples = {{"a", steady(1000, 40)}, {"b", steady(2000, 30)}, {"c", steady(3000, 20)}};
  file.instruments = {
      {"one", {{{set(gen::kPan, -500)}, {{0x0502, 48, 960, 0, 0}}},
               {{range(gen::kKeyRange, 0, 63), set(gen::kSampleId, 0)}},
               {{range(gen::kKeyRange, 64, 127), set(gen::kSampleId, 1)}}}},
      {"two", {{{set(gen::kSampleId, 2)}}}}};
  file.presets = {{"p", 0, 0,
                   {{{set(gen::kInitialAttenuation, 10)}},
                    {{range(gen::kVelRange, 0, 99), set(gen::kInstrument, 0)}},
                    {{range(gen::kVelRange, 100, 127), set(gen::kInstrument, 1)}}}}};
  return file;
}

// The message a load throws, or "" if it loads.
std::string loadError(const Bytes& bytes) {
  Graph graph;
  auto& node = static_cast<SoundFontNode&>(graph.node(graph.addNode<SoundFontNode>()));
  try {
    node.load(bytes);
  } catch (const std::invalid_argument& error) {
    return error.what();
  }
  return "";
}

bool refused(const Bytes& bytes) { return loadError(bytes).rfind("soundfont: ", 0) == 0; }

std::size_t find(const Bytes& bytes, const std::string& tag) {
  return static_cast<std::size_t>(std::search(bytes.begin(), bytes.end(), tag.begin(),
                                              tag.end()) - bytes.begin());
}

Chunk& list(Parts& parts, const std::string& tag) {
  for (auto& chunk : parts.pdta)
    if (chunk.tag == tag) return chunk;
  throw std::logic_error("no such list");
}

} // namespace

TEST_CASE("the rich test file loads") { CHECK(loadError(build(richFile())).empty()); }

TEST_CASE("truncated soundfonts are refused at every byte boundary") {
  const auto bytes = build(richFile());
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    CAPTURE(length);
    CHECK(refused(Bytes(bytes.begin(), bytes.begin() + length)));
  }
}

TEST_CASE("a malformed RIFF structure is refused") {
  auto bytes = build(richFile());
  SUBCASE("not a RIFF file") { bytes[0] = 'X'; }
  SUBCASE("not a soundfont form") { bytes[8] = 'X'; }
  SUBCASE("a chunk that overruns its list") {
    const auto at = find(bytes, "pgen") + 4;
    put32(bytes, at, bytes[at] + 4);
  }
  SUBCASE("data after the RIFF chunk") { bytes.push_back(0); }
  SUBCASE("lists missing") {
    auto parts = sf2test::parts(richFile());
    SUBCASE("no ifil version") { parts.info.erase(parts.info.begin()); }
    SUBCASE("an ifil version of the wrong size") { parts.info[0].data.push_back(0); }
    SUBCASE("no generator list") { parts.pdta.erase(parts.pdta.begin() + 7); }
    SUBCASE("no sample headers") { parts.pdta.pop_back(); }
    bytes = assemble(parts);
  }
  CHECK(refused(bytes));
}

TEST_CASE("a hydra list of the wrong size is refused") {
  auto parts = sf2test::parts(richFile());
  SUBCASE("a partial record") { list(parts, "pgen").data.push_back(0); }
  SUBCASE("sample headers cut short") { list(parts, "shdr").data.resize(46 * 3); }
  SUBCASE("more bags than the presets use") { // a second copy of the terminal bag
    auto& bags = list(parts, "pbag").data;
    const Bytes terminal(bags.end() - 4, bags.end());
    bags.insert(bags.end(), terminal.begin(), terminal.end());
  }
  SUBCASE("more generators than the bags use") {
    list(parts, "igen").data.insert(list(parts, "igen").data.end(), 4, 0);
  }
  SUBCASE("more modulators than the bags use") {
    list(parts, "imod").data.insert(list(parts, "imod").data.end(), 10, 0);
  }
  CHECK(refused(assemble(parts)));
}

TEST_CASE("an index or a sample pointing outside its list or the sample data is refused") {
  auto parts = sf2test::parts(richFile());
  SUBCASE("a preset's bags out of order") { put16(list(parts, "phdr").data, 24, 5); }
  SUBCASE("a bag's generators out of order") { put16(list(parts, "ibag").data, 4, 9); }
  SUBCASE("a bag's modulators out of order") { put16(list(parts, "pbag").data, 2, 1); }
  SUBCASE("an instrument index past the instruments") {
    put16(list(parts, "pgen").data, 4 * 2 + 2, 2);
  }
  SUBCASE("a sample index past the samples") { put16(list(parts, "igen").data, 4 * 2 + 2, 3); }
  SUBCASE("a sample ending past the sample data") {
    put32(list(parts, "shdr").data, 24, 100000);
  }
  SUBCASE("a sample ending before it starts") { put32(list(parts, "shdr").data, 46 + 24, 3); }
  CHECK(refused(assemble(parts)));
}

TEST_CASE("a file with no preset is refused") {
  auto file = richFile();
  file.presets.clear();
  CHECK(loadError(build(file)) == "soundfont: no preset");
}

TEST_CASE("sf3 files and versions other than 2 are refused") {
  auto file = richFile();
  SUBCASE("version 3") {
    file.major = 3;
    CHECK(loadError(build(file)) == "soundfont: compressed samples (sf3) are not supported");
  }
  SUBCASE("a compressed sample") {
    file.samples[2].type = kMonoSample | kCompressedSample;
    CHECK(loadError(build(file)) == "soundfont: compressed samples (sf3) are not supported");
  }
  SUBCASE("version 1") {
    file.major = 1;
    CHECK(refused(build(file)));
  }
  SUBCASE("versions 2.01 and 2.04 load") {
    file.minor = 1;
    CHECK(loadError(build(file)).empty());
  }
}

TEST_CASE("for now a zone whose sample is in ROM, empty or rateless is refused") {
  // Interim rule until ticket 04 skips and lists such zones.
  auto file = richFile();
  SUBCASE("in ROM") { file.samples[1].type = kRomSample; }
  SUBCASE("empty") { file.samples[1].points.clear(); }
  SUBCASE("no sample rate") { file.samples[1].rate = 0; }
  CHECK(refused(build(file)));
}

TEST_CASE("a ROM sample no zone plays does not stop a load") {
  auto file = richFile();
  file.samples.push_back({"rom", {}, 48000, 60, kRomSample});
  auto parts = sf2test::parts(file);
  put32(list(parts, "shdr").data, 46 * 3 + 24, 1u << 30); // ROM addresses, not ours
  CHECK(loadError(assemble(parts)).empty());
}

TEST_CASE("a 24-bit extension is skipped and the file plays at 16 bits") {
  auto file = oneZone(steady(16384, 2000), {set(gen::kPan, -500)});
  const auto plain = play(build(file), kOneNote, {48000, 128, 1000});
  file.sm24 = true;
  const auto extended = play(build(file), kOneNote, {48000, 128, 1000});
  CHECK(extended.left == plain.left);
}

TEST_CASE("generators and modulators the node does not read are ignored silently") {
  // An effects send, as on the default piano, a reserved generator, an
  // unknown one and a modulator, at both levels.
  const auto plain = oneZone(steady(16384, 2000), {set(gen::kPan, -500)});
  auto extra = plain;
  auto& zone = extra.instruments[0].zones[0];
  zone.generators.insert(zone.generators.begin(),
                         {set(gen::kReverbEffectsSend, 100), set(14, 7), set(99, 3)});
  zone.modulators.push_back({0x0502, 48, 960, 0, 0});
  extra.presets[0].zones[0].generators.insert(extra.presets[0].zones[0].generators.begin(),
                                              set(gen::kReverbEffectsSend, 100));
  extra.presets[0].zones[0].modulators.push_back({0x0502, 48, 960, 0, 0});
  const auto expected = play(build(plain), kOneNote, {48000, 128, 1000});
  CHECK(play(build(extra), kOneNote, {48000, 128, 1000}).left == expected.left);
}
