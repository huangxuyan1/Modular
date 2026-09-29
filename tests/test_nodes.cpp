#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "modular/Engine.h"
#include "modular/Graph.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/MidiNodes.h"
#include "modular/nodes/WavRecorderNode.h"
#include "WavTestFiles.h"
#include "modular/nodes/ManualTriggerNode.h"
#include "modular/nodes/WavFile.h"

using namespace modular;
using namespace modular::nodes;

namespace {

// A node and its id kept together. addNode() returns the id alone, and wiring
// needs both: the id to name the node, and the node itself to ask it for a
// port id through the accessors it publishes. No test below names a port by
// position.
template <class T>
struct Placed {
  NodeId id = kNoNode;
  T* node = nullptr;
  T* operator->() const { return node; }
};

template <class T, class... Args>
Placed<T> place(Graph& g, Args&&... args) {
  const NodeId id = g.addNode<T>(std::forward<Args>(args)...);
  return Placed<T>{id, static_cast<T*>(&g.node(id))};
}

// A steady audio level, so mixing can be checked as exact arithmetic.
class ConstAudioNode : public Node {
public:
  ConstAudioNode(Graph& graph, float v, std::uint32_t channels)
      : Node(graph), v_(v), channels_(channels) {
    out_ = addOutput(PortType::audio(channels), "out");
  }

  PortId out() const { return out_; }

  void process(ProcessContext& ctx) override {
    const auto out = ctx.audioOut(out_);
    for (std::uint32_t c = 0; c < channels_; ++c)
      for (std::uint32_t i = 0; i < ctx.numFrames(); ++i) out.channels[c][i] = v_;
  }

private:
  PortId out_ = kNoPort;
  float v_;
  std::uint32_t channels_;
};

class ScheduledTriggers : public Node {
public:
  ScheduledTriggers(Graph& graph, std::vector<std::uint64_t> frames)
      : Node(graph), frames_(std::move(frames)) {
    out = addOutput(PortType::event(subtype::Trigger), "trigger");
  }
  void process(ProcessContext& ctx) override {
    for (auto frame : frames_)
      if (frame >= ctx.blockStartFrame() && frame < ctx.blockStartFrame() + ctx.numFrames())
        ctx.eventsOut(out).push(Event::make(
            static_cast<std::uint32_t>(frame - ctx.blockStartFrame()), TriggerPayload{}));
  }
  PortId out;
private:
  std::vector<std::uint64_t> frames_;
};

class StereoPattern : public Node {
public:
  explicit StereoPattern(Graph& graph) : Node(graph) {
    out = addOutput(PortType::audio(2), "out");
  }
  void process(ProcessContext& ctx) override {
    constexpr float kSamples[]{0, 0.25f, 0.5f, 0.75f};
    const auto output = ctx.audioOut(out);
    for (std::uint32_t frame = 0; frame < ctx.numFrames(); ++frame) {
      const auto value = kSamples[(ctx.blockStartFrame() + frame) % 4];
      output.channels[0][frame] = value;
      output.channels[1][frame] = -value;
    }
  }
  PortId out;
};

// Records every event on every input at its absolute stream frame, so
// assertions can talk about time rather than about blocks.
class EventLogNode : public Node {
public:
  // (frame, port index, status, note) — ordered so sorting gives a canonical
  // sequence that cannot depend on how the blocks happened to fall. The port
  // is logged by index because that is what the assertions are about: which
  // voice, not which id.
  using Rec = std::tuple<std::uint64_t, std::size_t, int, int>;

  EventLogNode(Graph& graph, std::uint32_t ports) : Node(graph) {
    for (std::uint32_t p = 0; p < ports; ++p)
      ins_.push_back(addInput(PortType::event(subtype::Midi), "in" + std::to_string(p)));
  }

  PortId in(std::uint32_t index) const { return ins_[index]; }

  void process(ProcessContext& ctx) override {
    for (std::size_t p = 0; p < ins_.size(); ++p)
      for (const Event& e : ctx.eventsIn(ins_[p])) {
        const MidiMsg m = e.as<MidiMsg>();
        log.push_back({ctx.blockStartFrame() + e.offset, p, m.status, m.data1});
      }
  }

  std::vector<Rec> sorted() const {
    std::vector<Rec> s = log;
    std::sort(s.begin(), s.end());
    return s;
  }

  std::vector<Rec> log;

private:
  std::vector<PortId> ins_;
};

void render(Engine& e, std::uint64_t totalFrames, std::uint32_t block) {
  while (e.streamTime() < totalFrames) {
    const std::uint64_t remaining = totalFrames - e.streamTime();
    e.process(remaining < block ? static_cast<std::uint32_t>(remaining) : block);
  }
}

// A chord struck at `at` and released 4000 frames later, as one MIDI clip.
std::vector<MidiBlockNode::Entry> chord(std::vector<std::uint8_t> notes,
                                        std::uint64_t at) {
  std::vector<MidiBlockNode::Entry> clip;
  for (std::uint8_t n : notes) clip.push_back({at, MidiMsg{0x90, n, 100}});
  for (std::uint8_t n : notes) clip.push_back({at + 4000, MidiMsg{0x80, n, 0}});
  return clip;
}

// Notes struck at explicit frames, each released 4000 frames later. Where a
// chord leaves the arrival order to the event buffer, this pins it to the
// clock — which is what a test about *ordering* has to do.
std::vector<MidiBlockNode::Entry> sequence(
    const std::vector<std::pair<std::uint64_t, std::uint8_t>>& notes) {
  std::vector<MidiBlockNode::Entry> clip;
  for (const auto& [at, n] : notes) clip.push_back({at, MidiMsg{0x90, n, 100}});
  for (const auto& [at, n] : notes) clip.push_back({at + 4000, MidiMsg{0x80, n, 0}});
  return clip;
}

} // namespace

TEST_CASE("a wired recorder captures the Start sample and excludes the Stop sample") {
  for (const std::uint32_t block : {7, 64, 331}) {
    WavTestFiles files;
    Graph graph;
    const auto audio = place<StereoPattern>(graph);
    const auto start = place<TriggerAtNode>(graph, 101);
    const auto stop = place<TriggerAtNode>(graph, 509);
    const auto recorder = place<WavRecorderNode>(graph, files.directory, "take", 2);
    graph.connect(audio.id, audio->out, recorder.id, recorder->in());
    graph.connect(start.id, start->trigger(), recorder.id, recorder->start());
    graph.connect(stop.id, stop->trigger(), recorder.id, recorder->stop());
    Engine engine(graph, 48000, block);
    render(engine, 701, block);
    const auto wav = readTestWav(files.directory / "take-1.wav");
    CHECK(wav.channels == 2);
    CHECK(wav.sampleRate == 48000);
    REQUIRE(wav.samples.size() == 816);
    // Frame 101 is the positive quarter-scale/negative quarter-scale pair.
    const std::int16_t cycle[]{8191, -8191, 16383, -16383, 24575, -24575, 0, 0};
    for (std::size_t i = 0; i < wav.samples.size(); ++i) CHECK(wav.samples[i] == cycle[i % 8]);
  }
}

TEST_CASE("a failed completed take refuses Start until Retry saves it or Discard releases it") {
  WavTestFiles files;
  const auto directory = files.directory / "missing";
  Graph graph;
  const auto audio = place<ConstAudioNode>(graph, -0.5f, 1);
  const auto start = place<ManualTriggerNode>(graph);
  const auto stop = place<ManualTriggerNode>(graph, ManualTriggerNode::Label::Stop);
  const auto recorder = place<WavRecorderNode>(graph, directory);
  graph.connect(audio.id, audio->out(), recorder.id, recorder->in());
  graph.connect(start.id, start->trigger(), recorder.id, recorder->start());
  graph.connect(stop.id, stop->trigger(), recorder.id, recorder->stop());
  Engine engine(graph, 48000, 13);
  start->press(); engine.process(13);
  stop->press(); CHECK_NOTHROW(engine.process(13));
  CHECK(recorder->state() == WavRecorderNode::State::Failed);
  CHECK(recorder->statusText() == "Output folder not found. Take retained; Retry or Discard.");
  CHECK(engine.errorMessage().empty());
  start->press(); engine.process(13);
  stop->press(); engine.process(13);
  std::filesystem::create_directory(directory);
  recorder->retry(); engine.process(13);
  CHECK(recorder->state() == WavRecorderNode::State::Idle);
  CHECK(readTestWav(directory / "take-1.wav").samples ==
        std::vector<std::int16_t>(13, -16383));
  std::filesystem::rename(directory, files.directory / "completed");
  start->press(); engine.process(7);
  stop->press(); engine.process(7);
  CHECK(recorder->state() == WavRecorderNode::State::Failed);
  recorder->discard(); engine.process(7);
  CHECK(recorder->state() == WavRecorderNode::State::Idle);
  std::filesystem::create_directory(directory);
  start->press(); engine.process(5);
  stop->press(); engine.process(5);
  CHECK(readTestWav(directory / "take-1.wav").samples.size() == 5);
  CHECK(readTestWav(files.directory / "completed/take-1.wav").samples.size() == 13);
}

#ifdef _WIN32
TEST_CASE("a recorder saves to a Windows directory pasted with surrounding quotes") {
  WavTestFiles files;
  const auto output = files.directory / L"My recordings-é";
  std::filesystem::create_directory(output);
  const auto pasted = std::filesystem::path(L"\"" + output.wstring() + L"\"");
  for (const bool configure : {false, true}) {
    Graph graph;
    const auto start = place<TriggerAtNode>(graph, 3);
    const auto stop = place<TriggerAtNode>(graph, 17);
    const auto recorder = place<WavRecorderNode>(graph, configure ? files.directory : pasted);
    if (configure) recorder->configure(graph, recorder.id, pasted, "take", 1);
    graph.connect(start.id, start->trigger(), recorder.id, recorder->start());
    graph.connect(stop.id, stop->trigger(), recorder.id, recorder->stop());
    Engine engine(graph, 48000, 23);
    engine.process(23);
    INFO(recorder->statusText());
    CHECK(recorder->state() == WavRecorderNode::State::Idle);
    const auto file = output / (configure ? "take-2.wav" : "take-1.wav");
    REQUIRE(std::filesystem::exists(file));
    CHECK(readTestWav(file).samples == std::vector<std::int16_t>(14, 0));
    CHECK(recorder->directory() == output);
  }
}
#endif

TEST_CASE("duplicate triggers and Stop ties preserve distinct numbered takes across blocks") {
  for (const std::uint32_t block : {7, 64, 331}) {
    WavTestFiles files;
    std::ofstream(files.directory / "take-1.wav") << "unrelated";
    std::filesystem::create_directory(files.directory / "take-2.wav");
    Graph graph;
    const auto audio = place<ConstAudioNode>(graph, 0.5f, 1);
    const auto starts = place<ScheduledTriggers>(graph,
        std::vector<std::uint64_t>{11, 101, 101, 103, 509, 601, 701, 809});
    const auto stops = place<ScheduledTriggers>(graph,
        std::vector<std::uint64_t>{11, 17, 509, 509, 607, 701});
    const auto recorder = place<WavRecorderNode>(graph, files.directory);
    graph.connect(audio.id, audio->out(), recorder.id, recorder->in());
    graph.connect(starts.id, starts->out, recorder.id, recorder->start());
    graph.connect(stops.id, stops->out, recorder.id, recorder->stop());
    {
      Engine engine(graph, 48000, block);
      render(engine, 901, block);
      CHECK(recorder->state() == WavRecorderNode::State::Recording);
    }
    CHECK(recorder->state() == WavRecorderNode::State::Idle);
    CHECK(readTestWav(files.directory / "take-3.wav").samples ==
          std::vector<std::int16_t>(408, 16383));
    CHECK(readTestWav(files.directory / "take-4.wav").samples ==
          std::vector<std::int16_t>(6, 16383));
    std::ifstream existing(files.directory / "take-1.wav");
    std::string text; existing >> text;
    CHECK(text == "unrelated");
    CHECK(std::filesystem::is_directory(files.directory / "take-2.wav"));
    CHECK(std::distance(std::filesystem::directory_iterator(files.directory),
                        std::filesystem::directory_iterator{}) == 4);
  }
}

TEST_CASE("MIDI completion releases sound but only the recorder Stop completes the WAV") {
  std::vector<std::int16_t> reference;
  for (const std::uint32_t block : {7, 64, 331}) {
    WavTestFiles files;
    Graph graph;
    const auto start = place<TriggerAtNode>(graph, 101);
    const auto stop = place<TriggerAtNode>(graph, 809);
    const auto midi = place<MidiBlockNode>(graph, std::vector<MidiBlockNode::Entry>{
        {0, {0x90, 69, 100}}, {211, {0xb0, 1, 0}}});
    const auto synth = place<SineSynthNode>(graph);
    const auto recorder = place<WavRecorderNode>(graph, files.directory);
    graph.connect(start.id, start->trigger(), midi.id, midi->trigger());
    graph.connect(midi.id, midi->midi(), synth.id, synth->midiIn());
    graph.connect(synth.id, synth->out(), recorder.id, recorder->in());
    graph.connect(start.id, start->trigger(), recorder.id, recorder->start());
    graph.connect(stop.id, stop->trigger(), recorder.id, recorder->stop());
    Engine engine(graph, 48000, block);
    render(engine, 701, block);
    CHECK(recorder->state() == WavRecorderNode::State::Recording);
    CHECK(std::filesystem::is_empty(files.directory));
    render(engine, 901, block);
    const auto wav = readTestWav(files.directory / "take-1.wav");
    REQUIRE(wav.samples.size() == 708);
    CHECK(std::any_of(wav.samples.begin(), wav.samples.begin() + 211,
                     [](auto sample) { return sample != 0; }));
    CHECK(std::all_of(wav.samples.begin() + 211, wav.samples.end(),
                     [](auto sample) { return sample == 0; }));
    if (reference.empty()) reference = wav.samples;
    else CHECK(wav.samples == reference);
  }
}

TEST_CASE("a partial external write is cleaned and Retry preserves existing output") {
  WavTestFiles files;
  bool fail = true;
  const auto writer = [&](const std::filesystem::path& path,
                          const std::vector<float>& samples,
                          std::uint32_t channels, std::uint32_t rate) {
    if (!fail) { writeWav16(path, samples, channels, rate); return; }
    std::ofstream(path, std::ios::binary) << "partial";
    throw std::runtime_error("test disk full");
  };
  Graph graph;
  const auto start = place<ManualTriggerNode>(graph);
  const auto stop = place<ManualTriggerNode>(graph);
  const auto recorder = place<WavRecorderNode>(graph, files.directory, "take", 1, writer);
  graph.connect(start.id, start->trigger(), recorder.id, recorder->start());
  graph.connect(stop.id, stop->trigger(), recorder.id, recorder->stop());
  {
    Engine engine(graph, 48000, 13);
    start->press(); engine.process(13);
    stop->press(); engine.process(13);
    CHECK(recorder->state() == WavRecorderNode::State::Failed);
    CHECK(std::filesystem::is_empty(files.directory));
    recorder->retry(); engine.process(13);
    CHECK(recorder->state() == WavRecorderNode::State::Failed);
    CHECK(std::filesystem::is_empty(files.directory));
    std::ofstream(files.directory / "take-1.wav") << "unrelated";
    fail = false;
    recorder->retry(); engine.process(13);
    CHECK(readTestWav(files.directory / "take-2.wav").samples ==
          std::vector<std::int16_t>(13, 0));
    fail = true;
    start->press(); engine.process(13);
    stop->press(); engine.process(13);
  }
  CHECK(recorder->state() == WavRecorderNode::State::Idle);
  CHECK(std::distance(std::filesystem::directory_iterator(files.directory),
                      std::filesystem::directory_iterator{}) == 2);
}

// --- MixNode: the fan-in policy the graph refuses to have ---

TEST_CASE("a mixer sums its inputs sample for sample") {
  Graph g;
  const auto a = place<ConstAudioNode>(g, 0.125f, 1);
  const auto b = place<ConstAudioNode>(g, 0.25f, 1);
  const auto c = place<ConstAudioNode>(g, 0.5f, 1);
  const auto mix = place<MixNode>(g, 3, 1);
  const auto sink = place<CaptureSinkNode>(g, 1);
  g.connect(a.id, a->out(), mix.id, mix->in(0));
  g.connect(b.id, b->out(), mix.id, mix->in(1));
  g.connect(c.id, c->out(), mix.id, mix->in(2));
  g.connect(mix.id, mix->out(), sink.id, sink->in());

  Engine e(g, 48000.0, 128);
  render(e, 512, 128);
  // Every level is a negative power of two, so the sum is exact in binary.
  for (float s : sink->samples()) CHECK(s == 0.875f);
}

TEST_CASE("an unconnected mixer input contributes silence, not garbage") {
  Graph g;
  const auto a = place<ConstAudioNode>(g, 0.25f, 1);
  const auto mix = place<MixNode>(g, 3, 1); // two left dangling
  const auto sink = place<CaptureSinkNode>(g, 1);
  g.connect(a.id, a->out(), mix.id, mix->in(1));
  g.connect(mix.id, mix->out(), sink.id, sink->in());

  Engine e(g, 48000.0, 128);
  render(e, 256, 128);
  for (float s : sink->samples()) CHECK(s == 0.25f);
}

TEST_CASE("a mixer needs at least one input") {
  Graph g;
  CHECK_THROWS_AS(g.addNode<MixNode>(0, 1), std::invalid_argument);
  CHECK(g.nodeCount() == 0); // a node whose constructor threw was never adopted
}

// --- VoiceAllocatorNode: one stream in, N monophonic streams out ---

TEST_CASE("simultaneous notes are handed to different voices") {
  // 1009 is prime, so the chord cannot land on a block boundary by accident.
  constexpr std::uint64_t kAt = 1009;
  Graph g;
  const auto trig = place<TriggerAtNode>(g, 0);
  const auto blk = place<MidiBlockNode>(g, chord({60, 63, 67}, kAt));
  const auto alloc = place<VoiceAllocatorNode>(g, 3);
  const auto log = place<EventLogNode>(g, 3);
  g.connect(trig.id, trig->trigger(), blk.id, blk->trigger());
  g.connect(blk.id, blk->midi(), alloc.id, alloc->midiIn());
  for (std::uint32_t v = 0; v < 3; ++v)
    g.connect(alloc.id, alloc->voice(v), log.id, log->in(v));

  Engine e(g, 48000.0, 256);
  render(e, 8000, 256);

  const auto recs = log->sorted();
  REQUIRE(recs.size() == 6); // three note-ons, three note-offs
  // Each note went to its own voice, at the right frame, and its note-off
  // followed it to the same voice.
  const int expected[] = {60, 63, 67};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto& on = recs[i];
    const auto& off = recs[i + 3];
    CHECK(std::get<0>(on) == kAt);
    CHECK(std::get<1>(on) == i); // voice i, lowest free index first
    CHECK(std::get<2>(on) == 0x90);
    CHECK(std::get<3>(on) == expected[i]);
    CHECK(std::get<0>(off) == kAt + 4000);
    CHECK(std::get<1>(off) == std::get<1>(on)); // note-off found its voice
    CHECK(std::get<3>(off) == std::get<3>(on));
  }
}

TEST_CASE("a note beyond the voice count steals the longest-held voice") {
  // Four notes, two voices, struck at distinct primes so the order the
  // allocator sees them in is fixed by the clock and not by push order.
  Graph g;
  const auto trig = place<TriggerAtNode>(g, 0);
  const auto blk = place<MidiBlockNode>(
      g, sequence({{1009, 60}, {1103, 63}, {1201, 67}, {1301, 70}}));
  const auto alloc = place<VoiceAllocatorNode>(g, 2);
  const auto log = place<EventLogNode>(g, 2);
  g.connect(trig.id, trig->trigger(), blk.id, blk->trigger());
  g.connect(blk.id, blk->midi(), alloc.id, alloc->midiIn());
  for (std::uint32_t v = 0; v < 2; ++v)
    g.connect(alloc.id, alloc->voice(v), log.id, log->in(v));

  Engine e(g, 48000.0, 256);
  render(e, 8000, 256);

  const auto recs = log->sorted();
  // Four note-ons arrive, but only two note-offs: a stolen note is held by
  // nobody by the time its note-off comes, so it is dropped rather than
  // silencing whichever note took its voice.
  CHECK(recs.size() == 6);

  std::vector<std::pair<std::size_t, int>> ons;
  for (const auto& r : recs)
    if (std::get<2>(r) == 0x90) ons.push_back({std::get<1>(r), std::get<3>(r)});
  REQUIRE(ons.size() == 4);
  CHECK(ons[0] == std::pair<std::size_t, int>{0, 60}); // free voice
  CHECK(ons[1] == std::pair<std::size_t, int>{1, 63}); // free voice
  CHECK(ons[2] == std::pair<std::size_t, int>{0, 67}); // steals 60, held longest
  CHECK(ons[3] == std::pair<std::size_t, int>{1, 70}); // steals 63, now oldest
}

TEST_CASE("voice allocation is independent of block size") {
  // The kernel invariant, extended to a stateful user-space node: 331 is
  // prime and shares no structure with 256 or with the note offsets.
  auto allocateWith = [](std::uint32_t block) {
    Graph g;
    const auto trig = place<TriggerAtNode>(g, 0);
    auto clip = chord({60, 63, 67}, 1009);
    for (auto& entry : chord({55, 59, 62}, 6007)) clip.push_back(entry);
    const auto blk = place<MidiBlockNode>(g, std::move(clip));
    const auto alloc = place<VoiceAllocatorNode>(g, 3);
    const auto log = place<EventLogNode>(g, 3);
    g.connect(trig.id, trig->trigger(), blk.id, blk->trigger());
    g.connect(blk.id, blk->midi(), alloc.id, alloc->midiIn());
    for (std::uint32_t v = 0; v < 3; ++v)
      g.connect(alloc.id, alloc->voice(v), log.id, log->in(v));
    Engine e(g, 48000.0, block);
    render(e, 16000, block);
    return log->sorted();
  };
  const auto a = allocateWith(256);
  REQUIRE(a.size() == 12);
  CHECK(allocateWith(331) == a);
}

TEST_CASE("a voice allocator needs at least one voice") {
  Graph g;
  CHECK_THROWS_AS(g.addNode<VoiceAllocatorNode>(0), std::invalid_argument);
}

TEST_CASE("voice growth rejects identity exhaustion before changing its declared outputs") {
  Graph graph(4294967291ULL);
  const auto allocator = place<VoiceAllocatorNode>(graph, 2);
  CHECK_THROWS_AS(allocator->setVoiceCount(graph, allocator.id, 4), std::invalid_argument);
  CHECK(allocator->voiceCount() == 2);
  allocator->setVoiceCount(graph, allocator.id, 3);
  CHECK(allocator->voice(2) == 4294967295u);
  Engine engine(graph, 48000, 13);
  CHECK_NOTHROW(engine.process(13));
}

TEST_CASE("editing voice count preserves surviving wires and never reuses deleted ports") {
  Graph graph;
  const auto allocator = place<VoiceAllocatorNode>(graph, 2);
  const auto synth = place<SineSynthNode>(graph);
  const auto first = allocator->voice(0), second = allocator->voice(1);
  graph.connect(allocator.id, second, synth.id, synth->midiIn());
  CHECK_THROWS_AS(allocator->setVoiceCount(graph, allocator.id, 1), std::invalid_argument);
  CHECK(allocator->voiceCount() == 2);
  graph.disconnect(synth.id, synth->midiIn());
  allocator->setVoiceCount(graph, allocator.id, 1);
  allocator->setVoiceCount(graph, allocator.id, 3);
  CHECK(allocator->voice(0) == first);
  CHECK(allocator->voice(1) > second);
  CHECK(allocator->voiceCount() == 3);
  graph.connect(allocator.id, allocator->voice(2), synth.id, synth->midiIn());
  CHECK_NOTHROW(Engine(graph, 48000, 127));
}

TEST_CASE("invalid stored note numbers are rejected before playback") {
  Graph graph;
  CHECK_THROWS_AS(graph.addNode<MidiBlockNode>(
      std::vector<MidiBlockNode::Entry>{{0, MidiMsg{0x90, 128, 100}}}), std::invalid_argument);
}

TEST_CASE("removing a middle allocator output preserves sound through the surviving voices") {
  const auto renderAfterEdit = [](bool removeMiddle, std::uint32_t block) {
    Graph graph;
    const auto start = place<TriggerAtNode>(graph, 101);
    const auto midi = place<MidiBlockNode>(graph, chord({60, 64, 67}, 0));
    const auto allocator = place<VoiceAllocatorNode>(graph, removeMiddle ? 3 : 2);
    if (removeMiddle) graph.removePort(allocator.id, allocator->voice(1));
    const auto mix = place<MixNode>(graph, 2, 1);
    const auto sink = place<CaptureSinkNode>(graph, 1);
    graph.connect(start.id, start->trigger(), midi.id, midi->trigger());
    graph.connect(midi.id, midi->midi(), allocator.id, allocator->midiIn());
    for (std::uint32_t voice = 0; voice < 2; ++voice) {
      const auto synth = place<SineSynthNode>(graph);
      graph.connect(allocator.id, allocator->voice(voice), synth.id, synth->midiIn());
      graph.connect(synth.id, synth->out(), mix.id, mix->in(voice));
    }
    graph.connect(mix.id, mix->out(), sink.id, sink->in());
    Engine engine(graph, 48000, block);
    render(engine, 5003, block);
    return sink->samples();
  };
  const auto reference = renderAfterEdit(false, 127);
  REQUIRE(std::any_of(reference.begin(), reference.end(), [](float s) { return s != 0; }));
  CHECK(renderAfterEdit(true, 127) == reference);
  CHECK(renderAfterEdit(true, 331) == reference);
}
