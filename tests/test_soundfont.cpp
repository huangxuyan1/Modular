#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <new>
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

double cents(double measured, double expected) {
  return 1200 * std::log2(measured / expected);
}

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

// The first of the first `count` frames on which two renders differ, or
// `count` if they agree on all of them.
std::size_t firstDifference(const std::vector<float>& a, const std::vector<float>& b,
                            std::size_t count) {
  for (std::size_t i = 0; i < count; ++i)
    if (a.at(i) != b.at(i)) return i;
  return count;
}

// The gain the default modulators give a note: velocity to attenuation, and
// the channel volume held at 100. Each is 40 log10(value / 127) dB, which is
// (value / 127)^2 in amplitude.
double noteGain(int velocity) {
  return velocity / 127.0 * (velocity / 127.0) * (100 / 127.0) * (100 / 127.0);
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
  // down: 4000 / 32768 times 1/sqrt(2), times the default modulators' gain.
  CHECK(out.left[onset + 300] ==
        doctest::Approx(0.0863167 * noteGain(100)).epsilon(1e-5));
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

TEST_CASE("without overridingRootKey the root is the header's pitch, 255 or illegal meaning 60") {
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
  const auto out = play(build(file), {{13, noteOn(0, 60, 100)}}, {48000, 128, 1200});
  const auto onset = firstSounding(out.left);
  // Past the attack and hold, frame k of the voice reads point k / 2, so the
  // output rises by half a point's step every frame. The low-pass delays a
  // ramp but keeps its slope.
  for (std::size_t k = 200; k < 600; ++k) {
    CAPTURE(k);
    CHECK(out.left[onset + k + 1] - out.left[onset + k] ==
          doctest::Approx(32.0 / 32768 * noteGain(100)).epsilon(1e-3));
  }
}

namespace {

// A full-left zone playing a steady half-scale sample at the graph's rate, so
// at velocity 100 its left channel divided by 0.5 and the note's gain is the
// volume envelope's level.
Bytes envelopeFont(std::vector<Generator> generators, std::size_t points = 60000) {
  generators.push_back(set(gen::kPan, -500));
  return build(oneZone(steady(16384, points), generators));
}

double level(const Stereo& out, std::size_t frame) {
  return out.left.at(frame) / (0.5 * noteGain(100));
}

// Full level, to the output's float precision.
constexpr double kFull = 1 - 1e-5;

double db(double level) { return 20 * std::log10(level); }

std::size_t lastAtFullLevel(const Stereo& out) {
  std::size_t last = 0;
  for (std::size_t i = 0; i < out.left.size(); ++i)
    if (level(out, i) > kFull) last = i;
  return last;
}

} // namespace

TEST_CASE("the volume envelope's delay attack hold decay and sustain take their derived times") {
  // Delay -4800 timecents is 62.5 ms, attack -6000 is 31.25 ms and hold -7200
  // is 15.625 ms: 3000, 1500 and 750 frames at 48 kHz. Decay -1200 falls
  // 100 dB in 0.5 s, so reaching a 200 cB (20 dB) sustain takes 0.1 s.
  const auto font = envelopeFont({set(gen::kDelayVolEnv, -4800),
                                  set(gen::kAttackVolEnv, -6000),
                                  set(gen::kHoldVolEnv, -7200), set(gen::kDecayVolEnv, -1200),
                                  set(gen::kSustainVolEnv, 200)});
  const auto out = play(font, {{7, noteOn(0, 60, 100)}}, {48000, 128, 16000});
  const auto attack = firstSounding(out.left);
  CHECK(std::abs(static_cast<double>(attack) - (7 + 3000)) <= 1);
  // The attack rises linearly in amplitude.
  CHECK(level(out, attack + 750) == doctest::Approx(0.5).epsilon(0.002));
  std::size_t peak = attack;
  while (level(out, peak) < kFull) ++peak;
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
                          {{13, noteOn(0, 60, 100)}}, {48000, 128, 1000});
    const double left = level(out, 500), right = out.right[500] / (0.5 * noteGain(100));
    if (pan == 0) {
      CHECK(db(left) == doctest::Approx(-3.0103).epsilon(1e-4));
      CHECK(right == left);
    } else {
      CHECK((pan < 0 ? left : right) == doctest::Approx(1));
      CHECK((pan < 0 ? right : left) == 0);
    }
  }
}

TEST_CASE("initialAttenuation lowers a voice by its centibels") {
  // 60 cB is 6 dB.
  const auto out = play(envelopeFont({set(gen::kInitialAttenuation, 60)}),
                        {{13, noteOn(0, 60, 100)}}, {48000, 128, 1000});
  CHECK(db(level(out, 500)) == doctest::Approx(-6).epsilon(1e-5));
}

TEST_CASE("velocity attenuates a note by 40 log10(v / 127) dB") {
  // The default velocity-to-attenuation modulator: 960 cB on a concave curve.
  const auto font = envelopeFont({});
  const auto heard = [&](int velocity) {
    const auto out = play(font, {{13, noteOn(0, 60, velocity)}}, {48000, 128, 1000});
    return db(out.left.at(500));
  };
  const double loudest = heard(127);
  for (const auto& [velocity, gainDb] :
       {std::pair{105, -3.3}, std::pair{85, -7.0}, std::pair{64, -11.9}}) {
    CAPTURE(velocity);
    CHECK(std::abs(heard(velocity) - loudest - gainDb) <= 0.1);
  }
}

TEST_CASE("controllers are held at power-on values so only CC7's 4.2 dB applies") {
  // At velocity 127 a full-left zone plays its half-scale sample 4.2 dB down:
  // the default CC7-to-attenuation modulator at General MIDI's power-on 100.
  const auto font = envelopeFont({set(gen::kReleaseVolEnv, -1200)});
  const auto reference = play(font, {{13, noteOn(0, 60, 127)}, {1511, noteOff(0, 60)}},
                              {48000, 128, 30000});
  CHECK(std::abs(db(reference.left.at(500) / 0.5) + 4.2) <= 0.1);
  // Volume, expression, pan and modulation wheel moved, the sustain pedal
  // held down through the note-off, pitch bend and pressure: nothing changes.
  const auto moved = play(font, {{3, MidiMsg{0xb0, 7, 0}},
                                 {3, MidiMsg{0xb0, 11, 0}},
                                 {3, MidiMsg{0xb0, 10, 0}},
                                 {3, MidiMsg{0xb0, 1, 127}},
                                 {13, noteOn(0, 60, 127)},
                                 {101, MidiMsg{0xb0, 64, 127}},
                                 {307, MidiMsg{0xe0, 0, 0}},
                                 {509, MidiMsg{0xd0, 127, 0}},
                                 {1511, noteOff(0, 60)}},
                          {48000, 128, 30000});
  CHECK(moved.left == reference.left);
  CHECK(moved.right == reference.right);
}

TEST_CASE("initialAttenuation and the modulators together attenuate by at most 1440 cB") {
  // 1000 cB, velocity 1's 842 cB and CC7's 42 cB would make 188 dB. Clamped,
  // the voice is 144 dB down, never louder than its sample.
  const auto out = play(envelopeFont({set(gen::kInitialAttenuation, 1000)}),
                        {{13, noteOn(0, 60, 1)}}, {48000, 128, 1000});
  CHECK(std::abs(db(out.left.at(500) / 0.5) + 144) <= 0.1);
}

namespace {

// A tone and a steady reference through the same voice settings: the right
// channel plays a sine of `hertz`, the left a steady sample, both at 16000 and
// recorded at the graph's rate. The low-pass passes the steady sample
// unchanged, so the right channel against the left is the filter's gain.
Bytes toneFont(std::vector<Generator> generators, double hertz, double rate,
               std::size_t points) {
  File file;
  const auto sampleRate = static_cast<std::uint32_t>(rate);
  file.samples = {{"steady", steady(16000, points), sampleRate},
                  {"tone", sine(hertz / rate, points), sampleRate}};
  file.instruments.push_back({"pair",
                              {{generators},
                               {{set(gen::kPan, -500), set(gen::kSampleId, 0)}},
                               {{set(gen::kPan, 500), set(gen::kSampleId, 1)}}}});
  file.presets.push_back({"preset", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  return build(file);
}

// A steady tone's amplitude over frames [from, to), from its RMS.
double amplitude(const std::vector<float>& s, std::size_t from, std::size_t to) {
  double sum = 0;
  for (std::size_t i = from; i < to; ++i) sum += double(s.at(i)) * s.at(i);
  return std::sqrt(2 * sum / static_cast<double>(to - from));
}

// The tone's gain over frames [from, to), in dB: its amplitude against the
// steady sample's level in the middle of the window.
double toneGainDb(const Stereo& out, std::size_t from, std::size_t to) {
  return db(amplitude(out.right, from, to) / out.left.at((from + to) / 2));
}

// A Butterworth low-pass's gain at `hertz` with its cutoff at `cutoff`: the
// design for Q 0 cB.
double butterworthDb(double hertz, double cutoff) {
  return -10 * std::log10(1 + std::pow(hertz / cutoff, 4));
}

} // namespace

TEST_CASE("the filter is a two-pole low-pass 3 dB down at its derived cutoff") {
  // At velocity 127 the default velocity-to-cutoff modulator takes 18.75
  // cents off initialFilterFc, so 8340 cents puts the cutoff at 8321.25
  // cents: 1000 Hz. The design is 3 dB down there, 0.26 dB an octave below
  // and 12.3 dB an octave above, as a two-pole filter falls.
  for (const double rate : {44100.0, 48000.0}) {
    for (const double hertz : {1000.0, 500.0, 2000.0}) {
      CAPTURE(rate);
      CAPTURE(hertz);
      const auto font = toneFont({set(gen::kInitialFilterFc, 8340)}, hertz, rate,
                                 static_cast<std::size_t>(rate));
      const auto out = play(font, {{13, noteOn(0, 60, 127)}},
                            {rate, 128, static_cast<std::uint64_t>(rate / 2)});
      const auto window = static_cast<std::size_t>(rate / 10);
      const double gain = toneGainDb(out, 2000, 2000 + window);
      CHECK(std::abs(gain - butterworthDb(hertz, 999.966)) <= 1);
    }
  }
}

TEST_CASE("velocity lowers the cutoff linearly so a softer note is quieter and never brighter") {
  // The default velocity-to-cutoff modulator: -2400 cents times (128 - v) /
  // 128 at every velocity, with no switch at 64. From the default piano's
  // 11623 cents it gives these cutoffs, and a 4 kHz tone passes each as the
  // design says. Each softer note is quieter and its tone darker.
  const auto font = toneFont({set(gen::kInitialFilterFc, 11623)}, 4000, 48000, 48000);
  double lastLevel = 1, lastTone = 1;
  for (const auto& [velocity, cutoff] :
       {std::pair{127, 6661.2}, std::pair{105, 5249.0}, std::pair{85, 4226.7},
        std::pair{64, 3366.9}, std::pair{63, 3330.6}, std::pair{48, 2831.2},
        std::pair{40, 2596.2}, std::pair{1, 1701.8}}) {
    CAPTURE(velocity);
    const auto out = play(font, {{13, noteOn(0, 60, velocity)}}, {48000, 128, 12000});
    const double tone = toneGainDb(out, 2000, 6800);
    CHECK(std::abs(tone - butterworthDb(4000, cutoff)) <= 1);
    CHECK(tone < lastTone);
    CHECK(out.left.at(4400) < lastLevel);
    lastTone = tone;
    lastLevel = out.left.at(4400);
  }
}

namespace {

// Generators for a 2 kHz tone whose cutoff sits on it at velocity 127:
// initialFilterFc 9540 less the velocity modulator's 18.75 is 9521.25 cents,
// 2000 Hz. A volume release of 100 s keeps a released note at full level.
std::vector<Generator> modulatedTone(std::vector<Generator> modulation) {
  modulation.insert(modulation.end(), {set(gen::kInitialFilterFc, 9540),
                                       set(gen::kReleaseVolEnv, 8000)});
  return modulation;
}

// The first frame on which renders of the 2 kHz tone font with generators
// `a` and with `b` differ, for one note at velocity 127.
std::size_t firstDifferentToneFrame(const std::vector<Generator>& a,
                                    const std::vector<Generator>& b,
                                    const std::vector<Timed>& midi) {
  const Run run{48000, 128, 4000};
  const auto first = play(toneFont(modulatedTone(a), 2000, 48000, 8000), midi, run);
  const auto second = play(toneFont(modulatedTone(b), 2000, 48000, 8000), midi, run);
  return firstDifference(first.right, second.right, run.frames);
}

// Whether `frame` lies within one frame after `derived`.
bool withinAFrame(std::size_t frame, double derived) {
  const auto at = static_cast<double>(frame);
  return at >= derived && at <= derived + 1;
}

} // namespace

TEST_CASE("the modulation envelope moves the cutoff by modEnvToFilterFc times its level") {
  // Stages of 3000 frames at 48 kHz (-4800 timecents): delay, attack and
  // hold; a decay of 6000 frames for a full fall (-3600) to a sustain 50 %
  // down; and from the note-off, a release as long. modEnvToFilterFc -2400
  // takes the cutoff from 2000 Hz at level 0 to 500 Hz at level 1, and to
  // 1000 Hz at the sustain's 0.5.
  const auto font = toneFont(
      modulatedTone({set(gen::kDelayModEnv, -4800), set(gen::kAttackModEnv, -4800),
                     set(gen::kHoldModEnv, -4800), set(gen::kDecayModEnv, -3600),
                     set(gen::kSustainModEnv, 500), set(gen::kReleaseModEnv, -3600),
                     set(gen::kModEnvToFilterFc, -2400)}),
      2000, 48000, 48000);
  const auto out = play(font, {{13, noteOn(0, 60, 127)}, {13 + 15000, noteOff(0, 60)}},
                        {48000, 128, 22000});
  // Windows well inside the delay, the hold, the sustain and after the
  // release, which falls linearly and ends 3000 frames after the note-off.
  for (const auto& [from, cutoff] :
       {std::pair{613, 2000.0}, std::pair{6613, 500.0}, std::pair{12613, 1000.0},
        std::pair{18613, 2000.0}}) {
    CAPTURE(from);
    const double gain = toneGainDb(out, from, from + 2400);
    CHECK(std::abs(gain - butterworthDb(2000, cutoff)) <= 1);
  }
  // Halfway through the release the level is 0.25: 1414 Hz.
  CHECK(std::abs(toneGainDb(out, 15913, 17113) - butterworthDb(2000, 1414.2)) <= 1);
}

TEST_CASE("the modulation envelope's stages begin and end within one frame of their derived times") {
  // Two renders agree until the stage that tells them apart takes effect.
  // Each stage here is 375 frames at 48 kHz (-8400 timecents), and the note
  // starts at frame 13.
  const std::vector<Timed> note{{13, noteOn(0, 60, 127)}};
  const std::vector<Generator> stages{
      set(gen::kDelayModEnv, -8400), set(gen::kAttackModEnv, -8400),
      set(gen::kHoldModEnv, -8400), set(gen::kModEnvToFilterFc, -2400)};
  const auto with = [&](std::vector<Generator> more) {
    more.insert(more.begin(), stages.begin(), stages.end());
    return more;
  };
  // The delay ends when the attack starts to move the cutoff, which a zone
  // with no depth never does. (A repeated generator keeps its last value.)
  CHECK(withinAFrame(
      firstDifferentToneFrame(with({}), with({set(gen::kModEnvToFilterFc, 0)}), note),
      13 + 375));
  // An attack twice as long with twice the depth moves the cutoff at the same
  // rate, as a linear attack does, until the shorter attack ends.
  CHECK(withinAFrame(firstDifferentToneFrame(with({}),
                                             with({set(gen::kAttackModEnv, -7200),
                                                   set(gen::kModEnvToFilterFc, -4800)}),
                                             note),
                     13 + 2 * 375));
  // The attack and hold end when the decay starts: here falling to a sustain
  // of 0 over 375 frames, or over 750 (-7200).
  CHECK(withinAFrame(
      firstDifferentToneFrame(
          with({set(gen::kDecayModEnv, -8400), set(gen::kSustainModEnv, 1000)}),
          with({set(gen::kDecayModEnv, -7200), set(gen::kSustainModEnv, 1000)}), note),
      13 + 3 * 375));
  // The decay ends 187.5 frames later at a sustain 50 % down, while one to a
  // sustain 75 % down falls on.
  const std::vector<Generator> toHalf{set(gen::kDecayModEnv, -8400),
                                      set(gen::kSustainModEnv, 500)};
  const std::vector<Generator> toQuarter{set(gen::kDecayModEnv, -8400),
                                         set(gen::kSustainModEnv, 750)};
  CHECK(withinAFrame(firstDifferentToneFrame(with(toHalf), with(toQuarter), note),
                     13 + 3 * 375 + 187.5));
  // The release starts on the note-off's frame.
  const std::vector<Timed> released{{13, noteOn(0, 60, 127)}, {3011, noteOff(0, 60)}};
  auto quick = toHalf, slow = toHalf;
  quick.push_back(set(gen::kReleaseModEnv, -8400));
  slow.push_back(set(gen::kReleaseModEnv, -7200));
  CHECK(withinAFrame(firstDifferentToneFrame(with(quick), with(slow), released), 3011));
}

namespace {

// A miniature of the default piano's layout, from ticket 10's measurements:
// two key ranges of two mono zones each, panned -500 and +500, playing 32 kHz
// samples whose headers say 60 but which overridingRootKey roots at the top
// of their range, all looping in mode 1. The preset adds 140 cB of
// attenuation, an ignored reverb send, and a release per velocity zone; the
// instrument cuts off at 12623 cents with -1000 from a modulation envelope
// whose only slow stage is a 100 s release.
//
// Each side is a sine with whole cycles in its 32000-point loop: the left a
// low tone for pitch and level, the right one that lands on the note's cutoff.
// `open` lifts the cutoff to 13500 cents with no modulation envelope.
File miniaturePiano(bool open = false) {
  const auto side = [](std::string name, double hertz) {
    Sample sample{std::move(name), sine(hertz / 32000, 36000), 32000};
    sample.loop = Loop{4000, 36000};
    return sample;
  };
  File file;
  file.samples = {side("A left", 250), side("A right", 7477), side("B left", 300),
                  side("B right", 4744)};
  const auto zone = [](int low, int high, int root, int pan, int sample) {
    return Zone{{range(gen::kKeyRange, low, high), set(gen::kOverridingRootKey, root),
                 set(gen::kSampleModes, 1), set(gen::kPan, pan),
                 set(gen::kHoldVolEnv, 1783), set(gen::kDecayVolEnv, 6590),
                 set(gen::kSampleId, sample)}};
  };
  file.instruments.push_back(
      {"piano",
       {Zone{{set(gen::kInitialFilterFc, open ? 13500 : 12623),
              set(gen::kModEnvToFilterFc, open ? 0 : -1000),
              set(gen::kReleaseModEnv, 7973), set(gen::kAttackVolEnv, -8359),
              set(gen::kSustainVolEnv, 1000), set(gen::kReleaseVolEnv, 0)}},
        zone(0, 62, 62, -500, 0), zone(0, 62, 62, 500, 1), zone(63, 127, 66, -500, 2),
        zone(63, 127, 66, 500, 3)}});
  file.presets.push_back(
      {"piano", 0, 0,
       {Zone{{set(gen::kInitialAttenuation, 140), set(gen::kReverbEffectsSend, 100)}},
        Zone{{range(gen::kVelRange, 0, 100), set(gen::kReleaseVolEnv, -617),
              set(gen::kInstrument, 0)}},
        Zone{{range(gen::kVelRange, 101, 127), set(gen::kReleaseVolEnv, 316),
              set(gen::kInstrument, 0)}}}});
  return file;
}

} // namespace

TEST_CASE("a miniature of the default piano plays each note as derived at two graph rates") {
  // Both notes are two semitones below their root. Key 60, velocity 127,
  // plays range A: 250 Hz on the left becomes 222.72 Hz; its cutoff is 12623
  // - 1000 - 18.75 cents, 6661 Hz, where the right side's 7477 Hz lands. Key
  // 64, velocity 85, plays range B: 300 Hz becomes 267.27 Hz; its cutoff, at
  // -806.25 cents for velocity, is 4227 Hz, where 4744 Hz lands. The level is
  // 16000 / 32768, less 14 dB and the default modulators' 40 log10(v / 127)
  // dB and 4.15 dB. The release, 2^(316 / 1200) s or 2^(-617 / 1200) s for
  // 100 dB, ends a note held through its hold 96 dB later.
  //
  // The cutoff is read against the same piano with its filter opened, which
  // leaves 13500 cents less the velocity's: 19.7 kHz or 12.4 kHz. That also
  // cancels what reading a 32 kHz sample between its points costs a high tone.
  const auto font = build(miniaturePiano());
  const auto opened = build(miniaturePiano(true));
  struct Note {
    int key, velocity;
    double left, right; // each side's frequency, two semitones down
    double cutoff, openCutoff, levelDb, releaseSeconds;
  };
  const double down = 0.8908987; // 2^(-2/12)
  for (const double rate : {44100.0, 48000.0}) {
    for (const auto& note :
         {Note{60, 127, 250 * down, 7477 * down, 6661.21, 19697.6, -24.3787, 1.152239},
          Note{64, 85, 300 * down, 4744 * down, 4226.71, 12498.7, -31.3541, 0.672189}}) {
      CAPTURE(rate);
      CAPTURE(note.key);
      const auto release = static_cast<std::uint64_t>(13 + rate * 1.5);
      const std::vector<Timed> midi{{13, noteOn(0, note.key, note.velocity)},
                                    {release, noteOff(0, note.key)}};
      const Run run{rate, 128, static_cast<std::uint64_t>(rate * 3)};
      const auto out = play(font, midi, run);
      const auto open = play(opened, midi, run);
      const auto from = static_cast<std::size_t>(rate / 20);
      const auto to = static_cast<std::size_t>(rate * 1.45);
      // Pitch and image, across the loops' seams: each channel carries only
      // its own side.
      CHECK(std::abs(cents(frequency(out.left, from, to, rate), note.left)) < 1);
      CHECK(std::abs(cents(frequency(out.right, from, to, rate), note.right)) < 1);
      // Level, on the left side, which the filter passes.
      CHECK(std::abs(db(amplitude(out.left, from, to)) - note.levelDb) <= 0.1);
      // Cutoff: the right side against the left, and against the open filter.
      const auto sides = [&](const Stereo& s) {
        return db(amplitude(s.right, from, to) / amplitude(s.left, from, to));
      };
      CHECK(std::abs(sides(out) - sides(open) -
                     (butterworthDb(note.right, note.cutoff) -
                      butterworthDb(note.right, note.openCutoff))) <= 1);
      // Release: both sides end together, by their envelope.
      const double end = static_cast<double>(release) + note.releaseSeconds * rate;
      CHECK(std::abs(static_cast<double>(lastSounding(out.left) + 1) - end) <= 1);
      CHECK(lastSounding(out.right) == lastSounding(out.left));
    }
  }
}

TEST_CASE("a stereo pair plays as two voices and keeps its image") {
  File file;
  file.samples = {{"left", steady(8000, 2000)}, {"right", steady(-4000, 2000)}};
  file.instruments.push_back({"pair", {{{set(gen::kPan, -500), set(gen::kSampleId, 0)}},
                                       {{set(gen::kPan, 500), set(gen::kSampleId, 1)}}}});
  file.presets.push_back({"preset", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  const auto out = play(build(file), {{13, noteOn(0, 60, 100)}}, {48000, 128, 1000});
  for (std::size_t i = 300; i < 1000; ++i) {
    CHECK(out.left[i] == doctest::Approx(8000.0 / 32768 * noteGain(100)));
    CHECK(out.right[i] == doctest::Approx(-4000.0 / 32768 * noteGain(100)));
  }
}

namespace {

// The left level once a note's envelope is flat, in sample-data units,
// without the note's gain.
double plays(const Bytes& font, int key, int velocity) {
  const auto out = play(font, {{13, noteOn(0, key, velocity)}}, {48000, 128, 400});
  return out.left[300] * 32768.0 / noteGain(velocity);
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
  const auto out = play(build(file), {{13, noteOn(0, 60, 100)}}, {48000, 128, 2000});
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
    const auto out = play(font, {{13, noteOn(0, 60, velocity)}, {1009, noteOff(0, 60)}},
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
  auto out = play(build(file), {{13, noteOn(0, 60, 100)}}, {48000, 128, 1000});
  CHECK(out.left[500] == 0);
  CHECK(out.right[500] == doctest::Approx(0.5 * noteGain(100)));
  // FluidR3's attack of -32768 timecents is clamped to -12000: 46.875 frames.
  out = play(envelopeFont({set(gen::kAttackVolEnv, -32768)}), {{13, noteOn(0, 60, 100)}},
             {48000, 128, 1000});
  const auto onset = firstSounding(out.left);
  std::size_t peak = onset;
  while (level(out, peak) < kFull) ++peak;
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

const std::vector<Timed> kOneNote{{13, noteOn(0, 60, 100)}};

double steadyLevel(const Stereo& out) {
  return out.left.at(300) * 32768.0 / noteGain(100);
}

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
  CHECK_THROWS_AS(rig.node().load(std::filesystem::path("missing.sf2")),
                  std::invalid_argument);
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
  file.instruments.push_back(
      {"i", {{{set(gen::kPan, -500), set(gen::kReleaseVolEnv, -1200)}},
             {{range(gen::kVelRange, 0, 63), set(gen::kSampleId, 0)}},
             {{range(gen::kVelRange, 64, 127), set(gen::kSampleId, 1)}}}});
  file.presets.push_back({"p", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  return build(file);
}

double levelAt(const Stereo& out, std::size_t frame) { return out.left.at(frame) * 32768.0; }

} // namespace

TEST_CASE("a note-on with velocity 0 is a note-off and note-off velocity is ignored") {
  const auto font = twoVelocities();
  const auto withZero = play(font, {{13, noteOn(0, 60, 100)}, {2003, MidiMsg{0x90, 60, 0}}},
                             {48000, 128, 30000});
  const auto withOff = play(font, {{13, noteOn(0, 60, 100)}, {2003, MidiMsg{0x80, 60, 64}}},
                            {48000, 128, 30000});
  CHECK(withZero.left == withOff.left);
  // From full level the release ends 96 dB down, 0.48 s later.
  CHECK(std::abs(static_cast<double>(lastSounding(withZero.left) + 1) - (2003 + 23040)) <= 1);
}

TEST_CASE("every channel plays the preset and other channel messages change nothing") {
  const auto font = twoVelocities();
  const auto reference = play(font, {{13, noteOn(0, 60, 100)}, {1511, noteOff(0, 60)}},
                              {48000, 128, 30000});
  // Channel 10 (status nibble 9), with a program change, volume, the sustain
  // pedal, pitch bend and pressure among its notes.
  const auto drums = play(font, {{13, MidiMsg{0xc9, 40, 0}},
                                 {13, noteOn(9, 60, 100)},
                                 {307, MidiMsg{0xb9, 7, 20}},
                                 {503, MidiMsg{0xe9, 0, 127}},
                                 {701, MidiMsg{0xc9, 1, 0}},
                                 {907, MidiMsg{0xd9, 100, 0}},
                                 {1009, MidiMsg{0xb9, 64, 127}},
                                 {1201, MidiMsg{0xa9, 60, 90}},
                                 {1511, noteOff(9, 60)}},
                          {48000, 128, 30000});
  CHECK(levelAt(reference, 1000) == doctest::Approx(2000 * noteGain(100)));
  CHECK(drums.left == reference.left);
  CHECK(drums.right == reference.right);
}

TEST_CASE("a key struck again sounds beside its earlier note and note-offs end the oldest first") {
  const auto out = play(twoVelocities(),
                        {{13, noteOn(0, 60, 100)}, {1009, noteOn(0, 60, 30)},
                         {2003, noteOff(0, 60)}, {30011, noteOff(0, 60)}},
                        {48000, 128, 54000});
  const double loud = 2000 * noteGain(100), soft = 1000 * noteGain(30);
  CHECK(levelAt(out, 1500) == doctest::Approx(loud + soft));
  // The loud note, struck first, is released first: 0.1 s later it is 20 dB
  // down, while the soft one still holds.
  CHECK(levelAt(out, 2003 + 4800) == doctest::Approx(soft + loud / 10).epsilon(1e-4));
  CHECK(levelAt(out, 29000) == doctest::Approx(soft));
  CHECK(std::abs(static_cast<double>(lastSounding(out.left) + 1) - (30011 + 23040)) <= 1);
}

TEST_CASE("a note-off with no held note on its key does nothing") {
  const auto font = twoVelocities();
  const auto reference = play(font, {{13, noteOn(0, 60, 100)}, {1009, noteOff(0, 60)}},
                              {48000, 128, 30000});
  const auto strays = play(font, {{13, noteOn(0, 60, 100)}, {503, noteOff(0, 61)},
                                  {601, noteOff(1, 60)}, {1009, noteOff(0, 60)},
                                  {1511, noteOff(0, 60)}},
                           {48000, 128, 30000});
  CHECK(strays.left == reference.left);
}

TEST_CASE("two parts in unison on two channels both sound and end separately") {
  const auto out = play(twoVelocities(),
                        {{13, noteOn(0, 60, 100)}, {13, noteOn(1, 60, 100)},
                         {1009, noteOff(1, 60)}},
                        {48000, 128, 30000});
  const double one = 2000 * noteGain(100);
  CHECK(levelAt(out, 900) == doctest::Approx(2 * one));
  CHECK(levelAt(out, 1009 + 4800) == doctest::Approx(one + one / 10).epsilon(1e-4));
  CHECK(levelAt(out, 29000) == doctest::Approx(one));
}

namespace {

// As twoVelocities(), but a soft note's sample is 600 points long, so a held
// soft note ends by itself soon after its onset.
Bytes shortSoftNotes() {
  File file;
  file.samples = {{"soft", steady(1000, 600)}, {"loud", steady(2000, 60000)}};
  file.instruments.push_back(
      {"i", {{{set(gen::kPan, -500), set(gen::kReleaseVolEnv, -1200)}},
             {{range(gen::kVelRange, 0, 63), set(gen::kSampleId, 0)}},
             {{range(gen::kVelRange, 64, 127), set(gen::kSampleId, 1)}}}});
  file.presets.push_back({"p", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  return build(file);
}

} // namespace

TEST_CASE("a held note that ended by itself still takes its key's next note-off") {
  // The soft note's sample runs out while it is held. The first note-off on
  // its key is spent on it, so the loud note struck after it holds on until
  // the second.
  const auto font = shortSoftNotes();
  const auto out = play(font,
                        {{13, noteOn(0, 60, 30)}, {1009, noteOn(0, 60, 100)},
                         {2003, noteOff(0, 60)}, {9001, noteOff(0, 60)}},
                        {48000, 128, 40000});
  const auto loud = play(font, {{1009, noteOn(0, 60, 100)}, {9001, noteOff(0, 60)}},
                         {48000, 128, 40000});
  REQUIRE(lastSounding(play(font, {{13, noteOn(0, 60, 30)}}).left) < 1009);
  CHECK(std::equal(out.left.begin() + 1009, out.left.end(), loud.left.begin() + 1009));
}

namespace {

// 6 ms at 48 kHz: how long a taken voice takes to fade out.
constexpr std::size_t kFade = 288;

std::function<void(SoundFontNode&)> voiceLimit(std::uint32_t limit) {
  return [limit](SoundFontNode& node) { node.setVoiceLimit(limit); };
}

// Whether two renders agree on every frame from `from` on, in both channels.
bool agreeFrom(const Stereo& a, const Stereo& b, std::size_t from) {
  return std::equal(a.left.begin() + from, a.left.end(), b.left.begin() + from,
                    b.left.end()) &&
         std::equal(a.right.begin() + from, a.right.end(), b.right.begin() + from,
                    b.right.end());
}

} // namespace

TEST_CASE("a full pool takes released notes before held ones and each oldest first") {
  // Each note is one voice and the pool holds two, so the third note, at frame
  // 2003, takes one. Once the taken note has faded, the render matches one in
  // which that note was never struck. Each note's velocity sets it apart, and
  // a release lasts 0.48 s, so every released note still sounds when taken.
  const auto font = envelopeFont({set(gen::kReleaseVolEnv, -1200)});
  const Run run{48000, 128, 8000};
  const auto takes = [&](const std::vector<Timed>& midi, const std::vector<Timed>& untaken) {
    return agreeFrom(play(font, midi, run, voiceLimit(2)), play(font, untaken, run),
                     2003 + kFade);
  };
  const Timed a{13, noteOn(0, 60, 100)}, b{503, noteOn(0, 62, 90)},
      c{2003, noteOn(0, 64, 80)};
  SUBCASE("a released note goes before an older held one") {
    CHECK(takes({a, b, {1009, noteOff(0, 62)}, c}, {a, c}));
  }
  SUBCASE("the released note whose note-off came first goes first") {
    // b was struck after a but released before it.
    CHECK(takes({a, b, {1009, noteOff(0, 62)}, {1511, noteOff(0, 60)}, c},
                {a, {1511, noteOff(0, 60)}, c}));
  }
  SUBCASE("with none released, the held note whose note-on came first goes first") {
    CHECK(takes({a, b, c}, {b, c}));
  }
}

namespace {

// Keys up to 60 play a steady half-scale sample full left, and higher keys
// full right, so a taken note and the note taking it are heard apart.
Bytes splitByKey() {
  File file;
  file.samples = {{"s", steady(16384, 60000)}};
  file.instruments.push_back(
      {"split", {{{range(gen::kKeyRange, 0, 60), set(gen::kPan, -500),
                   set(gen::kSampleId, 0)}},
                 {{range(gen::kKeyRange, 61, 127), set(gen::kPan, 500),
                   set(gen::kSampleId, 0)}}}});
  file.presets.push_back({"p", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  return build(file);
}

} // namespace

TEST_CASE("a taken voice fades linearly to zero over 6 ms as the new note starts at its frame") {
  const auto font = splitByKey();
  for (const double rate : {44100.0, 48000.0}) {
    CAPTURE(rate);
    const Run run{rate, 128, 6000};
    const Timed held{13, noteOn(0, 60, 100)}, taking{2003, noteOn(0, 64, 100)};
    const auto out = play(font, {held, taking}, run, voiceLimit(1));
    const auto alone = play(font, {held}, run);
    CHECK(firstDifference(out.left, alone.left, 2003) == 2003);
    // 6 ms is 264.6 frames at 44.1 kHz and 288 at 48 kHz. From the new note's
    // frame, the taken note falls in equal steps from its own level to zero.
    const auto fade = lastSounding(out.left) + 1 - 2003;
    CHECK(std::abs(static_cast<double>(fade) - 0.006 * rate) <= 1);
    for (std::size_t j = 0; j < fade; ++j) {
      CAPTURE(j);
      CHECK(out.left[2003 + j] ==
            doctest::Approx(alone.left[2003 + j] * static_cast<double>(fade - j) /
                            static_cast<double>(fade)));
    }
    // The new note plays as though the pool had room: the fading voice does
    // not count against the limit.
    CHECK(out.right == play(font, {taking}, run).right);
  }
}

TEST_CASE("a slot taken again within 6 ms stops its older tail at once") {
  // One voice. The note at 2003 takes the left note, and the note 101 frames
  // later takes it in turn, so the left note's fade stops there.
  const auto font = splitByKey();
  const Timed third{2104, noteOn(0, 67, 100)};
  const auto out = play(font, {{13, noteOn(0, 60, 100)}, {2003, noteOn(0, 64, 100)}, third},
                        {}, voiceLimit(1));
  CHECK(out.left[2103] != 0);
  CHECK(lastSounding(out.left) == 2103);
  CHECK(agreeFrom(out, play(font, {third}), 2104 + kFade));
}

TEST_CASE("a voice's slot is free from the frame after its last sound") {
  // With one voice, a note-on on the frame after a note's sample has run out
  // starts at once.
  const auto font = build(oneZone(steady(4000, 600)));
  const auto limit = [](SoundFontNode& node) { node.setVoiceLimit(1); };
  const auto first = play(font, {{13, noteOn(0, 60, 100)}}, {48000, 128, 2000}, limit);
  const auto last = lastSounding(first.left);
  REQUIRE(last < first.left.size());
  const auto both = play(font, {{13, noteOn(0, 60, 100)}, {last + 1, noteOn(0, 62, 100)}},
                         {48000, 128, 2000}, limit);
  CHECK(firstSounding(both.left, last + 1) < both.left.size());
}

namespace {

// A steady sample of 600 points looping over [100, 500), played point for
// point. `generators` sets its sampleModes and envelope.
Bytes loopedFont(std::vector<Generator> generators, Loop loop = {100, 500}) {
  auto file = oneZone(steady(4000, 600), std::move(generators));
  file.samples[0].loop = loop;
  return build(file);
}

// The frames from a voice's first sound to its last.
std::size_t soundingFrames(const Stereo& out) {
  return lastSounding(out.left) + 1 - firstSounding(out.left);
}

// Whether the left channel sounds on every frame of [from, to).
bool soundsThroughout(const Stereo& out, std::size_t from, std::size_t to) {
  for (std::size_t i = from; i < to; ++i)
    if (out.left.at(i) == 0) return false;
  return true;
}

// A ramp of `points` points, each 48 above the last.
std::vector<std::int16_t> ramp(std::size_t points) {
  std::vector<std::int16_t> result(points);
  for (std::size_t i = 0; i < points; ++i)
    result[i] = static_cast<std::int16_t>(48 * i + 64);
  return result;
}

// What a loop plays, written out: `points` from `first` to the loop's end,
// then the loop `times` more times.
std::vector<std::int16_t> unrolled(const std::vector<std::int16_t>& points,
                                   std::size_t first, Loop loop, int times) {
  std::vector<std::int16_t> result(points.begin() + first, points.begin() + loop.end);
  for (int i = 0; i < times; ++i)
    result.insert(result.end(), points.begin() + loop.start, points.begin() + loop.end);
  return result;
}

} // namespace

TEST_CASE("sampleModes 0 and 2 play to the sample's end") {
  for (const int mode : {0, 2}) {
    CAPTURE(mode);
    const auto out = play(loopedFont({set(gen::kSampleModes, mode)}), kOneNote);
    CHECK(soundingFrames(out) == 600);
  }
}

TEST_CASE("a mode 1 loop sounds through the release until the envelope's 96 dB floor") {
  // Release -1200 timecents falls 100 dB in 0.5 s: from full level the voice
  // ends 0.48 s after its note-off, though its sample has 600 points.
  const auto font =
      loopedFont({set(gen::kSampleModes, 1), set(gen::kReleaseVolEnv, -1200)});
  const auto out = play(font, {{13, noteOn(0, 60, 100)}, {12007, noteOff(0, 60)}},
                        {48000, 128, 40000});
  const auto onset = firstSounding(out.left);
  CHECK(soundsThroughout(out, onset, 12007 + 23000));
  CHECK(std::abs(static_cast<double>(lastSounding(out.left) + 1) - (12007 + 23040)) <= 1);
}

TEST_CASE("a held mode 1 loop ends only when its decay has fallen 96 dB") {
  // As on the default piano: a 100 dB sustain lies below the floor. Decay
  // -2400 timecents falls 100 dB in 0.25 s, so 96 dB takes 11520 frames.
  const auto font = loopedFont({set(gen::kSampleModes, 1), set(gen::kDecayVolEnv, -2400),
                                 set(gen::kSustainVolEnv, 1000), set(gen::kPan, -500)});
  const auto out = play(font, kOneNote, {48000, 128, 14000});
  const auto onset = firstSounding(out.left);
  CHECK(soundsThroughout(out, onset, lastSounding(out.left)));
  // The decay starts after the default attack and hold, 47 frames each.
  const double end = static_cast<double>(onset + 94 + 11520);
  CHECK(std::abs(static_cast<double>(lastSounding(out.left) + 1) - end) <= 1);
}

TEST_CASE("a mode 3 loop repeats while held then plays on through its end to the sample's end") {
  // The note-off comes 1943 points into the sample: past the loop's first
  // end, so 243 points into its fifth pass, at point 343. The voice then plays
  // its last 257 points and ends, long before its 1 s release would.
  const auto font = loopedFont({set(gen::kSampleModes, 3), set(gen::kReleaseVolEnv, 0)});
  const auto out = play(font, {{13, noteOn(0, 60, 100)}, {2003, noteOff(0, 60)}},
                        {48000, 128, 6000});
  const auto onset = firstSounding(out.left);
  REQUIRE(onset == 60); // the default delay holds the sample back 47 frames
  CHECK(soundsThroughout(out, onset, 2003));
  CHECK(lastSounding(out.left) == 2003 + 257 - 1);
}

TEST_CASE("a looping voice interpolates across its loop's seam and never reads beyond it") {
  // Played an octave down, the voice reads every point and every midpoint. The
  // points after the loop are loud: a voice reading them would differ from the
  // same ramp written out loop after loop. Short loops and loops near the
  // sample's ends play as given.
  std::size_t points = 0;
  Loop loop;
  SUBCASE("a loop in the middle") {
    points = 400;
    loop = {100, 200};
  }
  SUBCASE("a loop shorter than 32 points") {
    points = 60;
    loop = {20, 25};
  }
  SUBCASE("a loop 3 points from the start and 5 from the end") {
    points = 60;
    loop = {3, 55};
  }
  CAPTURE(points);
  auto samplePoints = ramp(points);
  std::fill(samplePoints.begin() + loop.end, samplePoints.end(), std::int16_t{30000});
  const std::vector<Generator> octaveDown{set(gen::kOverridingRootKey, 72),
                                          set(gen::kPan, -500)};
  auto loopingDown = octaveDown;
  loopingDown.push_back(set(gen::kSampleModes, 1));
  auto looped = oneZone(samplePoints, loopingDown);
  looped.samples[0].loop = loop;
  // Written out to at least 1400 points, which take 2800 frames.
  const auto times = static_cast<int>(1400 / (loop.end - loop.start));
  const auto writtenOut = oneZone(unrolled(samplePoints, 0, loop, times), octaveDown);
  const auto expected = play(build(writtenOut), kOneNote, {48000, 128, 3000});
  const auto out = play(build(looped), kOneNote, {48000, 128, 3000});
  const auto onset = firstSounding(expected.left);
  REQUIRE(lastSounding(expected.left) > onset + 2700);
  CHECK(firstDifference(out.left, expected.left, onset + 2700) == onset + 2700);
}

TEST_CASE("a loop outside its sample or an empty loop plays unlooped") {
  Loop loop;
  SUBCASE("starting before the sample") { loop = {-10, 500}; }
  SUBCASE("ending past the sample") { loop = {100, 601}; }
  SUBCASE("empty") { loop = {300, 300}; }
  SUBCASE("ending before it starts") { loop = {400, 300}; }
  for (const int mode : {1, 3}) {
    CAPTURE(mode);
    const auto out = play(loopedFont({set(gen::kSampleModes, mode)}, loop), kOneNote);
    CHECK(soundingFrames(out) == 600);
  }
}

TEST_CASE("address offsets move the start end and loop points by fine plus 32768 times coarse") {
  // Each point moves by a small net offset made of a coarse and a fine part:
  // the start by +18, the end by -28, the loop's start by +22 and its end by
  // +16. The voice plays points 18 to 571 and loops over [122, 416).
  const auto samplePoints = ramp(600);
  const std::vector<Generator> offsets{
      set(gen::kStartAddrsCoarseOffset, 1),     set(gen::kStartAddrsOffset, -32750),
      set(gen::kEndAddrsCoarseOffset, -1),      set(gen::kEndAddrsOffset, 32740),
      set(gen::kStartloopAddrsCoarseOffset, 1), set(gen::kStartloopAddrsOffset, -32746),
      set(gen::kEndloopAddrsCoarseOffset, 1),   set(gen::kEndloopAddrsOffset, -32752),
      set(gen::kPan, -500)};
  const auto withOffsets = [&](int mode) {
    auto generators = offsets;
    generators.push_back(set(gen::kSampleModes, mode));
    auto file = oneZone(samplePoints, generators);
    file.samples[0].loop = Loop{100, 400};
    return build(file);
  };
  const auto once = play(withOffsets(0), kOneNote, {48000, 128, 3000});
  CHECK(soundingFrames(once) == 572 - 18);

  const auto looped = play(withOffsets(1), kOneNote, {48000, 128, 3000});
  const auto expected = play(
      build(oneZone(unrolled(samplePoints, 18, {122, 416}, 10), {set(gen::kPan, -500)})),
      kOneNote, {48000, 128, 3000});
  CHECK(firstDifference(looped.left, expected.left, 2000) == 2000);
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

TEST_CASE("bytes after the RIFF chunk are ignored") {
  auto bytes = build(richFile());
  bytes.insert(bytes.end(), {0, 'J', 'U', 'N', 'K'});
  CHECK(loadError(bytes).empty());
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

TEST_CASE("a load checks every preset, not only the one it selects") {
  // The rich file's one preset, bank 0 program 0, is the one a load selects.
  auto file = richFile();
  SUBCASE("a later preset's instrument index past the instruments") {
    file.presets.push_back({"later", 7, 3, {{{set(gen::kInstrument, 2)}}}});
  }
  SUBCASE("a later preset's instrument playing a ROM sample") {
    file.samples.push_back({"rom", {}, 48000, 60, kRomSample});
    file.instruments.push_back({"rom", {{{set(gen::kSampleId, 3)}}}});
    file.presets.push_back({"later", 7, 3, {{{set(gen::kInstrument, 2)}}}});
  }
  CHECK(refused(build(file)));
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

// Counts the global allocations made while `counting` is set.
namespace {
bool counting = false;
std::size_t allocations = 0;
} // namespace

void* operator new(std::size_t size) {
  if (counting) ++allocations;
  if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
  throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

// A soundfont node that counts what its process() allocates.
class CountingSoundFontNode : public SoundFontNode {
public:
  using SoundFontNode::SoundFontNode;
  void process(ProcessContext& ctx) override {
    counting = true;
    SoundFontNode::process(ctx);
    counting = false;
  }
};

// One preset that takes every path at once: a looping stereo pair of sines
// at 44.1 kHz under both envelopes and the filter, layered from key 60 with a
// short quiet sample.
Bytes busyFont() {
  File file;
  file.samples = {{"left", sine(0.01, 30000), 44100, 57},
                  {"right", sine(0.013, 30000), 44100, 57},
                  {"short", sine(0.02, 700), 22050, 72}};
  file.samples[0].loop = file.samples[1].loop = Loop{5003, 29011};
  file.samples[2].loop = Loop{101, 607};
  // Both envelopes in full, the modulation envelope moving the cutoff.
  const Zone envelope{{set(gen::kDelayVolEnv, -9000), set(gen::kAttackVolEnv, -7000),
                       set(gen::kHoldVolEnv, -8000), set(gen::kDecayVolEnv, -3000),
                       set(gen::kSustainVolEnv, 150), set(gen::kReleaseVolEnv, -6000),
                       set(gen::kInitialFilterFc, 9000),
                       set(gen::kModEnvToFilterFc, -1500),
                       set(gen::kDelayModEnv, -8000), set(gen::kAttackModEnv, -7500),
                       set(gen::kHoldModEnv, -9000), set(gen::kDecayModEnv, -5000),
                       set(gen::kSustainModEnv, 400), set(gen::kReleaseModEnv, -5500)}};
  // The pair loops through its release. The layer loops while held up to key
  // 66, and above it runs out while held.
  file.instruments = {
      {"pair", {envelope,
                Zone{{set(gen::kPan, -500), set(gen::kSampleModes, 1),
                      set(gen::kSampleId, 0)}},
                Zone{{set(gen::kPan, 500), set(gen::kSampleModes, 1),
                      set(gen::kSampleId, 1)}}}},
      {"layer", {Zone{{set(gen::kInitialAttenuation, 90)}},
                 Zone{{range(gen::kKeyRange, 60, 66), set(gen::kSampleModes, 3),
                       set(gen::kSampleId, 2)}},
                 Zone{{range(gen::kKeyRange, 67, 127), set(gen::kSampleId, 2)}}}}};
  file.presets = {{"preset", 0, 0, {Zone{{set(gen::kInstrument, 0)}},
                                    Zone{{set(gen::kInstrument, 1)}}}}};
  return build(file);
}

// Overlapping notes on four channels: a retrigger, releases, a velocity-0
// note-off, a stray note-off, and with ten voices, a note the pool refuses.
const std::vector<Timed> kBusy{
    {1, noteOn(0, 60, 100)},   {3, noteOn(1, 64, 90)},     {3, noteOn(0, 48, 70)},
    {131, noteOn(0, 60, 110)}, {509, noteOff(0, 60)},      {1021, noteOn(9, 67, 1)},
    {2053, noteOff(1, 64)},    {2053, noteOn(1, 64, 0)},   {3001, noteOn(2, 72, 127)},
    {4093, noteOff(0, 60)},    {5003, noteOff(0, 48)},     {6007, noteOn(0, 60, 100)},
    {7919, noteOff(9, 67)},    {9001, noteOff(2, 72)},     {9001, noteOff(0, 60)},
    {12011, noteOn(3, 50, 64)}, {17389, noteOff(3, 50)}};

const auto kTenVoices = [](SoundFontNode& node) { node.setVoiceLimit(10); };

} // namespace

TEST_CASE("output is bit-identical at any block size and rate") {
  for (const double rate : {44100.0, 48000.0}) {
    CAPTURE(rate);
    const auto reference = play(busyFont(), kBusy, {rate, 512, 20000}, kTenVoices);
    CHECK(firstSounding(reference.left) < reference.left.size());
    for (const std::uint32_t block : {1u, 7u, 127u, 509u}) {
      CAPTURE(block);
      const auto out = play(busyFont(), kBusy, {rate, block, 20000}, kTenVoices);
      CHECK(out.left == reference.left);
      CHECK(out.right == reference.right);
    }
  }
}

TEST_CASE("process allocates nothing") {
  for (const std::uint32_t block : {7u, 509u}) {
    CAPTURE(block);
    Graph graph;
    const auto source = graph.addNode<MidiAt>(kBusy);
    const auto synth = graph.addNode<CountingSoundFontNode>(SoundFont::parse(busyFont()));
    auto& node = static_cast<SoundFontNode&>(graph.node(synth));
    node.setVoiceLimit(10);
    const auto sink = graph.addNode<CaptureSinkNode>(2);
    auto& capture = static_cast<CaptureSinkNode&>(graph.node(sink));
    graph.connect(source, static_cast<MidiAt&>(graph.node(source)).out, synth, node.midiIn());
    graph.connect(synth, node.out(), sink, capture.in());
    Engine engine(graph, 48000, block);
    allocations = 0;
    while (engine.streamTime() < 20000) engine.process(block);
    CHECK(allocations == 0);
  }
}
