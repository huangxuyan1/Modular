#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Instrument.h"
#include "StereoSinkNode.h"
#include "SoundFontTestFiles.h"
#include "modular/Engine.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/MidiFileNode.h"
#include "modular/nodes/SoundFontNode.h"

using namespace modular;
using namespace modular::nodes;
using modular::web::Instrument;
using modular::web::Rendering;
using modular::web::StereoSinkNode;
using sf2test::Bytes;
using sf2test::set;
namespace gen = sf2test::gen;

namespace {

// --- Standard MIDI Files -------------------------------------------------

struct Timed {
  std::uint32_t tick;
  MidiMsg message;
};

Timed on(std::uint32_t tick, int key, int velocity = 127, int channel = 0) {
  return {tick, {static_cast<std::uint8_t>(0x90 | channel),
                 static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(velocity)}};
}

Timed off(std::uint32_t tick, int key, int channel = 0) {
  return {tick, {static_cast<std::uint8_t>(0x80 | channel),
                 static_cast<std::uint8_t>(key), 0}};
}

void bigEndian(Bytes& out, std::uint32_t value, int size) {
  for (int i = size - 1; i >= 0; --i)
    out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

void variable(Bytes& out, std::uint32_t value) {
  std::uint8_t groups[5];
  int count = 0;
  do {
    groups[count++] = static_cast<std::uint8_t>(value & 0x7f);
    value >>= 7;
  } while (value != 0);
  while (count > 1) out.push_back(static_cast<std::uint8_t>(groups[--count] | 0x80));
  out.push_back(groups[0]);
}

// A format 0 file of `events`, in tick order, ending at `endTick`. At 480 ticks
// per quarter and the default 500,000 us per quarter, 960 ticks make a second:
// 50 frames a tick at 48 kHz.
Bytes midiFile(const std::vector<Timed>& events, std::uint32_t endTick) {
  Bytes track;
  std::uint32_t at = 0;
  for (const auto& event : events) {
    variable(track, event.tick - at);
    at = event.tick;
    track.insert(track.end(),
                 {event.message.status, event.message.data1, event.message.data2});
  }
  variable(track, endTick - at);
  track.insert(track.end(), {0xff, 0x2f, 0});
  Bytes file{'M', 'T', 'h', 'd'};
  bigEndian(file, 6, 4);
  bigEndian(file, 0, 2); // format 0
  bigEndian(file, 1, 2); // one track
  bigEndian(file, 480, 2);
  file.insert(file.end(), {'M', 'T', 'r', 'k'});
  bigEndian(file, static_cast<std::uint32_t>(track.size()), 4);
  file.insert(file.end(), track.begin(), track.end());
  return file;
}

// A chord on two channels under a melody, at three velocities. Two notes are
// still held at the file's end, 0.6 s, which releases them.
Bytes chord() {
  return midiFile({on(0, 48, 100), on(0, 55, 90, 1), on(97, 64), off(301, 64),
                   on(313, 67, 64), off(500, 48)},
                  576);
}

Bytes melody() {
  return midiFile({on(11, 60), off(241, 60), on(241, 62, 80), off(479, 62),
                   on(479, 64, 100, 2)},
                  960);
}

Bytes notMidi() { return {'R', 'I', 'F', 'F', 0, 0, 0, 0}; }

// --- soundfonts ----------------------------------------------------------

std::vector<std::int16_t> sine(double cyclesPerPoint, std::size_t points) {
  std::vector<std::int16_t> result(points);
  for (std::size_t i = 0; i < points; ++i)
    result[i] = static_cast<std::int16_t>(
        std::lround(16000 * std::sin(2 * 3.14159265358979 * cyclesPerPoint * i)));
  return result;
}

// A stereo pair of looping sines at 44.1 kHz, rooted away from the notes
// played, under a full volume envelope and a lowered cutoff.
Bytes toneFont() {
  sf2test::File file;
  sf2test::Sample left{"left", sine(0.01, 4410), 44100, 57};
  left.loop = sf2test::Loop{100, 4100};
  sf2test::Sample right{"right", sine(0.0125, 4410), 44100, 57};
  right.loop = sf2test::Loop{80, 4080};
  file.samples = {left, right};
  const sf2test::Zone envelope{{set(gen::kAttackVolEnv, -7000),
                                set(gen::kDecayVolEnv, -3000),
                                set(gen::kSustainVolEnv, 150),
                                set(gen::kReleaseVolEnv, -1200),
                                set(gen::kInitialFilterFc, 9000)}};
  file.instruments.push_back(
      {"pair", {envelope,
                sf2test::Zone{{set(gen::kPan, -500), set(gen::kSampleModes, 1),
                               set(gen::kSampleId, 0)}},
                sf2test::Zone{{set(gen::kPan, 500), set(gen::kSampleModes, 1),
                               set(gen::kSampleId, 1)}}}});
  file.presets.push_back({"pair", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  return sf2test::build(file);
}

// One zone looping a steady sample of `value`, full left, for its voice's
// whole life, so its level follows the volume envelope alone. At velocity 127,
// CC7's constant 4.2 dB leaves value / 32768 * 0.617 on the left and exact zeros
// on the right. `generators` set its envelope.
Bytes steadyFont(std::int16_t value, std::vector<sf2test::Generator> generators) {
  sf2test::File file;
  sf2test::Sample sample{"steady", std::vector<std::int16_t>(400, value)};
  sample.loop = sf2test::Loop{100, 300};
  file.samples.push_back(sample);
  generators.insert(generators.end(), {set(gen::kPan, -500), set(gen::kSampleModes, 1),
                                       set(gen::kSampleId, 0)});
  file.instruments.push_back({"steady", {{generators}}});
  file.presets.push_back({"steady", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  return sf2test::build(file);
}

// A short steady sample, played once by every zone of the preset: `zones`
// voices a note.
Bytes shortFont(int zones = 1) {
  sf2test::File file;
  file.samples.push_back({"short", std::vector<std::int16_t>(200, 1000)});
  sf2test::Instrument instrument{"short", {}};
  for (int i = 0; i < zones; ++i)
    instrument.zones.push_back(sf2test::Zone{{set(gen::kSampleId, 0)}});
  file.instruments.push_back(instrument);
  file.presets.push_back({"short", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  return sf2test::build(file);
}

// --- the same nodes, wired by hand ---------------------------------------

struct Stereo {
  std::uint64_t fileFrames = 0;
  std::vector<float> left, right;
};

// The instrument's nodes wired by hand into a two-channel CaptureSinkNode,
// rendered over the file's frames and `tail` more, `block` frames a block.
Stereo byHand(const Bytes& font, const Bytes& midi, double rate, std::uint32_t block,
              std::uint64_t tail) {
  Graph graph;
  const auto startId = graph.addNode<TriggerAtNode>(0);
  const auto fileId = graph.addNode<MidiFileNode>(MidiFile::parse(midi));
  const auto synthId = graph.addNode<SoundFontNode>(SoundFont::parse(font));
  const auto sinkId = graph.addNode<CaptureSinkNode>(2);
  auto& file = static_cast<MidiFileNode&>(graph.node(fileId));
  auto& synth = static_cast<SoundFontNode&>(graph.node(synthId));
  auto& sink = static_cast<CaptureSinkNode&>(graph.node(sinkId));
  auto& start = static_cast<TriggerAtNode&>(graph.node(startId));
  graph.connect(startId, start.trigger(), fileId, file.trigger());
  graph.connect(fileId, file.midi(), synthId, synth.midiIn());
  graph.connect(synthId, synth.out(), sinkId, sink.in());
  Engine engine(graph, rate, block);
  const auto frames = file.durationFrames() + tail;
  while (engine.streamTime() < frames)
    engine.process(static_cast<std::uint32_t>(
        std::min<std::uint64_t>(block, frames - engine.streamTime())));
  Stereo result{file.durationFrames(), {}, {}};
  const auto& samples = sink.samples();
  for (std::size_t i = 0; i + 1 < samples.size(); i += 2) {
    result.left.push_back(samples[i]);
    result.right.push_back(samples[i + 1]);
  }
  return result;
}

// Two seconds of frames at a whole-number rate: the cap on the release tail.
std::uint64_t capFrames(double rate) { return static_cast<std::uint64_t>(2 * rate); }

std::vector<float> firstFrames(const std::vector<float>& samples, std::size_t count) {
  return {samples.begin(),
          samples.begin() + static_cast<std::ptrdiff_t>(std::min(count, samples.size()))};
}

// Whether either channel reaches -80 dBFS at the frame.
bool audibleAt(const std::vector<float>& left, const std::vector<float>& right,
               std::size_t frame) {
  return std::abs(left.at(frame)) >= 1e-4f || std::abs(right.at(frame)) >= 1e-4f;
}

bool audibleIn(const Stereo& out, std::size_t from, std::size_t to) {
  for (std::size_t frame = from; frame < to; ++frame)
    if (audibleAt(out.left, out.right, frame)) return true;
  return false;
}

bool soundsIn(const std::vector<float>& left, const std::vector<float>& right,
              std::size_t from, std::size_t to) {
  for (std::size_t frame = from; frame < to; ++frame)
    if (left.at(frame) != 0 || right.at(frame) != 0) return true;
  return false;
}

} // namespace

TEST_CASE("a rendering is the soundfont node's own output with no gain at two rates and two block sizes") {
  const auto font = toneFont();
  const auto midi = chord();
  for (const double rate : {44100.0, 48000.0}) {
    CAPTURE(rate);
    const auto rendering = Instrument(font).render(midi, rate);
    CHECK(rendering.sampleRate == rate);
    REQUIRE(rendering.left.size() == rendering.right.size());
    CHECK(rendering.left != rendering.right);
    for (const std::uint32_t block : {127u, 509u}) {
      CAPTURE(block);
      const auto reference = byHand(font, midi, rate, block, capFrames(rate));
      CHECK(rendering.left.size() > reference.fileFrames); // the release was kept
      CHECK(rendering.left == firstFrames(reference.left, rendering.left.size()));
      CHECK(rendering.right == firstFrames(reference.right, rendering.right.size()));
    }
  }
}

TEST_CASE("one instrument renders clip after clip as a fresh one would, before and after other clips and failures") {
  const auto font = toneFont();
  Instrument instrument(font);
  const auto first = instrument.render(chord(), 48000);
  const auto other = instrument.render(melody(), 44100);
  const auto again = instrument.render(chord(), 48000);
  CHECK_THROWS_AS(instrument.render(notMidi(), 48000), std::invalid_argument);
  CHECK_THROWS_AS(instrument.render(chord(), 0), std::invalid_argument);
  const auto afterFailures = instrument.render(chord(), 48000);
  const auto fresh = Instrument(font).render(melody(), 44100);
  CHECK(first.left.size() > 0);
  CHECK(again.left == first.left);
  CHECK(again.right == first.right);
  CHECK(afterFailures.left == first.left);
  CHECK(afterFailures.right == first.right);
  CHECK(other.sampleRate == 44100);
  CHECK(other.left == fresh.left);
  CHECK(other.right == fresh.right);
}

TEST_CASE("a release that ends inside the cap is kept through its last frame at -80 dBFS") {
  // The file's end releases the note 10.4 dB below full scale. Falling 100 dB
  // a second, it reaches -80 dBFS about 0.7 s later and ends after 0.96 s.
  const auto font = steadyFont(16000, {set(gen::kReleaseVolEnv, 0)});
  const auto midi = midiFile({on(0, 60)}, 96);
  const auto rendering = Instrument(font).render(midi, 48000);
  const auto reference = byHand(font, midi, 48000, 509, capFrames(48000));
  const auto length = rendering.left.size();
  REQUIRE(reference.fileFrames == 4800);
  CHECK(rendering.right.size() == length);
  CHECK(length > 4800);
  CHECK(length < 4800 + capFrames(48000));
  CHECK(audibleAt(rendering.left, rendering.right, length - 1));
  CHECK_FALSE(audibleIn(reference, length, reference.left.size()));
  // What the trim cut still sounds, below the threshold.
  CHECK(soundsIn(reference.left, reference.right, length, reference.left.size()));
}

TEST_CASE("a release still above -80 dBFS when the cap ends is cut at the cap") {
  // Falling 25 dB a second, the note is still audible two seconds after the
  // file's end.
  const auto font = steadyFont(16000, {set(gen::kReleaseVolEnv, 2400)});
  const auto rendering = Instrument(font).render(midiFile({on(0, 60)}, 96), 44100);
  CHECK(rendering.left.size() == 4410 + 88200);
  CHECK(rendering.right.size() == rendering.left.size());
  CHECK(audibleAt(rendering.left, rendering.right, rendering.left.size() - 1));
}

TEST_CASE("a file ending in silence keeps every one of its frames") {
  // The note is released 0.1 s in and has ended about 0.48 s later, so the
  // file's last 0.4 s are exact zeros.
  const auto font = steadyFont(16000, {set(gen::kReleaseVolEnv, -1200)});
  const auto rendering =
      Instrument(font).render(midiFile({on(0, 60), off(96, 60)}, 960), 48000);
  REQUIRE(rendering.left.size() == 48000);
  CHECK(rendering.right.size() == 48000);
  CHECK(soundsIn(rendering.left, rendering.right, 0, 4800));
  CHECK_FALSE(soundsIn(rendering.left, rendering.right, 28800, 48000));
}

TEST_CASE("a quiet note in the tail is kept above -80 dBFS and cut below it") {
  // A steady 16 plays at -70.4 dBFS and a steady 4 at -82.5 dBFS. Each falls
  // 100 dB a second once the file's end releases it.
  const auto midi = midiFile({on(0, 60)}, 96);
  const auto above = steadyFont(16, {set(gen::kReleaseVolEnv, 0)});
  const auto kept = Instrument(above).render(midi, 48000);
  const auto keptReference = byHand(above, midi, 48000, 127, capFrames(48000));
  CHECK(kept.left.size() > 4800);
  CHECK(audibleAt(kept.left, kept.right, kept.left.size() - 1));
  CHECK_FALSE(audibleIn(keptReference, kept.left.size(), keptReference.left.size()));

  const auto below = steadyFont(4, {set(gen::kReleaseVolEnv, 0)});
  const auto cut = Instrument(below).render(midi, 48000);
  const auto cutReference = byHand(below, midi, 48000, 127, capFrames(48000));
  REQUIRE(soundsIn(cutReference.left, cutReference.right, 4800,
                   cutReference.left.size()));
  CHECK_FALSE(audibleIn(cutReference, 0, cutReference.left.size()));
  CHECK(cut.left.size() == 4800);
  CHECK(cut.right.size() == 4800);
}

TEST_CASE("a file of zero duration renders zero frames") {
  Instrument instrument(steadyFont(16000, {}));
  for (const auto& midi : {midiFile({}, 0), midiFile({on(0, 60)}, 0)}) {
    const auto rendering = instrument.render(midi, 48000);
    CHECK(rendering.sampleRate == 48000);
    CHECK(rendering.left.empty());
    CHECK(rendering.right.empty());
  }
}

TEST_CASE("malformed bytes fail with the nodes' own messages") {
  std::string readerMessage;
  try {
    SoundFont::parse(chord());
  } catch (const std::invalid_argument& error) {
    readerMessage = error.what();
  }
  REQUIRE(readerMessage.starts_with("soundfont: "));
  CHECK_THROWS_WITH_AS(Instrument{chord()}, readerMessage.c_str(), std::invalid_argument);
  CHECK_THROWS_WITH_AS(Instrument{Bytes{}}, "soundfont: truncated chunk",
                       std::invalid_argument);
  sf2test::File compressed;
  compressed.major = 3;
  CHECK_THROWS_WITH_AS(Instrument(sf2test::build(compressed)),
                       "soundfont: compressed samples (sf3) are not supported",
                       std::invalid_argument);
  Instrument instrument(toneFont());
  CHECK_THROWS_WITH_AS(instrument.render(notMidi(), 48000),
                       "midi file: missing MThd header", std::invalid_argument);
  const auto rendering = instrument.render(chord(), 48000);
  CHECK(rendering.left == Instrument(toneFont()).render(chord(), 48000).left);
}

TEST_CASE("a rate that is not finite and positive is refused before the MIDI bytes are read") {
  Instrument instrument(toneFont());
  for (const double rate : {0.0, -48000.0, std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity()}) {
    CAPTURE(rate);
    CHECK_THROWS_WITH_AS(instrument.render(chord(), rate),
                         "render: sample rate must be finite and positive",
                         std::invalid_argument);
    CHECK_THROWS_WITH_AS(instrument.render(notMidi(), rate),
                         "render: sample rate must be finite and positive",
                         std::invalid_argument);
  }
}

TEST_CASE("a rendering whose frames cannot be counted is refused and the instrument renders on") {
  // At 1e19 Hz the file's single tick is about 1.04e16 frames, which a frame
  // count holds, but its two-second tail is 2e19, which none does.
  Instrument instrument(shortFont());
  CHECK_THROWS_WITH_AS(instrument.render(midiFile({}, 1), 1e19), "render: too many frames",
                       std::invalid_argument);
  const auto midi = midiFile({on(0, 60)}, 96);
  const auto fresh = Instrument(shortFont()).render(midi, 48000);
  CHECK(instrument.render(midi, 48000).left == fresh.left);
}

TEST_CASE("the voice limit defaults to the soundfont node's 256") {
  // 257 notes at once: a pool of 256 voices cannot play them all.
  const auto midi = midiFile(std::vector<Timed>(257, on(0, 60)), 2);
  const auto font = shortFont();
  const auto byDefault = Instrument(font).render(midi, 48000);
  const auto of256 = Instrument(font, 256).render(midi, 48000);
  const auto of257 = Instrument(font, 257).render(midi, 48000);
  CHECK(byDefault.left == of256.left);
  CHECK(byDefault.right == of256.right);
  CHECK(byDefault.left != of257.left);
}

TEST_CASE("a voice limit below what one note of the preset starts fails with the node's own errors") {
  const auto font = shortFont(2);
  const auto midi = midiFile({on(0, 60)}, 96);
  CHECK_THROWS_WITH_AS(Instrument(font, 1).render(midi, 48000),
                       "soundfont: voice limit is below the voices one note starts",
                       std::invalid_argument);
  CHECK_NOTHROW(Instrument(font, 2).render(midi, 48000));
  CHECK_THROWS_WITH_AS(Instrument(font, 0), "soundfont: voice limit must be 1 to 65535",
                       std::invalid_argument);
}

// Counts the global allocations made while `counting` is set, and fails any
// larger than `largest` while it is set, as if memory had run out.
namespace {
bool counting = false;
std::size_t allocations = 0;
std::size_t largest = 0;
} // namespace

void* operator new(std::size_t size) {
  if (counting) ++allocations;
  if (largest != 0 && size > largest) throw std::bad_alloc();
  if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
  throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

// Stereo that counts frames: the left channel plays each frame's stream time
// and the right its negative, so a sink's frames show where they came from.
class FrameCounter : public Node {
public:
  explicit FrameCounter(Graph& graph) : Node(graph) {
    out = addOutput(PortType::audio(2), "out");
  }
  void process(ProcessContext& ctx) override {
    const auto audio = ctx.audioOut(out);
    for (std::uint32_t i = 0; i < ctx.numFrames(); ++i) {
      const auto frame = static_cast<float>(ctx.blockStartFrame() + i);
      audio.channels[0][i] = frame;
      audio.channels[1][i] = -frame;
    }
  }
  PortId out;
};

// A sink that counts what its process() allocates.
class CountingSink : public StereoSinkNode {
public:
  using StereoSinkNode::StereoSinkNode;
  void process(ProcessContext& ctx) override {
    counting = true;
    StereoSinkNode::process(ctx);
    counting = false;
  }
};

// Frame counts into a sink: its own graph, never an instrument's.
class SinkRig {
public:
  SinkRig() {
    const auto source = graph_.addNode<FrameCounter>();
    sink_ = graph_.addNode<CountingSink>();
    graph_.connect(source, static_cast<FrameCounter&>(graph_.node(source)).out, sink_,
                   sink().in());
  }
  StereoSinkNode& sink() { return static_cast<StereoSinkNode&>(graph_.node(sink_)); }
  // Processes `frames` in a fresh Engine.
  void run(std::uint64_t frames, std::uint32_t block) {
    Engine engine(graph_, 48000, block);
    while (engine.streamTime() < frames)
      engine.process(static_cast<std::uint32_t>(
          std::min<std::uint64_t>(block, frames - engine.streamTime())));
  }

private:
  Graph graph_;
  NodeId sink_ = kNoNode;
};

bool countsFrom(const std::vector<float>& left, const std::vector<float>& right,
                std::size_t count) {
  if (left.size() != count || right.size() != count) return false;
  for (std::size_t i = 0; i < count; ++i)
    if (left[i] != static_cast<float>(i) || right[i] != -static_cast<float>(i))
      return false;
  return true;
}

} // namespace

TEST_CASE("the stereo sink holds planar channels, allocates nothing in process and stops at its capacity") {
  for (const std::uint32_t block : {7u, 509u}) {
    CAPTURE(block);
    SinkRig rig;
    rig.sink().setCapacity(10007);
    allocations = 0;
    rig.run(12011, block);
    CHECK(allocations == 0);
    const auto channels = rig.sink().take();
    CHECK(countsFrom(channels.left, channels.right, 10007));
  }
}

TEST_CASE("the stereo sink starts again from its first frame in each fresh Engine") {
  SinkRig rig;
  rig.sink().setCapacity(301);
  rig.run(211, 64);
  rig.run(97, 64);
  const auto channels = rig.sink().take();
  CHECK(countsFrom(channels.left, channels.right, 97));
  const auto emptied = rig.sink().take();
  CHECK(emptied.left.empty());
  CHECK(emptied.right.empty());
}

namespace {

// While it lives, no allocation larger than `bytes` succeeds.
class MemoryCap {
public:
  explicit MemoryCap(std::size_t bytes) { largest = bytes; }
  ~MemoryCap() { largest = 0; }
  MemoryCap(const MemoryCap&) = delete;
  MemoryCap& operator=(const MemoryCap&) = delete;
};

// 960 ticks make a second (see midiFile), so ten minutes are 576,000 ticks.
constexpr std::uint32_t kTenMinutes = 600 * 960;

} // namespace

TEST_CASE("a file longer than ten minutes is refused before its rendering is sized") {
  // One tick past ten minutes. At 48 kHz its rendering would take 230 MB, but
  // here no allocation over a mebibyte succeeds, so the refusal comes first.
  // At 1e19 Hz its frames could not even be counted.
  Instrument instrument(toneFont());
  const auto tooLong = midiFile({on(0, 60), off(kTenMinutes, 60)}, kTenMinutes + 1);
  {
    const MemoryCap cap(1 << 20);
    for (const double rate : {48000.0, 1e19}) {
      CAPTURE(rate);
      CHECK_THROWS_WITH_AS(instrument.render(tooLong, rate),
                           "render: file is longer than 10 minutes", std::invalid_argument);
    }
  }
  CHECK(instrument.render(chord(), 48000).left ==
        Instrument(toneFont()).render(chord(), 48000).left);
}

TEST_CASE("a file of exactly ten minutes renders and ends at most two seconds after the file") {
  // At 960 Hz a tick is a frame. The note sounds through the file's last second
  // and is still above -80 dBFS when the two-second cap ends, so the rendering
  // runs to the cap.
  const auto font = steadyFont(16000, {set(gen::kReleaseVolEnv, 2400)});
  const auto rendering =
      Instrument(font).render(midiFile({on(kTenMinutes - 960, 60)}, kTenMinutes), 960);
  CHECK(rendering.sampleRate == 960);
  CHECK(rendering.left.size() == kTenMinutes + 1920);
  CHECK(rendering.right.size() == rendering.left.size());
  CHECK(audibleAt(rendering.left, rendering.right, rendering.left.size() - 1));
}

TEST_CASE("a render that runs out of memory throws std::bad_alloc and the instrument renders on") {
  // A minute at 48 kHz takes 11.5 MB a channel, more than the memory allows.
  Instrument instrument(toneFont());
  const auto minute = midiFile({on(0, 60), off(60 * 960, 60)}, 60 * 960);
  {
    const MemoryCap cap(1 << 20);
    CHECK_THROWS_AS(instrument.render(minute, 48000), std::bad_alloc);
  }
  CHECK(instrument.render(chord(), 48000).left ==
        Instrument(toneFont()).render(chord(), 48000).left);
}

TEST_CASE("a rate whose two-second tail cannot be counted is refused before any node prepares") {
  // A file of zero duration renders nothing, but at these rates the soundfont
  // node would round its fades from numbers no integer holds.
  Instrument instrument(shortFont());
  for (const double rate : {1e300, std::numeric_limits<double>::max()}) {
    CAPTURE(rate);
    CHECK_THROWS_WITH_AS(instrument.render(midiFile({}, 0), rate), "render: too many frames",
                         std::invalid_argument);
  }
  CHECK(instrument.render(midiFile({}, 0), 48000).left.empty());
}
