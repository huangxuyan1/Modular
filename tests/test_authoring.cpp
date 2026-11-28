#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <chrono>
#include <thread>
#include <sstream>
#include <fstream>
#include <iterator>
#include <tuple>

#include "modular/Engine.h"
#include "modular/Workspace.h"
#include "modular/GraphExecution.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/GraphPersistence.h"
#include "modular/Supernode.h"
#include "modular/nodes/ManualTriggerNode.h"
#include "modular/nodes/PrimitivePersistence.h"
#include "modular/nodes/MidiFileNode.h"
#include "modular/nodes/WavRecorderNode.h"
#include "modular/nodes/SoundFontNode.h"
#include "SoundFontTestFiles.h"
#include "WavTestFiles.h"
#ifdef _WIN32
#include "modular/nodes/AudioOutputNode.h"
#endif

using namespace modular;
using namespace modular::nodes;

namespace {
// Test-owned unsigned 8-bit PCM, retained separately from the playback cursor.
// The shared container sees only the codec's opaque bytes.
class StoredAudio : public Node {
public:
  StoredAudio(Graph& graph, std::vector<std::byte> samples)
      : Node(graph), samples_(std::move(samples)) {
    output = addOutput(PortType::audio(1), "out");
  }
  const std::vector<std::byte>& content() const { return samples_; }
  void prepare(const PrepareInfo&) override { cursor_ = 0; }
  void process(ProcessContext& ctx) override {
    auto* outputSamples = ctx.audioOut(output).channels[0];
    for (std::uint32_t f = 0; f < ctx.numFrames(); ++f)
      outputSamples[f] = cursor_ < samples_.size()
          ? (std::to_integer<int>(samples_[cursor_++]) - 128) / 128.0f : 0;
  }
  PortId output;
private:
  std::vector<std::byte> samples_;
  std::size_t cursor_ = 0;
};
}

namespace {
struct GraphFiles {
  std::filesystem::path directory = std::filesystem::temp_directory_path() /
      ("modular-persistence-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  GraphFiles() { std::filesystem::create_directory(directory); }
  ~GraphFiles() {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }
};
}

TEST_CASE("the shared container preserves a nodes stored audio independently of its cursor") {
  const NodeCodec codec{"test.stored-audio/1",
      [](const Node& n) { return typeid(n) == typeid(StoredAudio); },
      [](const Node& n) { return NodeContent{"", static_cast<const StoredAudio&>(n).content()}; },
      [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
        return g.restoreNode<StoredAudio>(id, ports, content.stored);
      }};
  std::vector<NodeCodec> codecs(primitiveCodecs().begin(), primitiveCodecs().end());
  codecs.push_back(codec);
  Workspace authored;
  const auto id = authored.graph().addNode<StoredAudio>(std::vector<std::byte>{
      std::byte{0}, std::byte{64}, std::byte{128}, std::byte{192}, std::byte{255}});
  std::stringstream saved;
  {
    Engine engine(authored.graph(), 48000, 3);
    engine.process(3);
    saveGraph(authored, saved, codecs);
  }
  const auto serialized = saved.str();
  for (const std::uint32_t block : {3u, 17u}) {
    std::istringstream file(serialized);
    auto reopened = openGraph(file, codecs);
    auto& graph = reopened.graph();
    const auto sinkId = graph.addNode<CaptureSinkNode>(1);
    auto& sink = static_cast<CaptureSinkNode&>(graph.node(sinkId));
    graph.connect(id, static_cast<StoredAudio&>(graph.node(id)).output, sinkId, sink.in());
    Engine engine(graph, 48000, block);
    while (engine.streamTime() < 7)
      engine.process(static_cast<std::uint32_t>(
          std::min<std::uint64_t>(block, 7 - engine.streamTime())));
    CHECK(sink.samples() == std::vector<float>{-1, -0.5f, 0, 0.5f, 127 / 128.0f, 0, 0});
  }
}

TEST_CASE("shared file save replaces complete graphs at Unicode paths and retains files on failure") {
  GraphFiles files;
  const auto path = files.directory / std::filesystem::path(u8"作曲-é.modular");
  Workspace workspace;
  const auto id = workspace.graph().addNode<ConstFloatNode>(0.25f);
  saveGraphFile(workspace, path, primitiveCodecs());
  auto loaded = openGraphFile(path, primitiveCodecs());
  CHECK(static_cast<ConstFloatNode&>(loaded.graph().node(id)).constant() == 0.25f);
  static_cast<ConstFloatNode&>(workspace.graph().node(id)).setConstant(0.5f);
  saveGraphFile(workspace, path, primitiveCodecs());
  loaded = openGraphFile(path, primitiveCodecs());
  CHECK(static_cast<ConstFloatNode&>(loaded.graph().node(id)).constant() == 0.5f);
  workspace.graph().addNode<TriggerAtNode>(0);
  CHECK_THROWS_AS(saveGraphFile(workspace, path, primitiveCodecs()), std::invalid_argument);
  loaded = openGraphFile(path, primitiveCodecs());
  CHECK(loaded.graph().nodeCount() == 1);
  CHECK_THROWS_AS(openGraphFile(files.directory / "missing", primitiveCodecs()),
                  std::invalid_argument);
  CHECK_THROWS_AS(saveGraphFile(loaded, files.directory, primitiveCodecs()),
                  std::invalid_argument);
  CHECK(std::distance(std::filesystem::directory_iterator(files.directory),
                       std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("open rejects corrupt identities ports settings content and references without replacement") {
  const std::string valid =
      "MODULAR_GRAPH 1\nNEXT 99\nNODES 2\n"
      "NODE 2 \"modular.const-float/1\"\nSETTINGS \"0.5\"\nCONTENT \"\"\n"
      "POSITION 0 0\nINPUTS 0\nOUTPUTS 1\nPORT 1 1 3 \"value\"\n"
      "NODE 6 \"modular.gain/1\"\nSETTINGS \"1\"\nCONTENT \"\"\n"
      "POSITION -17 31\nINPUTS 2\nPORT 3 0 1 \"in\"\nPORT 4 1 3 \"gain\"\n"
      "OUTPUTS 1\nPORT 5 0 1 \"out\"\nWIRES 1\nWIRE 2 1 6 4\nEND\n";
  std::istringstream original(valid);
  auto current = openGraph(original, primitiveCodecs());
  for (const auto& [before, after] : std::vector<std::pair<std::string, std::string>>{
       {"MODULAR_GRAPH 1", "MODULAR_GRAPH 3"},
       {"modular.const-float/1", "unknown/1"},
       {"NEXT 99", "NEXT 6"}, {"NEXT 99", "NEXT 0"},
       {"NEXT 99", "NEXT 4294967297"}, {"NODES 2", "NODES -1"},
       {"NODE 6", "NODE 2"}, {"PORT 3 0 1", "PORT 1 0 1"},
       {"PORT 3 0 1", "PORT 6 0 1"}, {"PORT 1 1 3", "PORT 0 1 1"},
       {"PORT 1 1 3", "PORT 1 7 1"}, {"PORT 1 1 3", "PORT 1 0 1"},
       {"INPUTS 2\nPORT 3 0 1 \"in\"", "INPUTS 1"},
       {"SETTINGS \"0.5\"", "SETTINGS \"nan\""},
       {"SETTINGS \"1\"", "SETTINGS \"0\""},
       {"SETTINGS \"1\"", "SETTINGS \"1 junk\""},
       {"CONTENT \"\"", "CONTENT \"01\""}, {"CONTENT \"\"", "CONTENT \"0z\""},
       {"CONTENT \"\"", "CONTENT \"0\""}, {"POSITION 0 0", "POSITION nan 0"},
       {"WIRE 2 1 6 4", "WIRE 2 1 66 4"}, {"WIRE 2 1 6 4", "WIRE 2 1 6 3"},
       {"WIRES 1", "WIRES 2\nWIRE 2 1 6 4"}, {"END", "END junk"}, {"END", ""}}) {
    auto corrupt = valid;
    const auto at = corrupt.find(before);
    REQUIRE(at != std::string::npos);
    corrupt.replace(at, before.size(), after);
    std::istringstream input(corrupt);
    INFO(before, " -> ", after);
    CHECK_THROWS_AS(current = openGraph(input, primitiveCodecs()), std::invalid_argument);
    CHECK(current.graph().nodeCount() == 2);
    CHECK(static_cast<ConstFloatNode&>(current.graph().node(2)).constant() == 0.5f);
  }
  const auto next = current.graph().addNode<ConstFloatNode>(1.0f);
  CHECK(next == 100);
}

TEST_CASE("save rejects unsupported and nested content without emitting a partial graph") {
  Workspace workspace;
  workspace.graph().addNode<ConstFloatNode>(1.0f);
  workspace.graph().addNode<TriggerAtNode>(0);
  std::ostringstream output;
  CHECK_THROWS_AS(saveGraph(workspace, output, primitiveCodecs()), std::invalid_argument);
  CHECK(output.str().empty());
  Workspace nested;
  nested.graph().addNode<Supernode>();
  CHECK_THROWS_AS(saveGraph(nested, output, primitiveCodecs()), std::invalid_argument);
  CHECK(output.str().empty());
  Workspace inner;
  inner.graph().addNode<MixNode>(2, 1);
  CHECK_NOTHROW(saveGraph(inner, output, primitiveCodecs()));
}

TEST_CASE("an exhausted saved identity source never wraps or reuses a deleted identity") {
  std::istringstream input("MODULAR_GRAPH 1\nNEXT 4294967296\nNODES 0\nWIRES 0\nEND\n");
  auto loaded = openGraph(input, primitiveCodecs());
  CHECK_THROWS_AS(loaded.graph().addNode<ConstFloatNode>(1.0f), std::invalid_argument);
  CHECK(loaded.graph().nodeCount() == 0);
}

TEST_CASE("mixer growth from a nearly exhausted saved graph fails before changing its configuration") {
  std::istringstream input(
      "MODULAR_GRAPH 1\nNEXT 4294967295\nNODES 1\n"
      "NODE 4 \"modular.mix/1\"\nSETTINGS \"1\"\nCONTENT \"\"\nPOSITION 48 50\n"
      "INPUTS 2\nPORT 1 0 1 \"in0\"\nPORT 2 0 1 \"in1\"\n"
      "OUTPUTS 1\nPORT 3 0 1 \"out\"\nWIRES 0\nEND\n");
  auto loaded = openGraph(input, primitiveCodecs());
  auto& mix = static_cast<MixNode&>(loaded.graph().node(4));
  CHECK_THROWS_AS(mix.configure(loaded.graph(), 4, 4, 2), std::invalid_argument);
  CHECK(mix.inputCount() == 2);
  CHECK(mix.channels() == 1);
  for (const auto& port : mix.inputs()) CHECK(port.type == PortType::audio(1));
  CHECK(mix.outputs()[0].type == PortType::audio(1));
  // A smaller valid edit still gets the last available identity, proving the
  // rejected request did not consume identities or partially change port types.
  mix.configure(loaded.graph(), 4, 3, 1);
  CHECK(mix.in(2) == std::numeric_limits<PortId>::max());
  for (const auto& port : mix.inputs()) CHECK(port.type == PortType::audio(1));
  CHECK(mix.outputs()[0].type == PortType::audio(1));
  std::stringstream saved;
  CHECK_NOTHROW(saveGraph(loaded, saved, primitiveCodecs()));
  CHECK_NOTHROW(openGraph(saved, primitiveCodecs()));
}

TEST_CASE("a client can run and release a graph on a worker before editing and running again") {
  Workspace workspace;
  auto& graph = workspace.graph();
  const auto gain = graph.addNode<GainNode>(1);
  auto& node = static_cast<GainNode&>(graph.node(gain));
  GraphExecution execution;
  const auto waitFor = [&](GraphExecution::State expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (execution.state() != expected && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(execution.state() == expected);
  };
  for (int run = 0; run < 2; ++run) {
    execution.start(graph, 48000, 127);
    waitFor(GraphExecution::State::Running);
    workspace.setPosition(gain, {101, 173});
    execution.stop();
    waitFor(GraphExecution::State::Idle);
    node.setChannels(graph, gain, 2);
  }
  graph.connect(gain, node.out(), gain, node.in());
  execution.start(graph, 48000, 127);
  waitFor(GraphExecution::State::Failed);
  CHECK(execution.errorMessage().find("cycle") != std::string::npos);
  graph.disconnect(gain, node.in());
  execution.start(graph, 48000, 127);
  waitFor(GraphExecution::State::Running);
  execution.stop();
  waitFor(GraphExecution::State::Idle);
}

namespace {
class FailedBoundary : public Node {
public:
  explicit FailedBoundary(Graph& graph) : Node(graph) {}
  void process(ProcessContext&) override { failed_ = true; }
  std::string errorMessage() const override { return failed_ ? "boundary: unavailable" : ""; }
private:
  bool failed_ = false;
};
}

TEST_CASE("a worker reports a processing boundary failure and releases execution") {
  Graph graph;
  graph.addNode<FailedBoundary>();
  GraphExecution execution;
  execution.start(graph);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (execution.active() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(execution.state() == GraphExecution::State::Failed);
  CHECK(execution.errorMessage() == "boundary: unavailable");
}

namespace {
class SteadySignal : public Node {
public:
  SteadySignal(Graph& graph, std::uint32_t channels) : Node(graph) {
    output_ = addOutput(PortType::audio(channels), "out");
  }
  PortId out() const { return output_; }
  void process(ProcessContext& ctx) override {
    const auto output = ctx.audioOut(output_);
    for (std::uint32_t c = 0; c < output.numChannels; ++c)
      for (std::uint32_t f = 0; f < ctx.numFrames(); ++f) output.channels[c][f] = 0.25f;
  }
private:
  PortId output_ = kNoPort;
};

class ValueCapture : public Node {
public:
  explicit ValueCapture(Graph& graph) : Node(graph) {
    input_ = addInput(PortType::value(subtype::Float), "in");
  }
  PortId in() const { return input_; }
  float observed = 0;
  void process(ProcessContext& ctx) override {
    observed = ctx.valueIn(input_).valueAt<float>(0);
  }
private:
  PortId input_ = kNoPort;
};
} // namespace

namespace {
class MidiLog : public Node {
public:
  using Record = std::tuple<std::uint64_t, int, int, int>;
  explicit MidiLog(Graph& graph) : Node(graph) {
    input = addInput(PortType::event(subtype::Midi), "in");
  }
  void process(ProcessContext& ctx) override {
    for (const auto& event : ctx.eventsIn(input)) {
      const auto message = event.as<MidiMsg>();
      records.emplace_back(ctx.blockStartFrame() + event.offset,
                           message.status, message.data1, message.data2);
    }
  }
  PortId input;
  std::vector<Record> records;
};
}

TEST_CASE("saved MIDI file content plays independently of its removed import source") {
  GraphFiles files;
  const auto imported = files.directory / "import.mid";
  std::filesystem::copy_file(std::filesystem::path(MODULAR_TEST_FIXTURES) / "one-note.mid",
                             imported);
  Workspace authored;
  const auto id = authored.graph().addNode<MidiFileNode>(imported);
  const auto saved = files.directory / "owned.modular";
  saveGraphFile(authored, saved, primitiveCodecs());
  REQUIRE(std::filesystem::remove(imported));
  for (const auto rate : {8000.0, 44100.0}) {
    for (const std::uint32_t block : {17u, 127u, 512u}) {
      auto restored = openGraphFile(saved, primitiveCodecs());
      auto& graph = restored.graph();
      auto& midi = static_cast<MidiFileNode&>(graph.node(id));
      REQUIRE(midi.ready());
      const auto start = graph.addNode<TriggerAtNode>(101);
      const auto logId = graph.addNode<MidiLog>();
      auto& log = static_cast<MidiLog&>(graph.node(logId));
      graph.connect(start, static_cast<TriggerAtNode&>(graph.node(start)).trigger(),
                    id, midi.trigger());
      graph.connect(id, midi.midi(), logId, log.input);
      Engine engine(graph, rate, block);
      const auto horizon = static_cast<std::uint64_t>(rate) + 137;
      while (engine.streamTime() < horizon)
        engine.process(static_cast<std::uint32_t>(
            std::min<std::uint64_t>(block, horizon - engine.streamTime())));
      CHECK(log.records == std::vector<MidiLog::Record>{
          {101, 0x90, 60, 100},
          {101 + static_cast<std::uint64_t>(rate / 2), 0x90, 60, 0}});
    }
  }
}

namespace {
struct PlaybackGraph {
  Workspace workspace;
  NodeId start, stop, midi, allocator, mix, gain;
  explicit PlaybackGraph(std::optional<MidiFile> file = {}) {
    auto& graph = workspace.graph();
    start = graph.addNode<ManualTriggerNode>();
    stop = graph.addNode<ManualTriggerNode>(ManualTriggerNode::Label::Stop);
    midi = file ? graph.addNode<MidiFileNode>(std::move(*file))
        : graph.addNode<MidiBlockNode>(std::vector<MidiBlockNode::Entry>{
        {7, {0x90, 60, 100}}, {11, {0xb3, 7, 90}}, {13, {0x91, 64, 80}},
        {181, {0x80, 60, 0}}, {233, {0x81, 64, 0}}});
    allocator = graph.addNode<VoiceAllocatorNode>(3);
    auto& voices = static_cast<VoiceAllocatorNode&>(graph.node(allocator));
    graph.removePort(allocator, voices.voice(1)); // retain private IDs across a gap
    mix = graph.addNode<MixNode>(2, 1);
    auto& mixer = static_cast<MixNode&>(graph.node(mix));
    for (std::uint32_t i = 0; i < 2; ++i) {
      const auto synth = graph.addNode<SineSynthNode>();
      auto& node = static_cast<SineSynthNode&>(graph.node(synth));
      graph.connect(allocator, voices.voice(i), synth, node.midiIn());
      graph.connect(synth, node.out(), mix, mixer.in(i));
    }
    auto& block = static_cast<MidiBlockNode&>(graph.node(midi));
    graph.connect(start, static_cast<ManualTriggerNode&>(graph.node(start)).trigger(),
                  midi, block.trigger());
    graph.connect(stop, static_cast<ManualTriggerNode&>(graph.node(stop)).trigger(),
                  midi, block.stop());
    graph.connect(midi, block.midi(), allocator, voices.midiIn());
    gain = graph.addNode<GainNode>(1);
    auto& level = static_cast<GainNode&>(graph.node(gain));
    const auto constant = graph.addNode<ConstFloatNode>(0.375f);
    graph.connect(constant, static_cast<ConstFloatNode&>(graph.node(constant)).value(),
                  gain, level.gain());
    graph.connect(mix, mixer.out(), gain, level.in());
    workspace.setPosition(midi, {137.25, -81.5});
    const auto deleted = graph.addNode<ManualTriggerNode>();
    graph.removeNode(deleted);
  }
};

struct PlaybackResult {
  std::vector<float> samples;
  std::vector<MidiLog::Record> events;
  friend bool operator==(const PlaybackResult&, const PlaybackResult&) = default;
};

PlaybackResult renderPlayback(Workspace& workspace, const PlaybackGraph& ids,
                              std::uint32_t block, bool signal = true) {
  auto& graph = workspace.graph();
  const auto sinkId = graph.addNode<CaptureSinkNode>(1);
  auto& sink = static_cast<CaptureSinkNode&>(graph.node(sinkId));
  graph.connect(ids.gain, static_cast<GainNode&>(graph.node(ids.gain)).out(),
                sinkId, sink.in());
  const auto logId = graph.addNode<MidiLog>();
  auto& log = static_cast<MidiLog&>(graph.node(logId));
  graph.connect(ids.midi, static_cast<MidiBlockNode&>(graph.node(ids.midi)).midi(),
                logId, log.input);
  Engine engine(graph, 48000, block);
  // Explicit input frames are independent of dispatch size, including restart
  // while notes are held and Stop before the sequence's natural completion.
  for (const std::uint64_t boundary : {101u, 149u, 317u, 701u}) {
    while (engine.streamTime() < boundary)
      engine.process(static_cast<std::uint32_t>(
          std::min<std::uint64_t>(block, boundary - engine.streamTime())));
    if (signal && boundary != 701)
      static_cast<ManualTriggerNode&>(graph.node(
          boundary == 317 ? ids.stop : ids.start)).press();
  }
  return {sink.samples(), log.records};
}
}

TEST_CASE("a reopened playback graph preserves sound identities and configuration but no activity") {
  PlaybackGraph authored;
  auto& graph = authored.workspace.graph();
  std::stringstream before;
  saveGraph(authored.workspace, before, primitiveCodecs());
  std::stringstream active;
  {
    Engine engine(graph, 48000, 17);
    static_cast<ManualTriggerNode&>(graph.node(authored.start)).press();
    engine.process(17); // both voices sounding, with scheduled events still ahead
    static_cast<ManualTriggerNode&>(graph.node(authored.stop)).press(); // pending control
    saveGraph(authored.workspace, active, primitiveCodecs());
  }
  CHECK(active.str() == before.str());
  auto idle = openGraph(active, primitiveCodecs());
  CHECK(idle.graph().nodeIds() == graph.nodeIds());
  CHECK(idle.graph().nextIdentity() == graph.nextIdentity());
  CHECK(idle.position(authored.midi) == NodePosition{137.25, -81.5});
  std::stringstream resaved;
  saveGraph(idle, resaved, primitiveCodecs());
  CHECK(resaved.str() == before.str()); // includes ordered wires, names and port IDs
  auto& voices = static_cast<VoiceAllocatorNode&>(idle.graph().node(authored.allocator));
  CHECK(voices.voiceCount() == 2);
  CHECK(static_cast<ManualTriggerNode&>(idle.graph().node(authored.stop)).label() ==
        ManualTriggerNode::Label::Stop);
  const auto silent = renderPlayback(idle, authored, 127, false);
  CHECK(silent.events.empty());
  CHECK(std::all_of(silent.samples.begin(), silent.samples.end(),
                    [](float sample) { return sample == 0; }));

  PlaybackGraph reference;
  const auto expected = renderPlayback(reference.workspace, reference, 17);
  REQUIRE(!expected.events.empty());
  REQUIRE(std::any_of(expected.samples.begin(), expected.samples.end(),
                      [](float sample) { return sample != 0; }));
  for (const std::uint32_t block : {17u, 127u, 512u}) {
    std::istringstream saved(before.str());
    auto reopened = openGraph(saved, primitiveCodecs());
    CHECK(renderPlayback(reopened, authored, block) == expected);
  }
}

TEST_CASE("saved parsed MIDI preserves tempo timing channel messages tails and sample-rate preparation") {
  // Format 1, 500 PPQ: two simultaneous notes, all channel-message shapes,
  // running status, skipped SysEx and a tempo change at tick 3.
  const std::vector<std::uint8_t> bytes{
      'M','T','h','d', 0,0,0,6, 0,1, 0,2, 1,244,
      'M','T','r','k', 0,0,0,18,
      0,255,81,3,7,161,32, 3,255,81,3,15,66,64, 7,255,47,0,
      'M','T','r','k', 0,0,0,36,
      1,0x90,60,100, 0,0x91,64,80, 0,0xc3,9, 0,10,
      0,0xd3,12, 0,0xb3,7,90, 0,0xa3,60,45, 0,0xe3,1,64,
      2,0xf0,1,0xf7, 7,0xff,0x2f,0};
  const auto file = MidiFile::parse(bytes);
  CHECK(file.skippedSysExEvents() == 1);
  CHECK(file.durationSeconds() == doctest::Approx(0.017));
  PlaybackGraph authored(file);
  std::stringstream before;
  saveGraph(authored.workspace, before, primitiveCodecs());
  for (const auto rate : {44100.0, 96000.0}) {
    std::istringstream input(before.str());
    auto running = openGraph(input, primitiveCodecs());
    {
      Engine engine(running.graph(), rate, 127);
      static_cast<ManualTriggerNode&>(running.graph().node(authored.start)).press();
      engine.process(127);
      std::stringstream active;
      saveGraph(running, active, primitiveCodecs());
      CHECK(active.str() == before.str());
    }
  }
  PlaybackGraph reference(file);
  const auto expected = renderPlayback(reference.workspace, reference, 17);
  REQUIRE(expected.events.size() > 8);
  // Start at 149 replaces the earlier sequence's event due at that instant.
  CHECK(expected.events.front() == MidiLog::Record{197, 0x90, 60, 100});
  for (const auto block : {17u, 127u, 512u}) {
    std::istringstream input(before.str());
    auto reopened = openGraph(input, primitiveCodecs());
    CHECK(renderPlayback(reopened, authored, block) == expected);
  }
}

namespace {
std::string contentHex(const std::string& text) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string result;
  for (const unsigned char c : text) {
    result += kDigits[c >> 4];
    result += kDigits[c & 15];
  }
  return result;
}

// A saved graph holding one MIDI block or MIDI file node, id 4, with the given content.
std::string midiNodeGraph(const std::string& kind, const std::string& settings,
                          const std::string& content) {
  return "MODULAR_GRAPH 1\nNEXT 5\nNODES 1\nNODE 4 \"modular." + kind +
      "/1\"\nSETTINGS \"" + settings + "\"\nCONTENT \"" + contentHex(content) +
      "\"\nPOSITION 0 0\nINPUTS 2\nPORT 1 2 1 \"start\"\n"
      "PORT 2 2 1 \"stop\"\nOUTPUTS 1\nPORT 3 2 2 \"midi\"\nWIRES 0\nEND\n";
}
}

TEST_CASE("malformed stored MIDI is rejected without replacing the current authored graph") {
  Workspace current;
  const auto id = current.graph().addNode<ConstFloatNode>(0.625f);
  for (const auto& content : {
       "-1 0 0", "nan 0 0", "inf 0 0", "1 -1 0", "1 0 -1",
       "1 0 999999999999", "1 0 1", "1 0 0 junk",
       "1 0 1 nan 144 60 100", "1 0 1 -0.1 144 60 100", "1 0 1 2 144 60 100",
       "1 0 2 0.5 144 60 100 0.25 128 60 0",
       "1 0 1 0 256 60 100", "1 0 1 0 240 60 100", "1 0 1 0 127 60 100",
       "1 0 1 0 144 128 100", "1 0 1 0 144 60 128", "1 0 1 0 192 9 1"}) {
    INFO(content);
    std::istringstream input(midiNodeGraph("midi-file", "loaded", content));
    CHECK_THROWS_AS(current = openGraph(input, primitiveCodecs()), std::invalid_argument);
    CHECK(current.graph().nodeIds() == std::vector<NodeId>{id});
    CHECK(static_cast<ConstFloatNode&>(current.graph().node(id)).constant() == 0.625f);
  }
  for (const auto& content : {"-1", "99999999999", "1 -1 144 60 100",
       "1 18446744073709551616 144 60 100", "2 7 144 60 100 6 128 60 0",
       "1 7 144 128 100", "0 junk"}) {
    std::istringstream input(midiNodeGraph("midi-block", "", content));
    CHECK_THROWS_AS(current = openGraph(input, primitiveCodecs()), std::invalid_argument);
    CHECK(current.graph().nodeIds() == std::vector<NodeId>{id});
  }
  for (const auto& settings : {"empty", "missing", "loaded/2"}) {
    std::istringstream input(midiNodeGraph("midi-file", settings, "1 0 0"));
    CHECK_THROWS_AS(openGraph(input, primitiveCodecs()), std::invalid_argument);
  }
}

TEST_CASE("stored MIDI file times keep the digits that recover each double as earlier saves wrote them") {
  // Q:1/4=90 (666,667 us per quarter) at 480 ticks per quarter, a note from tick 40 to
  // tick 15,000: neither time is a short decimal. The expected text is what Save wrote
  // while MIDI times were long doubles, MSVC's 64-bit ones, so a graph saved then
  // reopens to the same times and saves the same bytes.
  const std::vector<std::uint8_t> bytes{
      'M','T','h','d', 0,0,0,6, 0,0, 0,1, 1,224,
      'M','T','r','k', 0,0,0,20,
      0,0xff,0x51,3,0x0a,0x2c,0x2b, 40,0x90,60,100, 0xf4,0x70,0x80,60,0, 0,0xff,0x2f,0};
  const auto file = MidiFile::parse(bytes);
  Workspace authored;
  authored.graph().addNode<MidiFileNode>(file);
  const std::string times = "20.833343750000001 0 2\n0.055555583333333332 144 60 100\n"
      "20.833343750000001 128 60 0\n";
  const auto earlier = midiNodeGraph("midi-file", "loaded", times);
  // Earlier saves wrote container version 1. Save now writes version 2, whose
  // port records differ, and the same CONTENT.
  const auto written =
      "MODULAR_GRAPH 2\nNEXT 5\nNODES 1\nNODE 4 \"modular.midi-file/1\"\n"
      "SETTINGS \"loaded\"\nCONTENT \"" + contentHex(times) + "\"\nPOSITION 0 0\n"
      "INPUTS 2\nPORT 1 2 1 0 \"start\"\nPORT 2 2 1 0 \"stop\"\n"
      "OUTPUTS 1\nPORT 3 2 2 0 \"midi\"\nWIRES 0\nEND\n";
  std::stringstream saved;
  saveGraph(authored, saved, primitiveCodecs());
  CHECK(saved.str() == written);
  std::istringstream input(earlier);
  const auto reopened = openGraph(input, primitiveCodecs());
  const auto& reopenedFile =
      static_cast<const MidiFileNode&>(reopened.graph().node(4)).file();
  CHECK(reopenedFile.durationSeconds() == file.durationSeconds());
  REQUIRE(reopenedFile.entries().size() == 2);
  for (std::size_t i = 0; i < 2; ++i)
    CHECK(reopenedFile.entries()[i].seconds == file.entries()[i].seconds);
  std::stringstream resaved;
  saveGraph(reopened, resaved, primitiveCodecs());
  CHECK(resaved.str() == written);
}

TEST_CASE("an unready MIDI import can be reopened and repaired and failed replacement keeps content") {
  Workspace authored;
  const auto id = authored.graph().addNode<MidiFileNode>();
  auto& midi = static_cast<MidiFileNode&>(authored.graph().node(id));
  CHECK_THROWS_AS(midi.import("missing.mid"), std::invalid_argument);
  CHECK(!midi.ready());
  CHECK(!midi.importError().empty());
  std::stringstream saved;
  saveGraph(authored, saved, primitiveCodecs());
  auto repaired = openGraph(saved, primitiveCodecs());
  auto& node = static_cast<MidiFileNode&>(repaired.graph().node(id));
  CHECK(!node.ready());
  CHECK_THROWS_AS(Engine(repaired.graph(), 48000, 17), std::invalid_argument);
  node.import(std::filesystem::path(MODULAR_TEST_FIXTURES) / "one-note.mid");
  CHECK(node.ready());
  std::stringstream before;
  saveGraph(repaired, before, primitiveCodecs());
  CHECK_THROWS_AS(node.import("missing.mid"), std::invalid_argument);
  CHECK(node.ready());
  std::stringstream after;
  saveGraph(repaired, after, primitiveCodecs());
  CHECK(after.str() == before.str());
  CHECK_NOTHROW(Engine(repaired.graph(), 48000, 17));
}

namespace {
// What a client that saves soundfont nodes passes: the primitives' codecs and,
// beside them, the soundfont node's, which primitiveCodecs() leaves out.
std::vector<NodeCodec> soundFontClientCodecs() {
  std::vector<NodeCodec> codecs(primitiveCodecs().begin(), primitiveCodecs().end());
  codecs.insert(codecs.end(), soundFontCodecs().begin(), soundFontCodecs().end());
  return codecs;
}

std::string bytesHex(const sf2test::Bytes& bytes) {
  return contentHex(std::string(bytes.begin(), bytes.end()));
}

// Two presets, each one zone of a 32 kHz sample: a steady level at bank 0
// program 0, the file's lowest preset, and a sine at bank 1 program 5.
sf2test::Bytes twoPresetSoundFont() {
  using namespace sf2test;
  File file;
  std::vector<std::int16_t> sine(1500);
  for (std::size_t i = 0; i < sine.size(); ++i)
    sine[i] = static_cast<std::int16_t>(std::lround(12000 * std::sin(0.05 * i)));
  file.samples.push_back({"steady", std::vector<std::int16_t>(1500, 3000), 32000});
  file.samples.push_back({"sine", std::move(sine), 32000});
  file.instruments.push_back({"steady", {{{set(gen::kSampleId, 0)}}}});
  file.instruments.push_back({"sine", {{{set(gen::kSampleId, 1)}}}});
  file.presets.push_back({"steady", 0, 0, {{{set(gen::kInstrument, 0)}}}});
  file.presets.push_back({"sine", 5, 1, {{{set(gen::kInstrument, 1)}}}});
  return build(file);
}

// Start -> a MIDI block of two overlapping notes -> a soundfont node, empty
// until a test loads it. Every node has a codec, so the graph saves as authored.
struct SoundFontGraph {
  Workspace workspace;
  NodeId start = kNoNode, notes = kNoNode, synth = kNoNode;
  SoundFontGraph() {
    auto& graph = workspace.graph();
    start = graph.addNode<ManualTriggerNode>();
    notes = graph.addNode<MidiBlockNode>(std::vector<MidiBlockNode::Entry>{
        {7, {0x90, 60, 100}}, {13, {0x91, 67, 90}}, {1031, {0x80, 60, 0}},
        {1307, {0x81, 67, 0}}});
    synth = graph.addNode<SoundFontNode>();
    auto& midi = static_cast<MidiBlockNode&>(graph.node(notes));
    graph.connect(start, static_cast<ManualTriggerNode&>(graph.node(start)).trigger(),
                  notes, midi.trigger());
    graph.connect(notes, midi.midi(), synth, synthNode().midiIn());
  }
  SoundFontNode& synthNode() {
    return static_cast<SoundFontNode&>(workspace.graph().node(synth));
  }
};

// Starts the notes on frame 101 at every block size and captures the stereo
// output, interleaved, to frame 3001.
std::vector<float> renderSoundFont(Workspace& workspace, const SoundFontGraph& ids,
                                   double rate, std::uint32_t block) {
  auto& graph = workspace.graph();
  const auto sinkId = graph.addNode<CaptureSinkNode>(2);
  auto& sink = static_cast<CaptureSinkNode&>(graph.node(sinkId));
  graph.connect(ids.synth, static_cast<SoundFontNode&>(graph.node(ids.synth)).out(),
                sinkId, sink.in());
  Engine engine(graph, rate, block);
  const auto runTo = [&](std::uint64_t end) {
    while (engine.streamTime() < end)
      engine.process(static_cast<std::uint32_t>(
          std::min<std::uint64_t>(block, end - engine.streamTime())));
  };
  runTo(101);
  static_cast<ManualTriggerNode&>(graph.node(ids.start)).press();
  runTo(3001);
  return sink.samples();
}

bool sounds(const std::vector<float>& samples) {
  return std::any_of(samples.begin(), samples.end(), [](float s) { return s != 0; });
}
} // namespace

TEST_CASE("a saved soundfont node reopens with its whole file preset and voice limit after the file is deleted") {
  GraphFiles files;
  auto font = twoPresetSoundFont();
  font.insert(font.end(), {7, 0, 9}); // after the RIFF chunk, but part of the file
  const auto source = files.directory / "instrument.sf2";
  std::ofstream(source, std::ios::binary)
      .write(reinterpret_cast<const char*>(font.data()),
             static_cast<std::streamsize>(font.size()));
  const auto author = [](SoundFontNode& node) {
    node.setPreset(1, 5);
    node.setVoiceLimit(1); // the second, overlapping note meets a full pool
  };
  SoundFontGraph authored;
  authored.synthNode().load(source);
  author(authored.synthNode());
  const auto codecs = soundFontClientCodecs();
  const auto saved = files.directory / "instrument.modular";
  saveGraphFile(authored.workspace, saved, codecs);
  REQUIRE(std::filesystem::remove(source));
  std::stringstream text;
  text << std::ifstream(saved, std::ios::binary).rdbuf();
  const auto content = "SETTINGS \"loaded 1 5 1\"\nCONTENT \"" + bytesHex(font) + "\"\n";
  CHECK(text.str().find(content) != std::string::npos);

  for (const double rate : {44100.0, 48000.0}) {
    SoundFontGraph reference;
    reference.synthNode().load(font);
    author(reference.synthNode());
    const auto expected = renderSoundFont(reference.workspace, reference, rate, 17);
    REQUIRE(sounds(expected));
    for (const std::uint32_t block : {1u, 17u, 127u, 512u}) {
      CAPTURE(rate);
      CAPTURE(block);
      auto reopened = openGraphFile(saved, codecs);
      auto& node = static_cast<SoundFontNode&>(reopened.graph().node(authored.synth));
      REQUIRE(node.ready());
      CHECK(node.bank() == 1);
      CHECK(node.program() == 5);
      CHECK(node.voiceLimit() == 1);
      CHECK(renderSoundFont(reopened, authored, rate, block) == expected);
    }
  }
}

TEST_CASE("saving a soundfont node during a run writes only its authored content") {
  SoundFontGraph authored;
  authored.synthNode().load(twoPresetSoundFont());
  authored.synthNode().setPreset(1, 5);
  authored.synthNode().setVoiceLimit(3);
  const auto codecs = soundFontClientCodecs();
  std::stringstream idle;
  saveGraph(authored.workspace, idle, codecs);
  auto& graph = authored.workspace.graph();
  Engine engine(graph, 48000, 127);
  static_cast<ManualTriggerNode&>(graph.node(authored.start)).press();
  engine.process(127); // both notes sounding, with their note-offs still ahead
  std::stringstream running;
  saveGraph(authored.workspace, running, codecs);
  CHECK(running.str() == idle.str());
}

TEST_CASE("an empty soundfont node saves and reopens empty with its voice limit and a load makes it ready") {
  SoundFontGraph authored;
  authored.synthNode().setVoiceLimit(9);
  const auto codecs = soundFontClientCodecs();
  std::stringstream saved;
  saveGraph(authored.workspace, saved, codecs);
  CHECK(saved.str().find("SETTINGS \"empty 9\"\nCONTENT \"\"\n") != std::string::npos);
  auto reopened = openGraph(saved, codecs);
  auto& node = static_cast<SoundFontNode&>(reopened.graph().node(authored.synth));
  CHECK(!node.ready());
  CHECK(node.voiceLimit() == 9);
  CHECK_THROWS_WITH_AS(Engine(reopened.graph(), 48000, 17),
                       "soundfont: load a soundfont first", std::invalid_argument);
  node.load(twoPresetSoundFont());
  CHECK(node.ready());
  CHECK(node.bank() == 0);
  CHECK(node.program() == 0);
  CHECK(node.voiceLimit() == 9);
  CHECK(sounds(renderSoundFont(reopened, authored, 48000, 127)));
}

TEST_CASE("the primitive codecs alone refuse a graph holding a soundfont node") {
  SoundFontGraph authored;
  authored.synthNode().load(twoPresetSoundFont());
  std::stringstream saved;
  saveGraph(authored.workspace, saved, soundFontClientCodecs());
  Workspace current;
  const auto id = current.graph().addNode<ConstFloatNode>(0.625f);
  CHECK_THROWS_WITH_AS(current = openGraph(saved, primitiveCodecs()),
                       "open: unsupported node kind modular.soundfont/1",
                       std::invalid_argument);
  CHECK(current.graph().nodeIds() == std::vector<NodeId>{id});
  std::ostringstream unsaved;
  CHECK_THROWS_WITH_AS(saveGraph(authored.workspace, unsaved, primitiveCodecs()),
                       "save: unsupported node kind; graph was not saved",
                       std::invalid_argument);
  CHECK(unsaved.str().empty());
}

TEST_CASE("corrupt saved soundfont content fails open without replacing the current graph") {
  using sf2test::Bytes;
  const auto encoded = [](const std::string& settings, const Bytes& content,
                          const std::string& out = "PORT 2 0 2 \"out\"") {
    return "MODULAR_GRAPH 1\nNEXT 4\nNODES 1\nNODE 3 \"modular.soundfont/1\"\n"
        "SETTINGS \"" + settings + "\"\nCONTENT \"" + bytesHex(content) + "\"\n"
        "POSITION 0 0\nINPUTS 1\nPORT 1 2 2 \"midi\"\nOUTPUTS 1\n" + out +
        "\nWIRES 0\nEND\n";
  };
  const auto codecs = soundFontClientCodecs();
  const auto font = twoPresetSoundFont();
  for (const auto& [settings, content] : std::vector<std::pair<std::string, Bytes>>{
       {"loaded 1 5 7", font}, {"loaded 0 0 65535", font}, {"empty 1", {}}}) {
    std::istringstream input(encoded(settings, content));
    CHECK_NOTHROW(openGraph(input, codecs));
  }

  auto truncated = font;
  truncated.pop_back();
  auto renamed = font;
  renamed[3] = 'X';
  Workspace current;
  const auto id = current.graph().addNode<ConstFloatNode>(0.625f);
  struct Corrupt {
    std::string settings;
    Bytes content;
    std::string error;
  };
  for (const auto& corrupt : std::vector<Corrupt>{
       // Bytes that no longer parse.
       {"loaded 0 0 256", truncated, "soundfont: truncated chunk"},
       {"loaded 0 0 256", renamed, "soundfont: missing RIFF header"},
       {"loaded 0 0 256", {}, "soundfont: truncated chunk"},
       // A preset the file lacks, and voice limits outside 1 to 65535.
       {"loaded 0 5 256", font, "soundfont: no preset at bank 0 program 5"},
       {"loaded 1 5 0", font, "soundfont: voice limit must be 1 to 65535"},
       {"loaded 1 5 65536", font, "soundfont: voice limit must be 1 to 65535"},
       {"empty 0", {}, "soundfont: voice limit must be 1 to 65535"},
       // Malformed and trailing settings.
       {"", font, "open: invalid soundfont settings"},
       {"loaded", font, "open: invalid soundfont settings"},
       {"loaded 1 5", font, "open: invalid soundfont settings"},
       {"loaded 1 x 256", font, "open: invalid soundfont settings"},
       {"loaded -1 5 256", font, "open: invalid soundfont settings"},
       {"loaded 65536 5 256", font, "open: invalid soundfont settings"},
       {"loaded 1 5 2.5", font, "open: invalid soundfont settings"},
       {"loaded 1 5 4294967296", font, "open: invalid soundfont settings"},
       {"loaded 1 5 256 junk", font, "open: invalid soundfont settings"},
       {"ready 1 5 256", font, "open: invalid soundfont settings"},
       {"empty", {}, "open: invalid soundfont settings"},
       {"empty 0 0 256", {}, "open: invalid soundfont settings"},
       // An empty node carrying content.
       {"empty 256", font, "open: empty soundfont node has content"}}) {
    CAPTURE(corrupt.settings);
    CAPTURE(corrupt.content.size());
    std::istringstream input(encoded(corrupt.settings, corrupt.content));
    CHECK_THROWS_WITH_AS(current = openGraph(input, codecs), corrupt.error.c_str(),
                         std::invalid_argument);
    CHECK(current.graph().nodeIds() == std::vector<NodeId>{id});
    CHECK(static_cast<ConstFloatNode&>(current.graph().node(id)).constant() == 0.625f);
  }
  // Ports other than midi in and audio<2> out.
  for (const auto& out : {"PORT 2 0 1 \"out\"", "PORT 2 2 2 \"out\""}) {
    std::istringstream input(encoded("loaded 1 5 7", font, out));
    CHECK_THROWS_AS(current = openGraph(input, codecs), std::invalid_argument);
    CHECK(current.graph().nodeIds() == std::vector<NodeId>{id});
  }
}

namespace {
// A derived node may own more than its base's codec would save.
class DerivedSoundFontNode : public SoundFontNode {
public:
  using SoundFontNode::SoundFontNode;
};
} // namespace

TEST_CASE("the soundfont codec saves only an exact soundfont node with its own ports") {
  const auto codecs = soundFontClientCodecs();
  std::ostringstream output;
  Workspace derived;
  derived.graph().addNode<DerivedSoundFontNode>();
  CHECK_THROWS_WITH_AS(saveGraph(derived, output, codecs),
                       "save: unsupported node kind; graph was not saved",
                       std::invalid_argument);
  Workspace deaf;
  const auto id = deaf.graph().addNode<SoundFontNode>();
  auto& node = static_cast<SoundFontNode&>(deaf.graph().node(id));
  deaf.graph().removePort(id, node.midiIn());
  CHECK_THROWS_WITH_AS(saveGraph(deaf, output, codecs),
                       "save: unsupported primitive port configuration",
                       std::invalid_argument);
  CHECK(output.str().empty());
}

#ifdef _WIN32
TEST_CASE("saved Windows output settings restore without opening or requiring a device") {
  Workspace authored;
  const auto id = authored.graph().addNode<AudioOutputNode>(2);
  std::stringstream file;
  saveGraph(authored, file, primitiveCodecs());
  auto loaded = openGraph(file, primitiveCodecs());
  auto& output = static_cast<AudioOutputNode&>(loaded.graph().node(id));
  CHECK(output.channels() == 2);
  CHECK(output.inputs()[0].id == authored.graph().node(id).inputs()[0].id);
  CHECK(output.errorMessage().empty());
  output.setChannels(loaded.graph(), id, 1);
  std::stringstream mono;
  saveGraph(loaded, mono, primitiveCodecs());
  auto reopened = openGraph(mono, primitiveCodecs());
  CHECK(static_cast<AudioOutputNode&>(reopened.graph().node(id)).channels() == 1);
}
#endif

TEST_CASE("explicit deletion removes only the selected node and its attached wires") {
  Workspace workspace;
  Graph& graph = workspace.graph();
  const auto constant = graph.addNode<ConstFloatNode>(0.5f);
  const auto gain = graph.addNode<GainNode>(1);
  const auto other = graph.addNode<GainNode>(1);
  const auto mix = graph.addNode<MixNode>(2, 1);
  const auto& source = static_cast<const ConstFloatNode&>(graph.node(constant));
  const auto& level = static_cast<const GainNode&>(graph.node(gain));
  const auto& survivor = static_cast<const GainNode&>(graph.node(other));
  const auto& mixer = static_cast<const MixNode&>(graph.node(mix));
  const auto oldOutput = level.out();
  graph.connect(constant, source.value(), gain, level.gain());
  graph.connect(constant, source.value(), other, survivor.gain());
  graph.connect(gain, level.out(), mix, mixer.in(0));
  graph.connect(gain, level.out(), mix, mixer.in(1));
  workspace.setPosition(gain, {37, 59});
  workspace.setPosition(other, {401, 103});

  CHECK_THROWS_AS(graph.removeNode(gain), std::invalid_argument);
  CHECK_THROWS_AS(graph.removePort(gain, oldOutput), std::invalid_argument);
  workspace.deleteNodeWithConnections(gain);
  CHECK(graph.nodeIds() == std::vector<NodeId>{constant, other, mix});
  REQUIRE(graph.connections().size() == 1);
  CHECK(graph.connections()[0].dstNode == other);
  CHECK(graph.connections()[0].dstPort == survivor.gain());
  CHECK(workspace.position(other) == NodePosition{401, 103});
  CHECK_THROWS_AS(workspace.position(gain), std::invalid_argument);
  CHECK_THROWS_AS(workspace.deleteNodeWithConnections(gain), std::invalid_argument);
  const auto replacement = graph.addNode<GainNode>(1);
  CHECK(replacement > mix);
  CHECK(static_cast<const GainNode&>(graph.node(replacement)).out() > oldOutput);
}

TEST_CASE("editing a wired constant preserves identity and changes its rendered value") {
  Graph graph;
  const auto id = graph.addNode<ConstFloatNode>(0.5f);
  auto& constant = static_cast<ConstFloatNode&>(graph.node(id));
  const auto sinkId = graph.addNode<ValueCapture>();
  auto& sink = static_cast<ValueCapture&>(graph.node(sinkId));
  const auto port = constant.value();
  graph.connect(id, port, sinkId, sink.in());
  constant.setConstant(-0.25f);
  CHECK(constant.constant() == -0.25f);
  CHECK(constant.value() == port);
  CHECK_THROWS_AS(constant.setConstant(std::numeric_limits<float>::infinity()),
                  std::invalid_argument);
  CHECK(constant.constant() == -0.25f);
  Engine engine(graph, 48000, 17);
  engine.process(13);
  CHECK(sink.observed == -0.25f);
}

TEST_CASE("channel edits preserve ports and reject incompatible wires without partial changes") {
  Graph graph;
  const auto id = graph.addNode<GainNode>(1);
  auto& gain = static_cast<GainNode&>(graph.node(id));
  const auto input = gain.in(), output = gain.out(), control = gain.gain();
  const auto constant = graph.addNode<ConstFloatNode>(0.5f);
  graph.connect(constant, static_cast<ConstFloatNode&>(graph.node(constant)).value(),
                id, control);
  gain.setChannels(graph, id, 2);
  CHECK(gain.channels() == 2);
  CHECK(gain.in() == input);
  CHECK(gain.out() == output);
  CHECK(gain.gain() == control);
  CHECK(gain.inputs()[0].type == PortType::audio(2));
  CHECK(gain.outputs()[0].type == PortType::audio(2));
  const auto sinkId = graph.addNode<CaptureSinkNode>(2);
  auto& sink = static_cast<CaptureSinkNode&>(graph.node(sinkId));
  graph.connect(id, output, sinkId, sink.in());
  CHECK_THROWS_AS(gain.setChannels(graph, id, 1), std::invalid_argument);
  CHECK_THROWS_AS(gain.setChannels(graph, id, 0), std::invalid_argument);
  CHECK_THROWS_AS(gain.setChannels(graph, constant, 1), std::invalid_argument);
  CHECK(gain.channels() == 2);
  CHECK(gain.inputs()[0].type == PortType::audio(2));
  CHECK(gain.outputs()[0].type == PortType::audio(2));
  CHECK(graph.connections().size() == 2);
  CHECK_NOTHROW(gain.setChannels(graph, id, 2));
  const auto source = graph.addNode<SteadySignal>(2);
  graph.connect(source, static_cast<const SteadySignal&>(graph.node(source)).out(),
                id, input);
  Engine engine(graph, 48000, 17);
  engine.process(13);
  CHECK(sink.samples() == std::vector<float>(26, 0.125f));
}

TEST_CASE("mixer edits retain surviving ports and reject removal of a wired input atomically") {
  Graph graph;
  const auto id = graph.addNode<MixNode>(3, 1);
  auto& mix = static_cast<MixNode&>(graph.node(id));
  const auto first = mix.in(0), second = mix.in(1), third = mix.in(2);
  const auto output = mix.out();
  const auto sourceId = graph.addNode<GainNode>(1);
  auto& source = static_cast<GainNode&>(graph.node(sourceId));
  graph.connect(sourceId, source.out(), id, third);
  CHECK_THROWS_AS(mix.configure(graph, id, 1, 2), std::invalid_argument);
  CHECK(mix.inputCount() == 3);
  CHECK(mix.channels() == 1);
  CHECK(mix.in(0) == first);
  CHECK(mix.in(1) == second);
  CHECK(mix.in(2) == third);
  CHECK(mix.out() == output);
  REQUIRE(graph.connections().size() == 1);
  CHECK(graph.connections()[0].dstPort == third);

  mix.configure(graph, id, 4, 1);
  const auto fourth = mix.in(3);
  CHECK(fourth > sourceId);
  CHECK(mix.in(2) == third);
  graph.disconnect(id, third);
  mix.configure(graph, id, 1, 2);
  CHECK(mix.inputCount() == 1);
  CHECK(mix.channels() == 2);
  CHECK(mix.in(0) == first);
  CHECK(mix.out() == output);
  CHECK(mix.inputs()[0].type == PortType::audio(2));
  CHECK(mix.outputs()[0].type == PortType::audio(2));
  mix.configure(graph, id, 2, 2);
  CHECK(mix.in(1) > fourth);
  CHECK_THROWS_AS(graph.connect(sourceId, source.out(), id, third),
                  std::invalid_argument);
  CHECK_THROWS_AS(mix.configure(graph, id, 0, 2), std::invalid_argument);
  CHECK_THROWS_AS(mix.configure(graph, id, 2, 0), std::invalid_argument);
  CHECK(mix.inputCount() == 2);
  const auto sinkId = graph.addNode<CaptureSinkNode>(2);
  auto& sink = static_cast<CaptureSinkNode&>(graph.node(sinkId));
  graph.connect(id, output, sinkId, sink.in());
  const auto signal = graph.addNode<SteadySignal>(2);
  const auto signalPort = static_cast<const SteadySignal&>(graph.node(signal)).out();
  graph.connect(signal, signalPort, id, mix.in(0));
  graph.connect(signal, signalPort, id, mix.in(1));
  Engine engine(graph, 48000, 17);
  engine.process(13);
  CHECK(sink.samples() == std::vector<float>(26, 0.5f));
}

TEST_CASE("layout follows the authored graph through moves and does not change wiring") {
  Workspace original;
  const auto id = original.graph().addNode<ConstFloatNode>(0.5f);
  CHECK(original.position(id) == NodePosition{});
  original.setPosition(id, {-37, 101});
  CHECK_THROWS_AS(original.setPosition(id, {0, std::numeric_limits<double>::quiet_NaN()}),
                  std::invalid_argument);
  Workspace moved = std::move(original);
  CHECK(moved.graph().nodeIds() == std::vector<NodeId>{id});
  CHECK(moved.position(id) == NodePosition{-37, 101});
  const auto next = moved.graph().addNode<GainNode>(1);
  CHECK(next > id);
  CHECK(moved.position(next) == NodePosition{});
  CHECK(moved.graph().connections().empty());
}

TEST_CASE("shared save and open preserve authored identities and later minting") {
  Workspace source;
  auto& graph = source.graph();
  const auto deleted = graph.addNode<ConstFloatNode>(9.0f);
  graph.removeNode(deleted);
  const auto constant = graph.addNode<ConstFloatNode>(0.75f);
  const auto gain = graph.addNode<GainNode>(2);
  const auto mix = graph.addNode<MixNode>(3, 2);
  auto& c = static_cast<ConstFloatNode&>(graph.node(constant));
  auto& g = static_cast<GainNode&>(graph.node(gain));
  auto& m = static_cast<MixNode&>(graph.node(mix));
  const auto removedPort = m.in(1);
  graph.removePort(mix, removedPort);
  m.configure(graph, mix, 3, 2);
  graph.connect(constant, c.value(), gain, g.gain());
  graph.connect(gain, g.out(), mix, m.in(1));
  source.setPosition(mix, {-37.5, 193.25});
  const auto lastDeleted = graph.addNode<ConstFloatNode>(7.0f);
  graph.removeNode(lastDeleted);

  std::stringstream file;
  saveGraph(source, file, primitiveCodecs());
  auto loaded = openGraph(file, primitiveCodecs());
  auto& restored = loaded.graph();
  CHECK(restored.nodeIds() == graph.nodeIds());
  CHECK(loaded.position(mix) == NodePosition{-37.5, 193.25});
  for (const auto id : graph.nodeIds()) {
    for (const auto input : {false, true}) {
      const auto before = input ? graph.node(id).inputs() : graph.node(id).outputs();
      const auto after = input ? restored.node(id).inputs() : restored.node(id).outputs();
      REQUIRE(before.size() == after.size());
      for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK(after[i].id == before[i].id);
        CHECK(after[i].type == before[i].type);
        CHECK(after[i].name == before[i].name);
      }
    }
  }
  REQUIRE(restored.connections().size() == 2);
  CHECK(restored.connections()[0].srcNode == constant);
  CHECK(restored.connections()[0].srcPort == c.value());
  CHECK(restored.connections()[0].dstNode == gain);
  CHECK(restored.connections()[0].dstPort == g.gain());
  CHECK_THROWS_AS(restored.removeNode(gain), std::invalid_argument);
  CHECK_THROWS_AS(restored.removePort(mix, removedPort), std::invalid_argument);
  auto& loadedMix = static_cast<MixNode&>(restored.node(mix));
  loadedMix.configure(restored, mix, 4, 2);
  CHECK(loadedMix.in(3) > lastDeleted);

  // Drive reconstructed private port references through real processing.
  const auto signal = restored.addNode<SteadySignal>(2);
  CHECK(signal > lastDeleted);
  CHECK(static_cast<ConstFloatNode&>(restored.node(constant)).constant() == 0.75f);
  CHECK(static_cast<GainNode&>(restored.node(gain)).channels() == 2);
  const auto sink = restored.addNode<CaptureSinkNode>(2);
  auto& loadedGain = static_cast<GainNode&>(restored.node(gain));
  restored.connect(signal, static_cast<SteadySignal&>(restored.node(signal)).out(),
                   gain, loadedGain.in());
  auto& capture = static_cast<CaptureSinkNode&>(restored.node(sink));
  restored.connect(mix, loadedMix.out(), sink, capture.in());
  Engine engine(restored, 48000, 17);
  engine.process(17);
  REQUIRE(capture.samples().size() == 34);
  for (const auto sample : capture.samples()) CHECK(sample == 0.1875f);
}

TEST_CASE("a graph saved by the version 1 writer opens with its identities port types and wires") {
  // Saved once by the version-1 Save and never edited; tests/fixtures/README.md
  // records how. Expected values are literal, not recomputed: an audio port by
  // its channel count, a value or event port by its subtype (event 1 trigger,
  // event 2 MIDI, value 3 float).
  const auto path =
      std::filesystem::path(MODULAR_TEST_FIXTURES) / "graph-version-1.modular";
  {
    std::ifstream header(path, std::ios::binary);
    std::string version;
    std::getline(header, version);
    REQUIRE(version == "MODULAR_GRAPH 1");
  }
  const auto loaded = openGraphFile(path, primitiveCodecs());
  const auto& graph = loaded.graph();
  CHECK(graph.nextIdentity() == 25); // 23 and 24 belonged to a deleted node
  REQUIRE(graph.nodeIds() == std::vector<NodeId>{2, 6, 9, 11, 15, 19, 22});

  using Ports = std::vector<std::tuple<PortId, std::string, PortType>>;
  const std::vector<std::tuple<NodeId, Ports, Ports>> expected{
      {2, {}, {{1, "trigger", PortType::event(1)}}},
      {6, {{3, "start", PortType::event(1)}, {4, "stop", PortType::event(1)}},
          {{5, "midi", PortType::event(2)}}},
      {9, {{7, "midi", PortType::event(2)}}, {{8, "out", PortType::audio(1)}}},
      {11, {}, {{10, "value", PortType::value(3)}}},
      {15, {{12, "in", PortType::audio(1)}, {13, "gain", PortType::value(3)}},
           {{14, "out", PortType::audio(1)}}},
      {19, {{16, "in", PortType::audio(2)}, {17, "gain", PortType::value(3)}},
           {{18, "out", PortType::audio(2)}}},
      {22, {{20, "in0", PortType::audio(2)}}, {{21, "out", PortType::audio(2)}}}};
  for (const auto& [id, inputs, outputs] : expected) {
    for (const auto input : {true, false}) {
      const auto& want = input ? inputs : outputs;
      const auto got = input ? graph.node(id).inputs() : graph.node(id).outputs();
      INFO("node ", id, input ? " inputs" : " outputs");
      REQUIRE(got.size() == want.size());
      for (std::size_t i = 0; i < want.size(); ++i) {
        const auto& [port, name, type] = want[i];
        CHECK(got[i].id == port);
        CHECK(got[i].name == name);
        CHECK(got[i].type == type);
      }
    }
  }

  std::vector<std::tuple<NodeId, PortId, NodeId, PortId>> wires;
  for (const auto& wire : graph.connections())
    wires.emplace_back(wire.srcNode, wire.srcPort, wire.dstNode, wire.dstPort);
  CHECK(wires == decltype(wires){
      {2, 1, 6, 3},       // event 1: trigger to MIDI block start
      {6, 5, 9, 7},       // event 2: MIDI block to synth
      {9, 8, 15, 12},     // mono audio: synth to gain
      {11, 10, 15, 13},   // value 3: constant to gain level
      {19, 18, 22, 20}}); // stereo audio: gain to mixer
}

namespace {
// A test-only id, as in the kernel tests: the node library defines no audio
// subtype yet.
constexpr std::uint32_t kMel = 7;

// A mel spectrogram's output, an N-band spectral stream. Like the mixer's
// arity, its width comes from its saved declaration, and it has no width check
// of its own, so a zero width reaches the kernel's rule.
class Spectrum : public Node {
public:
  Spectrum(Graph& graph, std::uint32_t bands) : Node(graph) {
    output_ = addOutput(PortType::audio(bands, kMel), "spectrum");
  }
  PortId out() const { return output_; }
  void process(ProcessContext&) override {}
private:
  PortId output_ = kNoPort;
};

std::vector<NodeCodec> codecsWithSpectrum() {
  std::vector<NodeCodec> codecs(primitiveCodecs().begin(), primitiveCodecs().end());
  codecs.push_back({"test.spectrum/1",
      [](const Node& n) { return typeid(n) == typeid(Spectrum); },
      [](const Node&) { return NodeContent{}; },
      [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent&) {
        if (!ports.inputs.empty() || ports.outputs.size() != 1)
          throw std::invalid_argument("open: invalid spectrum ports");
        return g.restoreNode<Spectrum>(id, ports, ports.outputs[0].type.channels);
      }});
  return codecs;
}

std::string fileBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}
} // namespace

TEST_CASE("a saved spectral port reopens with its subtype and refuses a speaker bus") {
  const auto codecs = codecsWithSpectrum();
  Workspace authored;
  const auto id = authored.graph().addNode<Spectrum>(80);
  const auto out = static_cast<Spectrum&>(authored.graph().node(id)).out();
  std::stringstream saved;
  saveGraph(authored, saved, codecs);
  const auto text = saved.str();
  CHECK(text.rfind("MODULAR_GRAPH 2\n", 0) == 0);
  // Subtype, then channel count: kMel is 7, the width 80.
  CHECK(text.find("PORT " + std::to_string(out) + " 0 7 80 \"spectrum\"\n") !=
        std::string::npos);

  auto reopened = openGraph(saved, codecs);
  auto& graph = reopened.graph();
  REQUIRE(graph.node(id).outputs().size() == 1);
  const auto type = graph.node(id).outputs()[0].type;
  CHECK(type == PortType::audio(80, kMel));
  CHECK(type.sub == kMel);
  CHECK(type.channels == 80);
  const auto speakers = graph.addNode<CaptureSinkNode>(80);
  const auto bus = static_cast<CaptureSinkNode&>(graph.node(speakers)).in();
  CHECK_THROWS_AS(graph.connect(id, out, speakers, bus), std::invalid_argument);
  CHECK(graph.connections().empty());
}

TEST_CASE("zero-width or mistyped saved ports fail to open and keep the graph") {
  const auto codecs = codecsWithSpectrum();
  const std::string valid =
      "MODULAR_GRAPH 2\nNEXT 99\nNODES 3\n"
      "NODE 2 \"modular.const-float/1\"\nSETTINGS \"0.5\"\nCONTENT \"\"\n"
      "POSITION 0 0\nINPUTS 0\nOUTPUTS 1\nPORT 1 1 3 0 \"value\"\n"
      "NODE 6 \"modular.gain/1\"\nSETTINGS \"1\"\nCONTENT \"\"\n"
      "POSITION -17 31\nINPUTS 2\nPORT 3 0 0 1 \"in\"\nPORT 4 1 3 0 \"gain\"\n"
      "OUTPUTS 1\nPORT 5 0 0 1 \"out\"\n"
      "NODE 8 \"test.spectrum/1\"\nSETTINGS \"\"\nCONTENT \"\"\n"
      "POSITION 0 0\nINPUTS 0\nOUTPUTS 1\nPORT 7 0 7 80 \"spectrum\"\n"
      "WIRES 1\nWIRE 2 1 6 4\nEND\n";
  std::istringstream original(valid);
  auto current = openGraph(original, codecs);
  REQUIRE(current.graph().nodeCount() == 3);
  CHECK(current.graph().node(6).inputs()[0].type == PortType::audio(1));
  CHECK(current.graph().node(6).inputs()[1].type == PortType::value(subtype::Float));
  CHECK(current.graph().node(8).outputs()[0].type == PortType::audio(80, kMel));
  for (const auto& [before, after] : std::vector<std::pair<std::string, std::string>>{
       {"PORT 3 0 0 1", "PORT 3 0 0 0"},     // the gain declares audio<1>
       {"PORT 7 0 7 80", "PORT 7 0 7 0"},    // declared as saved: the kernel's rule
       {"PORT 7 0 7 80", "PORT 7 0 0 80"},   // the subtype cannot be lost
       {"PORT 3 0 0 1", "PORT 3 0 7 1"},     // nor gained
       {"PORT 1 1 3 0", "PORT 1 1 3 1"},     // a value port has no channels
       {"PORT 1 1 3 0", "PORT 1 1 3"},       // a version 1 record in version 2
       {"PORT 3 0 0 1", "PORT 3 0 0 -1"}}) {
    auto corrupt = valid;
    const auto at = corrupt.find(before);
    REQUIRE(at != std::string::npos);
    corrupt.replace(at, before.size(), after);
    std::istringstream input(corrupt);
    INFO(before, " -> ", after);
    CHECK_THROWS_AS(current = openGraph(input, codecs), std::invalid_argument);
    CHECK(current.graph().nodeCount() == 3);
    CHECK(static_cast<ConstFloatNode&>(current.graph().node(2)).constant() == 0.5f);
  }

  // A version 1 audio port's one number is its width, so a zero there fails too:
  // against the gain's declaration, and through the kernel's rule for a node that
  // declares the width it was saved with.
  const auto version1 = [](const std::string& kind, const std::string& ports) {
    return "MODULAR_GRAPH 1\nNEXT 9\nNODES 1\nNODE 4 \"" + kind + "\"\nSETTINGS \"" +
        (kind == "modular.gain/1" ? "1" : "") + "\"\nCONTENT \"\"\nPOSITION 0 0\n" +
        ports + "WIRES 0\nEND\n";
  };
  const std::string gainPorts = "INPUTS 2\nPORT 1 0 1 \"in\"\nPORT 2 1 3 \"gain\"\n"
      "OUTPUTS 1\nPORT 3 0 1 \"out\"\n";
  std::istringstream mono(version1("modular.gain/1", gainPorts));
  CHECK_NOTHROW(openGraph(mono, codecs));
  auto zeroGain = gainPorts;
  zeroGain.replace(zeroGain.find("PORT 1 0 1"), 10, "PORT 1 0 0");
  std::istringstream zeroIn(version1("modular.gain/1", zeroGain));
  CHECK_THROWS_WITH_AS(current = openGraph(zeroIn, codecs),
                       "restore: port declaration does not match node",
                       std::invalid_argument);
  std::istringstream zeroSpectrum(version1("test.spectrum/1",
      "INPUTS 0\nOUTPUTS 1\nPORT 1 0 0 \"spectrum\"\n"));
  CHECK_THROWS_WITH_AS(current = openGraph(zeroSpectrum, codecs),
                       "declare: an audio port needs at least one channel",
                       std::invalid_argument);
  CHECK(current.graph().nodeCount() == 3);
  CHECK(static_cast<ConstFloatNode&>(current.graph().node(2)).constant() == 0.5f);
}

TEST_CASE("a reopened version 1 graph saves as version 2 and reopens unchanged") {
  const auto path =
      std::filesystem::path(MODULAR_TEST_FIXTURES) / "graph-version-1.modular";
  const auto original = fileBytes(path);
  REQUIRE(original.rfind("MODULAR_GRAPH 1\n", 0) == 0);
  const auto first = openGraphFile(path, primitiveCodecs());
  CHECK(fileBytes(path) == original); // only the user's next Save changes it

  std::stringstream saved;
  saveGraph(first, saved, primitiveCodecs());
  const auto text = saved.str();
  CHECK(text.rfind("MODULAR_GRAPH 2\n", 0) == 0);
  // The fixture's PORT 16 0 2, PORT 17 1 3 and PORT 5 2 2, as version 2 writes
  // them: subtype, then channel count.
  CHECK(text.find("PORT 16 0 0 2 \"in\"\n") != std::string::npos);
  CHECK(text.find("PORT 17 1 3 0 \"gain\"\n") != std::string::npos);
  CHECK(text.find("PORT 5 2 2 0 \"midi\"\n") != std::string::npos);

  const auto second = openGraph(saved, primitiveCodecs());
  const auto& before = first.graph();
  const auto& after = second.graph();
  CHECK(after.nextIdentity() == before.nextIdentity());
  REQUIRE(after.nodeIds() == before.nodeIds());
  for (const auto id : before.nodeIds()) {
    for (const auto input : {true, false}) {
      const auto want = input ? before.node(id).inputs() : before.node(id).outputs();
      const auto got = input ? after.node(id).inputs() : after.node(id).outputs();
      INFO("node ", id, input ? " inputs" : " outputs");
      REQUIRE(got.size() == want.size());
      for (std::size_t i = 0; i < want.size(); ++i) {
        CHECK(got[i].id == want[i].id);
        CHECK(got[i].name == want[i].name);
        CHECK(got[i].type == want[i].type);
      }
    }
  }
  REQUIRE(after.connections().size() == before.connections().size());
  for (std::size_t i = 0; i < before.connections().size(); ++i) {
    const auto& want = before.connections()[i];
    const auto& got = after.connections()[i];
    CHECK(std::tie(got.srcNode, got.srcPort, got.dstNode, got.dstPort) ==
          std::tie(want.srcNode, want.srcPort, want.dstNode, want.dstPort));
  }
  std::stringstream resaved;
  saveGraph(second, resaved, primitiveCodecs());
  CHECK(resaved.str() == text);
}

TEST_CASE("recording graphs reopen with owned MIDI and settings but no transient take") {
  WavTestFiles files;
  const auto imported = files.directory / "import.mid";
  std::filesystem::copy_file(std::filesystem::path(MODULAR_TEST_FIXTURES) / "one-note.mid",
                              imported);
  Workspace workspace;
  auto& graph = workspace.graph();
  const auto start = graph.addNode<ManualTriggerNode>();
  const auto stop = graph.addNode<ManualTriggerNode>(ManualTriggerNode::Label::Stop);
  const auto midi = graph.addNode<MidiFileNode>(imported);
  const auto synth = graph.addNode<SineSynthNode>();
  const auto recorder = graph.addNode<WavRecorderNode>(files.directory / L"录音-é", "session");
  auto& recording = static_cast<WavRecorderNode&>(graph.node(recorder));
  auto& playback = static_cast<MidiFileNode&>(graph.node(midi));
  auto& instrument = static_cast<SineSynthNode&>(graph.node(synth));
  const auto trigger = [&](NodeId id) -> ManualTriggerNode& {
    return static_cast<ManualTriggerNode&>(graph.node(id));
  };
  graph.connect(start, trigger(start).trigger(), midi, playback.trigger());
  graph.connect(stop, trigger(stop).trigger(), midi, playback.stop());
  graph.connect(midi, playback.midi(), synth, instrument.midiIn());
  graph.connect(synth, instrument.out(), recorder, recording.in());
  graph.connect(start, trigger(start).trigger(), recorder, recording.start());
  graph.connect(stop, trigger(stop).trigger(), recorder, recording.stop());
  workspace.setPosition(recorder, {437, -19});
  const auto retired = graph.addNode<ConstFloatNode>(1.0f);
  graph.removeNode(retired);
  std::filesystem::remove(imported);
  std::stringstream baseline;
  saveGraph(workspace, baseline, primitiveCodecs());
  {
    Engine engine(graph, 48000, 127);
    trigger(start).press(); engine.process(113);
    CHECK(recording.state() == WavRecorderNode::State::Recording);
    for (const bool failed : {false, true}) {
      if (failed) {
        trigger(stop).press(); engine.process(17);
        CHECK(recording.state() == WavRecorderNode::State::Failed);
      }
      const auto file = files.directory / "recording.modular";
      saveGraphFile(workspace, file, primitiveCodecs());
      auto loaded = openGraphFile(file, primitiveCodecs());
      auto& restored = loaded.graph();
      auto& take = static_cast<WavRecorderNode&>(restored.node(recorder));
      CHECK(take.state() == WavRecorderNode::State::Idle);
      CHECK(take.directory() == files.directory / L"录音-é");
      CHECK(take.prefix() == "session");
      CHECK(take.channels() == 1);
      CHECK(take.start() == recording.start());
      CHECK(take.stop() == recording.stop());
      CHECK(take.in() == recording.in());
      CHECK(loaded.position(recorder) == NodePosition{437, -19});
      CHECK(restored.nodeIds() == graph.nodeIds());
      CHECK(restored.connections().size() == 6);
      std::stringstream saved;
      saveGraph(loaded, saved, primitiveCodecs());
      CHECK(saved.str() == baseline.str());
      CHECK(restored.addNode<ConstFloatNode>(1.0f) > retired);
      std::filesystem::create_directory(take.directory());
      Engine run(restored, 48000, 127);
      run.process(113);
      CHECK(take.state() == WavRecorderNode::State::Idle);
      static_cast<ManualTriggerNode&>(restored.node(start)).press();
      run.process(113);
      static_cast<ManualTriggerNode&>(restored.node(stop)).press();
      run.process(17);
      const auto wav = readTestWav(take.directory() / "session-1.wav");
      REQUIRE(wav.samples.size() == 113);
      CHECK(std::any_of(wav.samples.begin(), wav.samples.end(),
                       [](auto sample) { return sample != 0; }));
      std::filesystem::remove(take.directory() / "session-1.wav");
      std::filesystem::remove(take.directory());
    }
  }
  CHECK(recording.state() == WavRecorderNode::State::Idle);
}
