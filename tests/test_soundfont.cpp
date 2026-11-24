#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
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

// Renders a fresh graph: MIDI at exact frames into a soundfont node holding
// `font`, into a two-channel capture. `setup` authors the node before the
// Engine prepares it.
Stereo play(const Bytes& font, const std::vector<Timed>& midi, Run run = {},
            const std::function<void(SoundFontNode&)>& setup = {}) {
  Graph graph;
  const auto sourceId = graph.addNode<MidiAt>(midi);
  const auto synthId = graph.addNode<SoundFontNode>(SoundFont::parse(font));
  const auto sinkId = graph.addNode<CaptureSinkNode>(2);
  auto& source = static_cast<MidiAt&>(graph.node(sourceId));
  auto& synth = static_cast<SoundFontNode&>(graph.node(synthId));
  auto& sink = static_cast<CaptureSinkNode&>(graph.node(sinkId));
  graph.connect(sourceId, source.out, synthId, synth.midiIn());
  graph.connect(synthId, synth.out(), sinkId, sink.in());
  if (setup) setup(synth);
  Engine engine(graph, run.rate, run.block);
  while (engine.streamTime() < run.frames)
    engine.process(static_cast<std::uint32_t>(
        std::min<std::uint64_t>(run.block, run.frames - engine.streamTime())));
  Stereo result;
  for (std::size_t i = 0; i + 1 < sink.samples().size(); i += 2) {
    result.left.push_back(sink.samples()[i]);
    result.right.push_back(sink.samples()[i + 1]);
  }
  return result;
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
