#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <limits>
#include <tuple>

#include "modular/Engine.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/MidiFileNode.h"
#include "modular/nodes/ManualTriggerNode.h"

using namespace modular;
using namespace modular::nodes;

namespace {
using Bytes = std::vector<std::uint8_t>;

void integer(Bytes& out, std::uint32_t value, int size) {
  for (int i = size - 1; i >= 0; --i)
    out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

void chunk(Bytes& out, const char* tag, const Bytes& content) {
  out.insert(out.end(), tag, tag + 4);
  integer(out, static_cast<std::uint32_t>(content.size()), 4);
  out.insert(out.end(), content.begin(), content.end());
}

Bytes smf(std::uint16_t format, std::uint16_t division,
          const std::vector<Bytes>& tracks) {
  Bytes header;
  integer(header, format, 2);
  integer(header, static_cast<std::uint32_t>(tracks.size()), 2);
  integer(header, division, 2);
  Bytes result;
  chunk(result, "MThd", header);
  for (const auto& track : tracks) chunk(result, "MTrk", track);
  return result;
}

Bytes oneNote() {
  return smf(0, 480, {{
      0, 0x90, 60, 100,        // note-on at zero
      0x83, 0x60, 60, 0,       // running-status note-off at tick 480
      0x83, 0x60, 0xff, 0x2f, 0 // end at tick 960, preserving the silent tail
  }});
}

Bytes tempoChange() {
  return smf(1, 480, {
      {0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20, // 500000 us / quarter
       0x83, 0x60, 0xff, 0x51, 3, 0x0f, 0x42, 0x40, // tick 480: 1000000
       0x83, 0x60, 0xff, 0x2f, 0}, // end at tick 960
      {0, 0x90, 60, 100,
       0x87, 0x40, 0x80, 60, 0, // tick 960: 1.5 seconds
       0, 0xff, 0x2f, 0},
      {0, 0x91, 64, 80,
       0x83, 0x60, 0x81, 64, 0, // tick 480: 0.5 seconds
       0x83, 0x60, 0xff, 0x2f, 0}});
}

class EventLog : public Node {
public:
  using Record = std::tuple<std::uint64_t, int, int, int>;
  explicit EventLog(Graph& graph) : Node(graph) {
    in = addInput(PortType::event(subtype::Midi), "in");
  }
  void process(ProcessContext& ctx) override {
    for (const auto& event : ctx.eventsIn(in)) {
      const auto m = event.as<MidiMsg>();
      records.emplace_back(ctx.blockStartFrame() + event.offset,
                           m.status, m.data1, m.data2);
    }
  }
  PortId in;
  std::vector<Record> records;
};

class Retrigger : public Node {
public:
  explicit Retrigger(Graph& graph) : Node(graph) {
    out = addOutput(PortType::event(subtype::Trigger), "out");
  }
  void process(ProcessContext& ctx) override {
    for (const std::uint64_t frame : {101u, 107u, 173u}) {
      if (frame >= ctx.blockStartFrame() &&
          frame < ctx.blockStartFrame() + ctx.numFrames())
        ctx.eventsOut(out).push(Event::make(
            static_cast<std::uint32_t>(frame - ctx.blockStartFrame()), TriggerPayload{}));
    }
  }
  PortId out;
};

class SignalsAt : public Node {
public:
  SignalsAt(Graph& graph, std::vector<std::uint64_t> frames)
      : Node(graph), frames_(std::move(frames)) {
    out = addOutput(PortType::event(subtype::Trigger), "out");
  }
  void process(ProcessContext& ctx) override {
    for (const auto frame : frames_)
      if (frame >= ctx.blockStartFrame() && frame < ctx.blockStartFrame() + ctx.numFrames())
        ctx.eventsOut(out).push(Event::make(
            static_cast<std::uint32_t>(frame - ctx.blockStartFrame()), TriggerPayload{}));
  }
  PortId out;
private:
  std::vector<std::uint64_t> frames_;
};
} // namespace

TEST_CASE("MIDI files use the default tempo and preserve running status and silent tails") {
  const auto file = MidiFile::parse(oneNote());
  REQUIRE(file.entries().size() == 2);
  CHECK(file.entries()[0].seconds == doctest::Approx(0));
  CHECK(file.entries()[0].message.isNoteOn());
  CHECK(file.entries()[1].seconds == doctest::Approx(0.5));
  CHECK(file.entries()[1].message.isNoteOff());
  CHECK(file.durationSeconds() == doctest::Approx(1));
}

TEST_CASE("a file-backed node loads an actual MIDI file and reports missing files") {
  Graph graph;
  const auto path = std::filesystem::path(MODULAR_TEST_FIXTURES) / "one-note.mid";
  const auto id = graph.addNode<MidiFileNode>(path);
  auto& node = static_cast<MidiFileNode&>(graph.node(id));
  REQUIRE(node.file().entries().size() == 2);
  CHECK(node.file().entries().front().message.data1 == 60);
  CHECK(node.file().durationSeconds() == doctest::Approx(1));
  CHECK_THROWS_AS(MidiFile::read(path / "missing.mid"), std::invalid_argument);
}

TEST_CASE("simultaneous tracks share a tempo map and retain deterministic event order") {
  const auto file = MidiFile::parse(tempoChange());
  REQUIRE(file.entries().size() == 4);
  const int statuses[] = {0x90, 0x91, 0x81, 0x80};
  const double times[] = {0, 0, 0.5, 1.5};
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(file.entries()[i].message.status == statuses[i]);
    CHECK(file.entries()[i].seconds == doctest::Approx(times[i]));
  }
  CHECK(file.durationSeconds() == doctest::Approx(1.5));
}

TEST_CASE("channel messages keep their data and unknown metadata does not shift timing") {
  auto bytes = smf(0, 480, {{
      0, 0xc3, 9, 0, 10, // program change and one-byte running status
      0, 0xd3, 12,       // channel pressure
      0, 0xb3, 7, 90,    // control change
      0, 0xa3, 60, 45,   // polyphonic pressure
      0, 0xe3, 1, 64,    // pitch bend
      0x83, 0x60, 0xff, 0x7f, 2, 0xff, 0x80, // opaque metadata at tick 480
      0, 0xf0, 3, 1, 2, 0xf7,
      0, 0xf7, 1, 3,
      0, 0x83, 60, 0,
      0, 0xff, 0x2f, 0}});
  chunk(bytes, "junk", {0xff, 0x80});
  const auto file = MidiFile::parse(bytes);
  REQUIRE(file.entries().size() == 7);
  CHECK(file.entries()[1].message.status == 0xc3);
  CHECK(file.entries()[1].message.data1 == 10);
  CHECK(file.entries()[1].message.data2 == 0);
  CHECK(file.entries()[5].message.data1 == 1);
  CHECK(file.entries()[5].message.data2 == 64);
  CHECK(file.entries().back().seconds == doctest::Approx(0.5));
  CHECK(file.skippedSysExEvents() == 2);
}

TEST_CASE("truncated MIDI files are rejected at every byte boundary") {
  const auto bytes = tempoChange();
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    CAPTURE(length);
    CHECK_THROWS_AS(MidiFile::parse(std::span(bytes).first(length)),
                    std::invalid_argument);
  }
}

TEST_CASE("invalid MIDI headers and unsupported timing are explicit errors") {
  const Bytes end{0, 0xff, 0x2f, 0};
  CHECK_THROWS_AS(MidiFile::parse(smf(2, 480, {end})), std::invalid_argument);
  CHECK_THROWS_AS(MidiFile::parse(smf(0, 0xe728, {end})), std::invalid_argument);
  CHECK_THROWS_AS(MidiFile::parse(smf(0, 0, {end})), std::invalid_argument);
  CHECK_THROWS_AS(MidiFile::parse(smf(0, 480, {})), std::invalid_argument);
  CHECK_THROWS_AS(MidiFile::parse(smf(0, 480, {end, end})), std::invalid_argument);
  auto extra = smf(1, 480, {end});
  chunk(extra, "MTrk", end);
  CHECK_THROWS_AS(MidiFile::parse(extra), std::invalid_argument);
}

TEST_CASE("malformed events cannot consume following chunks or reuse cancelled status") {
  const std::vector<Bytes> invalidTracks = {
      {0, 60, 100}, // running status without a preceding channel status
      {0, 0x90, 60, 100}, // missing end-of-track
      {0, 0xff, 0x2f, 0, 0}, // data after end-of-track
      {0, 0x90, 60, 0x80}, // bad channel data
      {0x81, 0x80, 0x80, 0x80, 0, 0xff, 0x2f, 0}, // five-byte VLQ
      {0, 0xff, 0x51, 3, 0, 0, 0, 0, 0xff, 0x2f, 0}, // zero tempo
      {0, 0xff, 0x51, 2, 1, 2, 0, 0xff, 0x2f, 0}, // short tempo
      {0, 0xf0, 100, 1}, // SysEx extends beyond its track
      {0, 0x90, 60, 100, 0, 0xff, 1, 0, 0, 60, 0, 0, 0xff, 0x2f, 0},
      {0, 0x90, 60, 100, 0, 0xf0, 1, 0xf7, 0, 60, 0, 0, 0xff, 0x2f, 0}};
  for (const auto& track : invalidTracks)
    CHECK_THROWS_AS(MidiFile::parse(smf(1, 480, {track, {0, 0xff, 0x2f, 0}})),
                    std::invalid_argument);
  CHECK_THROWS_AS(MidiFile::parse(smf(1, 480, {
      {0, 0xff, 0x2f, 0}, {0, 0xff, 0x51, 3, 1, 2, 3, 0, 0xff, 0x2f, 0}})),
      std::invalid_argument);
}

TEST_CASE("file playback is triggered at exact frames at different sample rates and blocks") {
  for (const double rate : {44100.0, 48000.0}) {
    for (const std::uint32_t block : {127u, 512u, 1009u}) {
      CAPTURE(rate);
      CAPTURE(block);
      Graph graph;
      const auto startId = graph.addNode<TriggerAtNode>(101);
      const auto fileId = graph.addNode<MidiFileNode>(MidiFile::parse(tempoChange()));
      const auto logId = graph.addNode<EventLog>();
      auto& start = static_cast<TriggerAtNode&>(graph.node(startId));
      auto& file = static_cast<MidiFileNode&>(graph.node(fileId));
      auto& log = static_cast<EventLog&>(graph.node(logId));
      graph.connect(startId, start.trigger(), fileId, file.trigger());
      graph.connect(fileId, file.midi(), logId, log.in);
      Engine engine(graph, rate, block);
      const auto halfSecond = static_cast<std::uint64_t>(rate / 2);
      CHECK(file.durationFrames() == 3 * halfSecond);
      while (engine.streamTime() <= 101 + file.durationFrames()) engine.process(block);
      const std::vector<EventLog::Record> expected{
          {101, 0x90, 60, 100}, {101, 0x91, 64, 80},
          {101 + halfSecond, 0x81, 64, 0}, {101 + 3 * halfSecond, 0x80, 60, 0}};
      CHECK(log.records == expected);
    }
  }
}

TEST_CASE("loading a MIDI file does not start it without a connected trigger") {
  Graph graph;
  const auto fileId = graph.addNode<MidiFileNode>(MidiFile::parse(oneNote()));
  const auto logId = graph.addNode<EventLog>();
  auto& file = static_cast<MidiFileNode&>(graph.node(fileId));
  auto& log = static_cast<EventLog&>(graph.node(logId));
  graph.connect(fileId, file.midi(), logId, log.in);
  Engine engine(graph, 48000, 512);
  engine.process(512);
  CHECK(log.records.empty());
}

TEST_CASE("retriggering preserves earlier events in the block and replaces pending events") {
  for (const std::uint32_t block : {7u, 127u, 512u}) {
    Graph graph;
    const auto startId = graph.addNode<Retrigger>();
    const auto fileId = graph.addNode<MidiFileNode>(MidiFile::parse(smf(0, 480, {{
        0, 0x90, 60, 100, 1, 0x80, 60, 0, 1, 0xff, 0x2f, 0}})));
    const auto logId = graph.addNode<EventLog>();
    auto& start = static_cast<Retrigger&>(graph.node(startId));
    auto& file = static_cast<MidiFileNode&>(graph.node(fileId));
    auto& log = static_cast<EventLog&>(graph.node(logId));
    graph.connect(startId, start.out, fileId, file.trigger());
    graph.connect(fileId, file.midi(), logId, log.in);
    Engine engine(graph, 48000, block);
    while (engine.streamTime() < 300) engine.process(block);
    const std::vector<EventLog::Record> expected{
        {101, 0x90, 60, 100}, {107, 0x80, 60, 0}, {107, 0x90, 60, 100},
        {157, 0x80, 60, 0},
        {173, 0x90, 60, 100}, {223, 0x80, 60, 0}};
    CHECK(log.records == expected);
  }
}

TEST_CASE("file preparation rejects invalid rates before converting to integer frames") {
  Graph graph;
  const auto id = graph.addNode<MidiFileNode>(MidiFile::parse(oneNote()));
  auto& node = static_cast<MidiFileNode&>(graph.node(id));
  for (const double rate : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN(), 1e100})
    CHECK_THROWS_AS(node.prepare({rate, 512}), std::invalid_argument);
}

TEST_CASE("wired Stop wins ties and completion releases held notes with sample-exact silence") {
  std::vector<float> reference;
  for (const std::uint32_t block : {1u, 7u, 127u, 512u}) {
    Graph graph;
    const auto starts = graph.addNode<SignalsAt>(
        std::vector<std::uint64_t>{101, 173, 251, 401});
    const auto stops = graph.addNode<SignalsAt>(std::vector<std::uint64_t>{173, 337});
    const auto fileId = graph.addNode<MidiFileNode>(MidiFile::parse(smf(0, 480, {{
        0, 0x90, 60, 100, 1, 0x91, 64, 100, 1, 0x90, 67, 100,
        1, 0xff, 0x2f, 0}})));
    const auto logId = graph.addNode<EventLog>();
    const auto synthId = graph.addNode<SineSynthNode>();
    const auto sinkId = graph.addNode<CaptureSinkNode>(1);
    auto& file = static_cast<MidiFileNode&>(graph.node(fileId));
    auto& synth = static_cast<SineSynthNode&>(graph.node(synthId));
    auto& sink = static_cast<CaptureSinkNode&>(graph.node(sinkId));
    auto& log = static_cast<EventLog&>(graph.node(logId));
    graph.connect(starts, static_cast<SignalsAt&>(graph.node(starts)).out,
                  fileId, file.trigger());
    graph.connect(stops, static_cast<SignalsAt&>(graph.node(stops)).out, fileId, file.stop());
    graph.connect(fileId, file.midi(), logId, log.in);
    graph.connect(fileId, file.midi(), synthId, synth.midiIn());
    graph.connect(synthId, synth.out(), sinkId, sink.in());
    Engine engine(graph, 48000, block);
    while (engine.streamTime() < 701)
      engine.process(static_cast<std::uint32_t>(
          std::min<std::uint64_t>(block, 701 - engine.streamTime())));
    const std::vector<EventLog::Record> expected{
        {101, 0x90, 60, 100}, {151, 0x91, 64, 100},
        {173, 0x80, 60, 0}, {173, 0x81, 64, 0},
        {251, 0x90, 60, 100}, {301, 0x91, 64, 100},
        {337, 0x80, 60, 0}, {337, 0x81, 64, 0},
        {401, 0x90, 60, 100}, {451, 0x91, 64, 100}, {501, 0x90, 67, 100},
        {551, 0x80, 60, 0}, {551, 0x80, 67, 0}, {551, 0x81, 64, 0}};
    CHECK(log.records == expected);
    const auto& audio = sink.samples();
    REQUIRE(audio.size() == 701);
    for (std::size_t i = 0; i < audio.size(); ++i)
      if (i < 101 || (i >= 173 && i < 251) || (i >= 337 && i < 401) || i >= 551)
        CHECK(audio[i] == 0);
    CHECK(std::abs(audio[172]) > 0.01f);
    CHECK(std::abs(audio[336]) > 0.01f);
    CHECK(std::abs(audio[550]) > 0.01f);
    if (reference.empty()) reference = audio;
    else CHECK(audio == reference);
  }
}

TEST_CASE("manual buttons emit only through their wires at the next processed frame") {
  for (const bool wired : {false, true}) {
    Graph graph;
    const auto startId = graph.addNode<ManualTriggerNode>();
    const auto fileId = graph.addNode<MidiFileNode>(MidiFile::parse(oneNote()));
    const auto logId = graph.addNode<EventLog>();
    auto& start = static_cast<ManualTriggerNode&>(graph.node(startId));
    auto& file = static_cast<MidiFileNode&>(graph.node(fileId));
    auto& log = static_cast<EventLog&>(graph.node(logId));
    if (wired) graph.connect(startId, start.trigger(), fileId, file.trigger());
    graph.connect(fileId, file.midi(), logId, log.in);
    Engine engine(graph, 48000, 127);
    engine.process(101);
    CHECK(log.records.empty());
    start.press();
    engine.process(13);
    engine.process(17);
    const std::vector<EventLog::Record> expected = wired
        ? std::vector<EventLog::Record>{{101, 0x90, 60, 100}}
        : std::vector<EventLog::Record>{};
    CHECK(log.records == expected);
  }
}

TEST_CASE("a fresh execution starts idle after abandoning a sounding sequence") {
  Graph graph;
  const auto startId = graph.addNode<ManualTriggerNode>();
  const auto fileId = graph.addNode<MidiFileNode>(MidiFile::parse(oneNote()));
  const auto synthId = graph.addNode<SineSynthNode>();
  const auto sinkId = graph.addNode<CaptureSinkNode>(1);
  auto& start = static_cast<ManualTriggerNode&>(graph.node(startId));
  auto& file = static_cast<MidiFileNode&>(graph.node(fileId));
  auto& synth = static_cast<SineSynthNode&>(graph.node(synthId));
  auto& sink = static_cast<CaptureSinkNode&>(graph.node(sinkId));
  graph.connect(startId, start.trigger(), fileId, file.trigger());
  graph.connect(fileId, file.midi(), synthId, synth.midiIn());
  graph.connect(synthId, synth.out(), sinkId, sink.in());
  {
    Engine engine(graph, 48000, 127);
    start.press();
    engine.process(127);
    CHECK(std::abs(sink.samples()[126]) > 0.01f);
    start.press(); // an unconsumed press must not leak to the next execution
  }
  Engine next(graph, 48000, 127);
  const auto before = sink.samples().size();
  next.process(127);
  for (std::size_t i = before; i < sink.samples().size(); ++i) CHECK(sink.samples()[i] == 0);
}

TEST_CASE("an empty MIDI node can be repaired by import and keeps content after a failed replacement") {
  Graph graph;
  const auto id = graph.addNode<MidiFileNode>();
  auto& file = static_cast<MidiFileNode&>(graph.node(id));
  CHECK_FALSE(file.ready());
  CHECK_THROWS_AS(Engine(graph, 48000, 127), std::invalid_argument);
  const auto path = std::filesystem::path(MODULAR_TEST_FIXTURES) / "one-note.mid";
  file.import(path);
  CHECK(file.ready());
  CHECK(file.file().entries().size() == 2);
  CHECK_THROWS_AS(file.import(path / "missing.mid"), std::invalid_argument);
  CHECK(file.ready());
  CHECK_FALSE(file.importError().empty());
  CHECK(file.file().entries().size() == 2);
  CHECK_NOTHROW(Engine(graph, 48000, 127));
}
