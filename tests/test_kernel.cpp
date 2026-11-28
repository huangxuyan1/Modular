#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "modular/Engine.h"
#include "modular/Graph.h"
#include "modular/Supernode.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/MidiNodes.h"

using namespace modular;
using namespace modular::nodes;

namespace {
class ExecutionResource : public Node {
public:
  ExecutionResource(Graph& graph, std::weak_ptr<int>& observed, bool fail)
      : Node(graph), observed_(observed), fail_(fail) {}
  void prepare(const PrepareInfo&) override {
    resource_ = std::make_shared<int>(1);
    observed_ = resource_;
    if (fail_) throw std::invalid_argument("resource: unavailable");
  }
  void release() noexcept override { resource_.reset(); }
  void process(ProcessContext&) override {}
private:
  std::weak_ptr<int>& observed_;
  std::shared_ptr<int> resource_;
  bool fail_;
};
}

TEST_CASE("execution resources are released on destruction and failed preparation") {
  for (const bool fail : {false, true}) {
    Graph graph;
    std::weak_ptr<int> first, second;
    graph.addNode<ExecutionResource>(first, false);
    graph.addNode<ExecutionResource>(second, fail);
    if (fail) {
      CHECK_THROWS_AS(Engine(graph, 48000, 127), std::invalid_argument);
    } else {
      Engine engine(graph, 48000, 127);
      CHECK_FALSE(first.expired());
      CHECK_FALSE(second.expired());
    }
    CHECK(graph.nodeCount() == 2);
    CHECK(first.expired());
    CHECK(second.expired());
  }
}

namespace {

// A node and its id kept together. addNode() returns the id alone, and wiring
// needs both: the id to name the node, and the node itself to ask it for a
// port id through the accessors it publishes. No test in this file names a
// port by position — that is the whole claim under test.
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

// Records the order in which process() is called, into a shared log.
class OrderProbeNode : public Node {
public:
  OrderProbeNode(Graph& graph, int tag, std::vector<int>& log)
      : Node(graph), tag_(tag), log_(log) {
    in_ = addInput(PortType::event(subtype::Trigger), "in");
    out_ = addOutput(PortType::event(subtype::Trigger), "out");
  }

  PortId in() const { return in_; }
  PortId out() const { return out_; }

  void process(ProcessContext&) override { log_.push_back(tag_); }

private:
  PortId in_ = kNoPort, out_ = kNoPort;
  int tag_;
  std::vector<int>& log_;
};

// Several audio inputs told apart in the output: out = sum of weight_i * in_i.
// Which input a wire lands on is therefore audible, which is what makes the
// port-identity claim falsifiable by comparing samples.
//
// It is also the node that exercises port mutation: it adds inputs after it is
// already wired into a graph, and keeps its own members in step when the graph
// removes one.
class WeightedSumNode : public Node {
public:
  WeightedSumNode(Graph& graph, std::vector<float> weights)
      : Node(graph), weights_(std::move(weights)) {
    for (std::size_t i = 0; i < weights_.size(); ++i)
      ins_.push_back(addInput(PortType::audio(1), "in" + std::to_string(i)));
    out_ = addOutput(PortType::audio(1), "out");
  }

  PortId in(std::size_t index) const { return ins_[index]; }
  PortId out() const { return out_; }
  std::size_t inputCount() const { return ins_.size(); }

  // A port declared while this node already sits wired in a graph. Nothing is
  // frozen — this is the shape a node hosting a plugin whose sections switch
  // in and out would need.
  PortId addWeightedInput(float weight) {
    weights_.push_back(weight);
    ins_.push_back(addInput(PortType::audio(1), "in" + std::to_string(ins_.size())));
    return ins_.back();
  }

  void process(ProcessContext& ctx) override {
    const auto out = ctx.audioOut(out_);
    for (std::uint32_t i = 0; i < ctx.numFrames(); ++i) out.channels[0][i] = 0.0f;
    for (std::size_t p = 0; p < ins_.size(); ++p) {
      const auto in = ctx.audioIn(ins_[p]);
      for (std::uint32_t i = 0; i < ctx.numFrames(); ++i)
        out.channels[0][i] += weights_[p] * in.channels[0][i];
    }
  }

protected:
  // The graph has already dropped the port; this is where the node drops the
  // members that named it. Every other id it holds is still good.
  void portRemoved(PortId port) override {
    for (std::size_t i = 0; i < ins_.size(); ++i) {
      if (ins_[i] == port) {
        ins_.erase(ins_.begin() + static_cast<std::ptrdiff_t>(i));
        weights_.erase(weights_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
      }
    }
  }

private:
  std::vector<PortId> ins_;
  std::vector<float> weights_; // parallel to ins_
  PortId out_ = kNoPort;
};

// The milestone clip's content. Shared by every build below, so a difference
// in rendered output can only have come from the grouping.
std::vector<MidiBlockNode::Entry> milestoneClip() {
  return {
      {0, MidiMsg{0x90, 69, 100}},    // A4 on at trigger
      {7000, MidiMsg{0x80, 69, 0}},   // off 7000 frames later
      {9000, MidiMsg{0x90, 72, 100}}, // C5 on
      {15000, MidiMsg{0x80, 72, 0}},
  };
}

// How much of the milestone graph is wrapped up in supernodes. The nodes and
// the wiring between them are the same in every case; only the nesting differs.
enum class Grouping {
  Flat,       // ungrouped, as milestone 1 wired it
  Clip,       // the midi-clip section grouped
  Instrument, // that supernode in turn grouped with the synth
};

void render(Engine& e, std::uint64_t totalFrames, std::uint32_t block) {
  while (e.streamTime() < totalFrames) {
    const std::uint64_t remaining = totalFrames - e.streamTime();
    e.process(remaining < block ? static_cast<std::uint32_t>(remaining) : block);
  }
}

// Guards every comparison of two renders below: two silent renders are equal
// for reasons that have nothing to do with what is being claimed.
bool isSounding(const std::vector<float>& samples) {
  for (float s : samples)
    if (s != 0.0f) return true;
  return false;
}

// Placing a boundary node is the most repeated gesture in the supernode tests;
// naming it keeps the graph-building readable. The boundary node it hands back
// is also how the caller learns the outer port id, since that id *is* the
// boundary node's port id.
Placed<BoundaryNode> exposeInput(Graph& g, PortType type, std::string name) {
  return place<BoundaryNode>(g, BoundaryDirection::Input, type, std::move(name));
}
Placed<BoundaryNode> exposeOutput(Graph& g, PortType type, std::string name) {
  return place<BoundaryNode>(g, BoundaryDirection::Output, type, std::move(name));
}

// A supernode and the outer ports it exposes. A user-space builder hands back
// the face it created, exactly as a node hands back the ports it declared;
// nobody outside has to count boundary nodes to find one.
struct ClipSection {
  Placed<Supernode> super;
  PortId triggerIn = kNoPort;
  PortId midiOut = kNoPort;
};

// The reference example the design leans on, assembled from milestone nodes:
// trigger in, midi out, the stored clip inside. It is user-space wiring — a
// pattern anyone can build — not a library node.
ClipSection addMidiClipSupernode(Graph& parent) {
  const auto s = place<Supernode>(parent);
  Graph& in = s->inner();
  const auto trigB = exposeInput(in, PortType::event(subtype::Trigger), "trigger");
  const auto blk = place<MidiBlockNode>(in, milestoneClip());
  const auto midiB = exposeOutput(in, PortType::event(subtype::Midi), "midi");
  in.connect(trigB.id, trigB->port(), blk.id, blk->trigger());
  in.connect(blk.id, blk->midi(), midiB.id, midiB->port());
  return {s, trigB->port(), midiB->port()};
}

struct Instrument {
  Placed<Supernode> super;
  PortId triggerIn = kNoPort;
  PortId audioOut = kNoPort;
};

// The clip supernode and a synth, grouped again one level up: trigger in,
// audio out. The trigger now crosses two boundaries to reach the clip, and
// nothing on that path runs — the compiler has to resolve through all of it.
Instrument addInstrumentSupernode(Graph& parent) {
  const auto s = place<Supernode>(parent);
  Graph& in = s->inner();
  const auto trigB = exposeInput(in, PortType::event(subtype::Trigger), "trigger");
  const ClipSection clip = addMidiClipSupernode(in);
  const auto synth = place<SineSynthNode>(in);
  const auto outB = exposeOutput(in, PortType::audio(1), "out");
  in.connect(trigB.id, trigB->port(), clip.super.id, clip.triggerIn);
  in.connect(clip.super.id, clip.midiOut, synth.id, synth->midiIn());
  in.connect(synth.id, synth->out(), outB.id, outB->port());
  return {s, trigB->port(), outB->port()};
}

// Builds the full three-domain milestone graph; returns the capture sink.
Placed<CaptureSinkNode> buildMilestoneGraph(Graph& g, std::uint64_t triggerFrame,
                                            Grouping grouping) {
  // The parts every build shares: the transport-start stand-in, and the
  // value-controlled gain feeding the sink.
  const auto trigger = place<TriggerAtNode>(g, triggerFrame);
  const auto gainVal = place<ConstFloatNode>(g, 0.5f);
  const auto gain = place<GainNode>(g, 1);
  const auto sink = place<CaptureSinkNode>(g, 1);
  g.connect(gainVal.id, gainVal->value(), gain.id, gain->gain());
  g.connect(gain.id, gain->out(), sink.id, sink->in());

  switch (grouping) {
    case Grouping::Flat: {
      const auto blk = place<MidiBlockNode>(g, milestoneClip());
      const auto synth = place<SineSynthNode>(g);
      g.connect(trigger.id, trigger->trigger(), blk.id, blk->trigger());
      g.connect(blk.id, blk->midi(), synth.id, synth->midiIn());
      g.connect(synth.id, synth->out(), gain.id, gain->in());
      break;
    }
    case Grouping::Clip: {
      const ClipSection clip = addMidiClipSupernode(g);
      const auto synth = place<SineSynthNode>(g);
      g.connect(trigger.id, trigger->trigger(), clip.super.id, clip.triggerIn);
      g.connect(clip.super.id, clip.midiOut, synth.id, synth->midiIn());
      g.connect(synth.id, synth->out(), gain.id, gain->in());
      break;
    }
    case Grouping::Instrument: {
      const Instrument inst = addInstrumentSupernode(g);
      g.connect(trigger.id, trigger->trigger(), inst.super.id, inst.triggerIn);
      g.connect(inst.super.id, inst.audioOut, gain.id, gain->in());
      break;
    }
  }
  return sink;
}

// One render of the milestone graph. Built fresh every time because nodes are
// stateful: a second render through the same graph would start from wherever
// the first one ended. The trigger frame is deliberately not a multiple of any
// block size used below.
std::vector<float> renderMilestone(Grouping grouping, std::uint32_t block) {
  Graph g;
  const auto sink = buildMilestoneGraph(g, 1234, grouping);
  Engine e(g, 48000.0, block);
  render(e, 20000, block);
  return sink->samples();
}

} // namespace

TEST_CASE("connect enforces exact type equality") {
  Graph g;
  const auto synth = place<SineSynthNode>(g); // out: audio<1>
  const auto gain2 = place<GainNode>(g, 2);   // in: audio<2>
  const auto gain1 = place<GainNode>(g, 1);
  const auto trig = place<TriggerAtNode>(g, 0); // out: event<trigger>

  // audio<1> -> audio<2>
  CHECK_THROWS(g.connect(synth.id, synth->out(), gain2.id, gain2->in()));
  // event<trigger> -> event<midi>
  CHECK_THROWS(g.connect(trig.id, trig->trigger(), synth.id, synth->midiIn()));
  // event -> value
  CHECK_THROWS(g.connect(trig.id, trig->trigger(), gain1.id, gain1->gain()));
  // audio<1> -> audio<1>
  CHECK_NOTHROW(g.connect(synth.id, synth->out(), gain1.id, gain1->in()));
}

// --- audio subtypes: what a signal is, apart from how wide it is (ADR-0003) ---

namespace {

// Defined here and nowhere else. The kernel compares an audio subtype and never
// learns what it means, and the node library defines none yet.
constexpr std::uint32_t kMel = 7;

// One audio input and one audio output of whatever types a test declares, so a
// spectral stream and a speaker bus need no node library that defines either.
// retype() is a node-owned configuration edit of both ports at once.
class AudioPorts : public Node {
public:
  AudioPorts(Graph& graph, PortType in, PortType out) : Node(graph) {
    in_ = addInput(in, "in");
    out_ = addOutput(out, "out");
  }
  PortId in() const { return in_; }
  PortId out() const { return out_; }
  void retype(Graph& graph, NodeId self, PortType in, PortType out) {
    const std::array changes{PortTypeChange{in_, in}, PortTypeChange{out_, out}};
    setPortTypes(graph, self, changes);
  }
  void process(ProcessContext&) override {}
private:
  PortId in_ = kNoPort, out_ = kNoPort;
};

} // namespace

TEST_CASE("an 80-band spectral stream cannot connect to an 80-channel speaker bus") {
  // The same width, so only the subtype tells them apart. Unspecified audio,
  // subtype 0, is a concrete type rather than a wildcard, on either end.
  Graph g;
  const auto mel = PortType::audio(80, kMel);
  const auto bus = PortType::audio(80);
  const auto spectrum = place<AudioPorts>(g, mel, mel);
  const auto analysis = place<AudioPorts>(g, mel, mel);
  const auto speakers = place<AudioPorts>(g, bus, bus);
  const auto output = place<AudioPorts>(g, bus, bus);

  CHECK_THROWS_AS(g.connect(spectrum.id, spectrum->out(), speakers.id, speakers->in()),
                  std::invalid_argument);
  CHECK_THROWS_AS(g.connect(speakers.id, speakers->out(), analysis.id, analysis->in()),
                  std::invalid_argument);
  CHECK(g.connections().empty());

  CHECK_NOTHROW(g.connect(spectrum.id, spectrum->out(), analysis.id, analysis->in()));
  CHECK_NOTHROW(g.connect(speakers.id, speakers->out(), output.id, output->in()));
}

TEST_CASE("an equal audio subtype at a different width still refuses to connect") {
  Graph g;
  const auto wideType = PortType::audio(80, kMel);
  const auto narrowType = PortType::audio(40, kMel);
  const auto wide = place<AudioPorts>(g, wideType, wideType);
  const auto narrow = place<AudioPorts>(g, narrowType, narrowType);
  CHECK_THROWS_AS(g.connect(wide.id, wide->out(), narrow.id, narrow->in()),
                  std::invalid_argument);
  CHECK_THROWS_AS(g.connect(narrow.id, narrow->out(), wide.id, wide->in()),
                  std::invalid_argument);
  CHECK(g.connections().empty());
}

TEST_CASE("an audio subtype never equals a value or event subtype of the same number") {
  // Equality includes the domain, so each domain numbers its subtypes alone.
  CHECK(PortType::audio(1, subtype::Trigger) != PortType::event(subtype::Trigger));
  CHECK(PortType::audio(1, subtype::Float) != PortType::value(subtype::Float));
  CHECK(PortType::event(subtype::Trigger).channels == 0);
  CHECK(PortType::value(subtype::Float).channels == 0);

  Graph g;
  const auto audio = place<AudioPorts>(g, PortType::audio(1, subtype::Trigger),
                                       PortType::audio(1, subtype::Trigger));
  std::vector<int> log;
  const auto probe = place<OrderProbeNode>(g, 0, log); // event<trigger> both ways
  CHECK_THROWS_AS(g.connect(audio.id, audio->out(), probe.id, probe->in()),
                  std::invalid_argument);
  CHECK_THROWS_AS(g.connect(probe.id, probe->out(), audio.id, audio->in()),
                  std::invalid_argument);
}

TEST_CASE("a supernode face carries a spectral subtype and refuses a speaker bus") {
  // Grouping cannot launder a spectral signal into a speaker bus: the face is
  // the boundary node's own port, so it carries the full type, subtype too.
  Graph parent;
  const auto mel = PortType::audio(80, kMel);
  const auto bus = PortType::audio(80);
  const auto super = place<Supernode>(parent);
  const auto spectrumIn = exposeInput(super->inner(), mel, "spectrum in");
  const auto spectrumOut = exposeOutput(super->inner(), mel, "spectrum out");
  REQUIRE(parent.node(super.id).inputs().size() == 1);
  REQUIRE(parent.node(super.id).outputs().size() == 1);
  CHECK(parent.node(super.id).inputs()[0].type == mel);
  CHECK(parent.node(super.id).outputs()[0].type == mel);

  const auto speakers = place<AudioPorts>(parent, bus, bus);
  const auto analysis = place<AudioPorts>(parent, mel, mel);
  CHECK_THROWS_AS(
      parent.connect(super.id, spectrumOut->port(), speakers.id, speakers->in()),
      std::invalid_argument);
  CHECK_THROWS_AS(
      parent.connect(speakers.id, speakers->out(), super.id, spectrumIn->port()),
      std::invalid_argument);
  CHECK(parent.connections().empty());
  CHECK_NOTHROW(
      parent.connect(super.id, spectrumOut->port(), analysis.id, analysis->in()));
  CHECK_NOTHROW(
      parent.connect(analysis.id, analysis->out(), super.id, spectrumIn->port()));
}

TEST_CASE("a subtype change that would break a wire is refused and changes no type") {
  Graph g;
  const auto mel = PortType::audio(80, kMel);
  const auto bus = PortType::audio(80);
  const auto source = place<AudioPorts>(g, mel, mel);
  const auto sink = place<AudioPorts>(g, mel, mel);
  g.connect(source.id, source->out(), sink.id, sink->in());

  // The same width, so only the subtype breaks the wire. The edit's unwired
  // half, the source's input, is not published either.
  CHECK_THROWS_AS(source->retype(g, source.id, bus, bus), std::invalid_argument);
  CHECK(source->inputs()[0].type == mel);
  CHECK(source->outputs()[0].type == mel);
  CHECK(sink->inputs()[0].type == mel);
  CHECK(sink->outputs()[0].type == mel);
  REQUIRE(g.connections().size() == 1);

  // An edit that keeps the wired port's type is ordinary configuration.
  CHECK_NOTHROW(sink->retype(g, sink.id, mel, bus));
  CHECK(sink->inputs()[0].type == mel);
  CHECK(sink->outputs()[0].type == bus);
}

TEST_CASE("an audio port with no channels throws when declared or reconfigured") {
  // A zero-wide buffer would read as permanent silence instead of failing, so it
  // is an authoring error in every build, wherever a port type enters a node.
  Graph g;
  CHECK_THROWS_AS(g.addNode<AudioPorts>(PortType::audio(0), PortType::audio(1)),
                  std::invalid_argument);
  CHECK_THROWS_AS(g.addNode<AudioPorts>(PortType::audio(1), PortType::audio(0, kMel)),
                  std::invalid_argument);
  CHECK(g.nodeCount() == 0);

  const auto s = place<Supernode>(g);
  CHECK_THROWS_AS(exposeInput(s->inner(), PortType::audio(0), "in"),
                  std::invalid_argument);
  CHECK_THROWS_AS(exposeOutput(s->inner(), PortType::audio(0, kMel), "out"),
                  std::invalid_argument);
  CHECK(s->inputs().empty());
  CHECK(s->outputs().empty());

  // Unwired, so only the width can refuse the edit, and none of it lands.
  const auto node = place<AudioPorts>(g, PortType::audio(2), PortType::audio(2));
  CHECK_THROWS_AS(node->retype(g, node.id, PortType::audio(1), PortType::audio(0)),
                  std::invalid_argument);
  CHECK(node->inputs()[0].type == PortType::audio(2));
  CHECK(node->outputs()[0].type == PortType::audio(2));
  CHECK_NOTHROW(node->retype(g, node.id, PortType::audio(1), PortType::audio(1)));
  CHECK(node->outputs()[0].type == PortType::audio(1));
}

TEST_CASE("a port id names a port on its own node and nowhere else") {
  // A port id is unique across the whole graph, so naming one that belongs to
  // another node is not an off-by-one that lands somewhere plausible — it is
  // an id this node does not have, and the graph says so.
  Graph g;
  const auto a = place<GainNode>(g, 1);
  const auto b = place<GainNode>(g, 1);
  CHECK_THROWS_AS(g.connect(a.id, b->out(), b.id, b->in()), std::invalid_argument);
  CHECK_THROWS_AS(g.connect(a.id, a->out(), b.id, a->in()), std::invalid_argument);
  // An input id offered where an output is wanted is equally not a port here.
  CHECK_THROWS_AS(g.connect(a.id, a->in(), b.id, b->in()), std::invalid_argument);
  CHECK_NOTHROW(g.connect(a.id, a->out(), b.id, b->in()));
}

TEST_CASE("input accepts only one connection; merging is a node's job") {
  Graph g;
  const auto a = place<SineSynthNode>(g);
  const auto b = place<SineSynthNode>(g);
  const auto gain = place<GainNode>(g, 1);
  g.connect(a.id, a->out(), gain.id, gain->in());
  CHECK_THROWS(g.connect(b.id, b->out(), gain.id, gain->in()));
}

TEST_CASE("engine processes in topological order regardless of insertion order") {
  std::vector<int> log;
  Graph g;
  // Insert in reverse dependency order on purpose.
  const auto c = place<OrderProbeNode>(g, 3, log);
  const auto b = place<OrderProbeNode>(g, 2, log);
  const auto a = place<OrderProbeNode>(g, 1, log);
  g.connect(a.id, a->out(), b.id, b->in());
  g.connect(b.id, b->out(), c.id, c->in());

  Engine e(g, 48000.0, 64);
  e.process(64);
  CHECK(log == std::vector<int>{1, 2, 3});
}

TEST_CASE("cycles are rejected at compile time") {
  std::vector<int> log;
  Graph g;
  const auto a = place<OrderProbeNode>(g, 1, log);
  const auto b = place<OrderProbeNode>(g, 2, log);
  g.connect(a.id, a->out(), b.id, b->in());
  g.connect(b.id, b->out(), a.id, a->in());
  CHECK_THROWS(Engine(g, 48000.0, 64));
}

TEST_CASE("event timing is sample-accurate: silence before the trigger, sound after") {
  // Trigger frame deliberately not on a block boundary.
  constexpr std::uint64_t kTrigger = 1234;
  Graph g;
  const auto sink = buildMilestoneGraph(g, kTrigger, Grouping::Flat);
  Engine e(g, 48000.0, 512);
  render(e, 20000, 512);

  const auto& s = sink->samples();
  REQUIRE(s.size() == 20000);
  for (std::uint64_t i = 0; i < kTrigger; ++i) CHECK(s[i] == 0.0f);
  // sin starts at phase 0, so the first frame is 0 — check just after onset.
  bool sounding = false;
  for (std::uint64_t i = kTrigger; i < kTrigger + 8; ++i)
    if (s[i] != 0.0f) sounding = true;
  CHECK(sounding);
}

TEST_CASE("output is independent of block size") {
  // The kernel invariant: block size is dispatch granularity, not a time unit.
  // 331 is prime and shares no structure with 512 or the event offsets.
  CHECK(renderMilestone(Grouping::Flat, 512) == renderMilestone(Grouping::Flat, 331));
}

TEST_CASE("value input is applied (gain) and defaults to zero when unconnected") {
  auto build = [](bool connectGain) {
    Graph g;
    std::vector<MidiBlockNode::Entry> clip = {{0, MidiMsg{0x90, 69, 100}},
      {100000, MidiMsg{0x80, 69, 0}}}; // keep the fixture sounding over its render horizon
    const auto trig = place<TriggerAtNode>(g, 0);
    const auto block = place<MidiBlockNode>(g, std::move(clip));
    const auto synth = place<SineSynthNode>(g);
    const auto gain = place<GainNode>(g, 1);
    const auto sink = place<CaptureSinkNode>(g, 1);
    g.connect(trig.id, trig->trigger(), block.id, block->trigger());
    g.connect(block.id, block->midi(), synth.id, synth->midiIn());
    g.connect(synth.id, synth->out(), gain.id, gain->in());
    g.connect(gain.id, gain->out(), sink.id, sink->in());
    if (connectGain) {
      const auto v = place<ConstFloatNode>(g, 0.25f);
      g.connect(v.id, v->value(), gain.id, gain->gain());
    }
    Engine e(g, 48000.0, 256);
    e.process(256);
    return sink->samples();
  };

  const auto quiet = build(true);
  const auto muted = build(false);
  CHECK(isSounding(quiet));               // gain 0.25 lets signal through
  for (float s : muted) CHECK(s == 0.0f); // unconnected value input reads 0
}

TEST_CASE("unconnected audio input reads silence") {
  Graph g;
  const auto gain = place<GainNode>(g, 1);
  const auto v = place<ConstFloatNode>(g, 1.0f);
  const auto sink = place<CaptureSinkNode>(g, 1);
  g.connect(v.id, v->value(), gain.id, gain->gain());
  g.connect(gain.id, gain->out(), sink.id, sink->in());
  Engine e(g, 48000.0, 128);
  e.process(128);
  for (float s : sink->samples()) CHECK(s == 0.0f);
}

TEST_CASE("Event payload roundtrip and EventBuffer stable ordering") {
  const MidiMsg m{0x90, 60, 100};
  const Event e = Event::make(37, m);
  const auto back = e.as<MidiMsg>();
  CHECK(back.status == 0x90);
  CHECK(back.data1 == 60);
  CHECK(back.data2 == 100);

  EventBuffer buf;
  buf.push(Event::make(50, MidiMsg{0x90, 1, 1}));
  buf.push(Event::make(10, MidiMsg{0x90, 2, 1}));
  buf.push(Event::make(50, MidiMsg{0x90, 3, 1})); // equal offset: keeps push order
  REQUIRE(buf.size() == 3);
  // Read forward-only: EventBuffer exposes no indexed access, deliberately.
  std::vector<std::uint32_t> offsets;
  std::vector<std::uint8_t> notes;
  for (const Event& ev : buf) {
    offsets.push_back(ev.offset);
    notes.push_back(ev.as<MidiMsg>().data1);
  }
  CHECK(offsets == std::vector<std::uint32_t>{10, 50, 50});
  CHECK(notes == std::vector<std::uint8_t>{2, 1, 3});
}

// The payload is sized by the plugin oracle, not by convenience: a conformant
// note must fit inline. These structs are hand-copied field-for-field from VST3's
// ivstevents.h and CLAP's events.h. The static_asserts pin the *copies* — they
// cannot detect an upstream change, only that these have not drifted from the
// sizes ADR-0002 was decided on. Note-off is asserted too: it fitted the original
// 16 bytes when note-on did not, which is how the gap stayed invisible.
TEST_CASE("the event payload carries a conformant plugin note") {
  struct Vst3NoteOn {
    std::int16_t channel;
    std::int16_t pitch;
    float tuning;
    float velocity;
    std::int32_t length;
    std::int32_t noteId;
  };
  struct Vst3NoteOff {
    std::int16_t channel;
    std::int16_t pitch;
    float velocity;
    std::int32_t noteId;
    float tuning;
  };
  struct ClapNote {
    std::int32_t noteId;
    std::int16_t portIndex;
    std::int16_t channel;
    std::int16_t key;
    double velocity;
  };
  static_assert(sizeof(Vst3NoteOn) == 20, "local copy of VST3 NoteOnEvent has drifted");
  static_assert(sizeof(Vst3NoteOff) == 16, "local copy of VST3 NoteOffEvent has drifted");
  static_assert(sizeof(ClapNote) == 24, "local copy of clap_event_note has drifted");
  static_assert(sizeof(ClapNote) <= Event::kInlineBytes,
                "the widest ordinary note in the oracle must fit inline");
  // The asymmetry that hid the bug: only one of a note pair used to fit.
  static_assert(sizeof(Vst3NoteOff) <= 16 && sizeof(Vst3NoteOn) > 16);

  // A non-zero offset, to check make/as carry it rather than clobbering it; it is
  // not part of the payload. Timing itself is exercised by the render tests.
  const Vst3NoteOn vst{2, 60, -12.5f, 0.75f, 4410, 77};
  const Event a = Event::make(97, vst);
  CHECK(a.offset == 97);
  CHECK(a.as<Vst3NoteOn>().pitch == 60);
  CHECK(a.as<Vst3NoteOn>().tuning == -12.5f);
  CHECK(a.as<Vst3NoteOn>().noteId == 77);

  const ClapNote clap{99, 1, 3, 64, 0.5};
  const Event b = Event::make(97, clap);
  CHECK(b.as<ClapNote>().noteId == 99);
  CHECK(b.as<ClapNote>().key == 64);
  CHECK(b.as<ClapNote>().velocity == 0.5);
}

namespace {

class ConstTextNode : public Node {
public:
  ConstTextNode(Graph& graph, std::string text) : Node(graph), text_(std::move(text)) {
    out_ = addOutput(PortType::value(subtype::Text), "text");
  }
  PortId out() const { return out_; }
  void process(ProcessContext& ctx) override { setText(ctx.valueOut(out_), text_); }

private:
  PortId out_ = kNoPort;
  std::string text_;
};

class TextProbeNode : public Node {
public:
  explicit TextProbeNode(Graph& graph) : Node(graph) {
    in_ = addInput(PortType::value(subtype::Text), "in");
  }
  PortId in() const { return in_; }
  void process(ProcessContext& ctx) override { last = getText(ctx.valueIn(in_)); }
  std::string last;

private:
  PortId in_ = kNoPort;
};

} // namespace

TEST_CASE("values are size-free: text of any length flows through the graph") {
  const std::string longText =
      "values are held bytes; the kernel stores them without interpreting, "
      "so a transcript segment fits exactly like a float does";
  Graph g;
  const auto src = place<ConstTextNode>(g, longText);
  const auto probe = place<TextProbeNode>(g);
  g.connect(src.id, src->out(), probe.id, probe->in());
  Engine e(g, 48000.0, 64);
  e.process(64);
  CHECK(probe->last == longText);

  // Unconnected text input reads empty, not garbage.
  Graph g2;
  const auto lone = place<TextProbeNode>(g2);
  Engine e2(g2, 48000.0, 64);
  e2.process(64);
  CHECK(lone->last.empty());
}

TEST_CASE("trigger mid-block anchors content to the exact trigger frame") {
  // Trigger at frame 100 inside a 256-frame block; note-on at content offset 0.
  Graph g;
  std::vector<MidiBlockNode::Entry> clip = {{0, MidiMsg{0x90, 69, 100}},
      {100000, MidiMsg{0x80, 69, 0}}}; // keep the fixture sounding over its render horizon
  const auto trig = place<TriggerAtNode>(g, 100);
  const auto block = place<MidiBlockNode>(g, std::move(clip));
  const auto synth = place<SineSynthNode>(g);
  const auto sink = place<CaptureSinkNode>(g, 1);
  g.connect(trig.id, trig->trigger(), block.id, block->trigger());
  g.connect(block.id, block->midi(), synth.id, synth->midiIn());
  g.connect(synth.id, synth->out(), sink.id, sink->in());
  Engine e(g, 48000.0, 256);
  e.process(256);

  const auto& s = sink->samples();
  for (int i = 0; i < 100; ++i) CHECK(s[i] == 0.0f);
  bool sounding = false;
  for (int i = 100; i < 108; ++i)
    if (s[i] != 0.0f) sounding = true;
  CHECK(sounding);
}

// --- supernodes: authoring seam (the engine still knows nothing of them) ---

TEST_CASE("a supernode's face is derived from its boundary nodes in insertion order") {
  Graph g;
  const auto s = place<Supernode>(g);
  // Directions interleaved on purpose: each face follows its own boundary
  // nodes' order, not the position of those nodes among the inner graph's.
  const auto left = exposeInput(s->inner(), PortType::audio(1), "left");
  const auto notes =
      exposeOutput(s->inner(), PortType::event(subtype::Midi), "notes");
  place<GainNode>(s->inner(), 1); // ordinary node: never on the face
  const auto amount =
      exposeInput(s->inner(), PortType::value(subtype::Float), "amount");

  REQUIRE(s->inputs().size() == 2);
  CHECK(s->inputs()[0].name == "left");
  CHECK(s->inputs()[0].type == PortType::audio(1));
  CHECK(s->inputs()[1].name == "amount");
  CHECK(s->inputs()[1].type == PortType::value(subtype::Float));

  REQUIRE(s->outputs().size() == 1);
  CHECK(s->outputs()[0].name == "notes");
  CHECK(s->outputs()[0].type == PortType::event(subtype::Midi));

  // Order is what a view walks; the id is what a wire names, and it is the
  // backing boundary node's own port id with nothing converted.
  CHECK(s->inputs()[0].id == left->port());
  CHECK(s->inputs()[1].id == amount->port());
  CHECK(s->outputs()[0].id == notes->port());
  CHECK(s->boundaryFor(left->port()) == left.id);
  CHECK(s->boundaryFor(notes->port()) == notes.id);
  CHECK(s->boundaryFor(kNoPort) == kNoNode);
}

TEST_CASE("two boundary nodes in one inner graph have different port ids") {
  // The collision that killed node-scoped ids in ADR-0004: with a counter per
  // node, each of these would call its single port 0, and a face could not
  // name one without naming the other. Graph scope is what rules that out.
  Graph g;
  const auto s = place<Supernode>(g);
  const auto a = exposeInput(s->inner(), PortType::audio(1), "a");
  const auto b = exposeInput(s->inner(), PortType::audio(1), "b");
  CHECK(a->port() != b->port());
  CHECK(s->boundaryFor(a->port()) != s->boundaryFor(b->port()));
}

TEST_CASE("a supernode owns its inner graph, so no two can share one") {
  // Placing a supernode snapshots its contents. Graph is move-only, so an
  // aliased inner graph is not expressible — the snapshot rule is enforced by
  // the type rather than by discipline.
  static_assert(!std::is_copy_constructible_v<Supernode>);
  static_assert(!std::is_copy_assignable_v<Supernode>);

  Graph g;
  const auto s = place<Supernode>(g);
  place<GainNode>(s->inner(), 1);
  const Supernode& readOnly = *s.node;
  CHECK(readOnly.inner().nodeCount() == 1); // the inner graph lives in the supernode
}

TEST_CASE("a supernode's face follows inner-graph edits, even after it joins a graph") {
  Graph parent;
  const auto s = place<Supernode>(parent);
  exposeInput(s->inner(), PortType::audio(1), "in");

  // Read back through Node&: the derived face must survive the abstraction.
  REQUIRE(parent.node(s.id).inputs().size() == 1);
  CHECK(parent.node(s.id).outputs().empty());

  // Nothing is frozen by joining a graph — a primitive's ports are not, and a
  // derived face is as editable as the inner graph it is read from.
  exposeOutput(s->inner(), PortType::audio(1), "out");
  CHECK(parent.node(s.id).inputs().size() == 1);
  REQUIRE(parent.node(s.id).outputs().size() == 1);
  CHECK(parent.node(s.id).outputs()[0].name == "out");
}

TEST_CASE("connections to a supernode type-check like connections to any node") {
  Graph parent;
  const auto super = place<Supernode>(parent);
  const auto inB = exposeInput(super->inner(), PortType::audio(1), "in");
  const auto outB = exposeOutput(super->inner(), PortType::audio(1), "out");
  const auto synth = place<SineSynthNode>(parent); // out: audio<1>
  const auto gain2 = place<GainNode>(parent, 2);   // in: audio<2>
  const auto sink = place<CaptureSinkNode>(parent, 1);

  // audio<1> -> exposed audio<1>
  CHECK_NOTHROW(parent.connect(synth.id, synth->out(), super.id, inB->port()));
  CHECK_NOTHROW(parent.connect(super.id, outB->port(), sink.id, sink->in()));
  // Authoring-path convention: these are invalid_argument, not just any throw.
  CHECK_THROWS_AS(parent.connect(super.id, outB->port(), gain2.id, gain2->in()),
                  std::invalid_argument);
  // A port id that is not on this face at all.
  CHECK_THROWS_AS(parent.connect(synth.id, synth->out(), super.id, sink->in()),
                  std::invalid_argument);
  // Already connected.
  CHECK_THROWS_AS(parent.connect(synth.id, synth->out(), super.id, inB->port()),
                  std::invalid_argument);
}

TEST_CASE("a boundary node presents the mirror-image port inside the inner graph") {
  // An exposed input feeds the inner graph, so inside it reads as an output;
  // an exposed output is fed by the inner graph, so inside it reads as an input.
  Graph g;
  const auto s = place<Supernode>(g);
  Graph& inner = s->inner();
  const auto in = exposeInput(inner, PortType::audio(1), "in");
  const auto out = exposeOutput(inner, PortType::audio(1), "out");
  const auto gainA = place<GainNode>(inner, 1);
  const auto gainB = place<GainNode>(inner, 1);

  CHECK(inner.node(in.id).inputs().empty());
  REQUIRE(inner.node(in.id).outputs().size() == 1);
  CHECK(inner.node(in.id).outputs()[0].type == PortType::audio(1));
  CHECK(inner.node(out.id).outputs().empty());
  REQUIRE(inner.node(out.id).inputs().size() == 1);
  // Inside and outside are the one port, so they carry the one id.
  CHECK(inner.node(in.id).outputs()[0].id == in->port());
  CHECK(inner.node(out.id).inputs()[0].id == out->port());

  // Exposure is ordinary wiring, so the ordinary rules apply inside: exact
  // type equality, and fan-out from a boundary just like from any output.
  CHECK_NOTHROW(inner.connect(in.id, in->port(), gainA.id, gainA->in()));
  CHECK_NOTHROW(inner.connect(in.id, in->port(), gainB.id, gainB->in()));
  CHECK_NOTHROW(inner.connect(gainA.id, gainA->out(), out.id, out->port()));
  CHECK_THROWS_AS(inner.connect(in.id, in->port(), gainB.id, gainB->gain()),
                  std::invalid_argument); // audio<1> -> value<float>
}

// --- supernodes: flattened by resolution at compile ---

namespace {

// A gain wrapped in a supernode: audio crosses the boundary in both
// directions, the value source stays inside.
struct GainSection {
  Placed<Supernode> super;
  PortId audioIn = kNoPort;
  PortId audioOut = kNoPort;
};

GainSection addGainSupernode(Graph& parent, float gain) {
  const auto s = place<Supernode>(parent);
  Graph& in = s->inner();
  const auto inB = exposeInput(in, PortType::audio(1), "in");
  const auto g = place<GainNode>(in, 1);
  const auto v = place<ConstFloatNode>(in, gain);
  const auto outB = exposeOutput(in, PortType::audio(1), "out");
  in.connect(inB.id, inB->port(), g.id, g->in());
  in.connect(v.id, v->value(), g.id, g->gain());
  in.connect(g.id, g->out(), outB.id, outB->port());
  return {s, inB->port(), outB->port()};
}

// A sounding source: trigger -> midi block -> synth. 313 is prime, so the
// note onset cannot land on a block boundary by accident.
Placed<SineSynthNode> addSource(Graph& g) {
  std::vector<MidiBlockNode::Entry> clip = {{0, MidiMsg{0x90, 69, 100}},
      {100000, MidiMsg{0x80, 69, 0}}}; // keep the fixture sounding over its render horizon
  const auto trig = place<TriggerAtNode>(g, 313);
  const auto blk = place<MidiBlockNode>(g, std::move(clip));
  const auto synth = place<SineSynthNode>(g);
  g.connect(trig.id, trig->trigger(), blk.id, blk->trigger());
  g.connect(blk.id, blk->midi(), synth.id, synth->midiIn());
  return synth;
}

} // namespace

TEST_CASE("grouping a gain into a supernode does not change a single sample") {
  // Same signal path twice: gain applied directly, and gain reached only
  // through a supernode's boundaries.
  auto renderWith = [](bool grouped) {
    Graph g;
    const auto synth = addSource(g);
    const auto sink = place<CaptureSinkNode>(g, 1);
    if (grouped) {
      const GainSection section = addGainSupernode(g, 0.5f);
      g.connect(synth.id, synth->out(), section.super.id, section.audioIn);
      g.connect(section.super.id, section.audioOut, sink.id, sink->in());
    } else {
      const auto gain = place<GainNode>(g, 1);
      const auto v = place<ConstFloatNode>(g, 0.5f);
      g.connect(synth.id, synth->out(), gain.id, gain->in());
      g.connect(v.id, v->value(), gain.id, gain->gain());
      g.connect(gain.id, gain->out(), sink.id, sink->in());
    }
    Engine e(g, 48000.0, 256);
    render(e, 2000, 256);
    return sink->samples();
  };

  const auto flat = renderWith(false);
  REQUIRE(isSounding(flat)); // otherwise the comparison below is vacuous
  CHECK(renderWith(true) == flat);
}

namespace {

// Ways an inner value input can end up with nothing feeding it — which the
// node inside must not be able to tell apart.
enum class GainValue { FromOutside, ExposedButUnconnected, NotExposed };

// A gain whose audio *and* value inputs cross the boundary.
std::vector<float> renderExposedGain(GainValue mode) {
  Graph g;
  const auto synth = addSource(g);

  const auto super = place<Supernode>(g);
  Graph& in = super->inner();
  const auto inB = exposeInput(in, PortType::audio(1), "in");
  const auto gain = place<GainNode>(in, 1);
  const auto outB = exposeOutput(in, PortType::audio(1), "out");
  in.connect(inB.id, inB->port(), gain.id, gain->in());
  in.connect(gain.id, gain->out(), outB.id, outB->port());
  PortId exposedGain = kNoPort;
  if (mode != GainValue::NotExposed) {
    const auto vB = exposeInput(in, PortType::value(subtype::Float), "gain");
    in.connect(vB.id, vB->port(), gain.id, gain->gain());
    exposedGain = vB->port();
  }

  const auto sink = place<CaptureSinkNode>(g, 1);
  g.connect(synth.id, synth->out(), super.id, inB->port());
  g.connect(super.id, outB->port(), sink.id, sink->in());
  if (mode == GainValue::FromOutside) {
    const auto v = place<ConstFloatNode>(g, 0.25f);
    g.connect(v.id, v->value(), super.id, exposedGain);
  }

  Engine e(g, 48000.0, 256);
  render(e, 2000, 256);
  return sink->samples();
}

// A synth wrapped in a supernode: midi crosses inbound, audio outbound.
struct SynthSection {
  Placed<Supernode> super;
  PortId midiIn = kNoPort;
  PortId audioOut = kNoPort;
};

SynthSection addSynthSupernode(Graph& parent) {
  const auto s = place<Supernode>(parent);
  Graph& in = s->inner();
  const auto inB = exposeInput(in, PortType::event(subtype::Midi), "midi");
  const auto synth = place<SineSynthNode>(in);
  const auto outB = exposeOutput(in, PortType::audio(1), "out");
  in.connect(inB.id, inB->port(), synth.id, synth->midiIn());
  in.connect(synth.id, synth->out(), outB.id, outB->port());
  return {s, inB->port(), outB->port()};
}

} // namespace

TEST_CASE("a value crosses a supernode boundary; an unfed one still reads zero") {
  Graph flatG;
  const auto synth = addSource(flatG);
  const auto gain = place<GainNode>(flatG, 1);
  const auto v = place<ConstFloatNode>(flatG, 0.25f);
  const auto sink = place<CaptureSinkNode>(flatG, 1);
  flatG.connect(synth.id, synth->out(), gain.id, gain->in());
  flatG.connect(v.id, v->value(), gain.id, gain->gain());
  flatG.connect(gain.id, gain->out(), sink.id, sink->in());
  Engine e(flatG, 48000.0, 256);
  render(e, 2000, 256);
  const auto flat = sink->samples();

  REQUIRE(isSounding(flat));
  CHECK(renderExposedGain(GainValue::FromOutside) == flat);

  // An exposed input nobody connected and an inner input nobody exposed are
  // the same thing to the node inside: a value port reading zero, so silence.
  for (float s : renderExposedGain(GainValue::ExposedButUnconnected)) CHECK(s == 0.0f);
  for (float s : renderExposedGain(GainValue::NotExposed)) CHECK(s == 0.0f);
}

TEST_CASE("an event crossing a supernode boundary keeps its exact frame") {
  // 1009 is prime, so the onset cannot land on a block boundary by accident.
  constexpr std::uint64_t kOnset = 1009;
  Graph g;
  std::vector<MidiBlockNode::Entry> clip = {{0, MidiMsg{0x90, 69, 100}},
      {100000, MidiMsg{0x80, 69, 0}}}; // keep the fixture sounding over its render horizon
  const auto trig = place<TriggerAtNode>(g, kOnset);
  const auto blk = place<MidiBlockNode>(g, std::move(clip));
  const SynthSection section = addSynthSupernode(g);
  const auto sink = place<CaptureSinkNode>(g, 1);
  g.connect(trig.id, trig->trigger(), blk.id, blk->trigger());
  g.connect(blk.id, blk->midi(), section.super.id, section.midiIn);
  g.connect(section.super.id, section.audioOut, sink.id, sink->in());
  Engine e(g, 48000.0, 256);
  render(e, 2000, 256);

  const auto& s = sink->samples();
  REQUIRE(s.size() == 2000);
  // Silent up to and including the onset frame — the note starts at phase 0,
  // so that frame is legitimately zero — then sounding immediately after.
  // A boundary that shifted the event by even one block would break this.
  for (std::uint64_t i = 0; i <= kOnset; ++i) CHECK(s[i] == 0.0f);
  bool sounding = false;
  for (std::uint64_t i = kOnset + 1; i < kOnset + 9; ++i)
    if (s[i] != 0.0f) sounding = true;
  CHECK(sounding);
}

TEST_CASE("one exposed input feeds several inner consumers") {
  // Fan-out is free inside a boundary exactly as it is outside: one
  // connection into the supernode, resolved into two.
  Graph g;
  const auto synth = addSource(g);

  const auto super = place<Supernode>(g);
  Graph& in = super->inner();
  const auto inB = exposeInput(in, PortType::audio(1), "in");
  const auto unity = place<GainNode>(in, 1);
  const auto unityV = place<ConstFloatNode>(in, 1.0f);
  const auto half = place<GainNode>(in, 1);
  const auto halfV = place<ConstFloatNode>(in, 0.5f);
  const auto outA = exposeOutput(in, PortType::audio(1), "a");
  const auto outB = exposeOutput(in, PortType::audio(1), "b");
  in.connect(inB.id, inB->port(), unity.id, unity->in());
  in.connect(inB.id, inB->port(), half.id, half->in()); // same exposed input again
  in.connect(unityV.id, unityV->value(), unity.id, unity->gain());
  in.connect(halfV.id, halfV->value(), half.id, half->gain());
  in.connect(unity.id, unity->out(), outA.id, outA->port());
  in.connect(half.id, half->out(), outB.id, outB->port());

  const auto sinkA = place<CaptureSinkNode>(g, 1);
  const auto sinkB = place<CaptureSinkNode>(g, 1);
  g.connect(synth.id, synth->out(), super.id, inB->port());
  g.connect(super.id, outA->port(), sinkA.id, sinkA->in());
  g.connect(super.id, outB->port(), sinkB.id, sinkB->in());

  Engine e(g, 48000.0, 256);
  render(e, 2000, 256);
  const auto& a = sinkA->samples();
  const auto& b = sinkB->samples();

  REQUIRE(a.size() == b.size());
  REQUIRE(isSounding(a)); // otherwise both branches are trivially equal
  // Both consumers saw the same samples. 0.5 and 1.0 are exact in binary, so
  // this is an equality and not an approximation.
  for (std::size_t i = 0; i < a.size(); ++i) CHECK(b[i] == 0.5f * a[i]);
}

TEST_CASE("an inner port nobody exposed reads its domain default") {
  // Audio reads silence and event reads empty, exactly as an unconnected
  // input does in a flat graph. (The value case is covered above.)
  Graph g;
  const auto super = place<Supernode>(g);
  Graph& in = super->inner();
  const auto gain = place<GainNode>(in, 1); // audio in: unexposed
  const auto gv = place<ConstFloatNode>(in, 1.0f);
  const auto synth = place<SineSynthNode>(in); // events unexposed
  const auto outGain = exposeOutput(in, PortType::audio(1), "gain");
  const auto outSynth = exposeOutput(in, PortType::audio(1), "synth");
  in.connect(gv.id, gv->value(), gain.id, gain->gain());
  in.connect(gain.id, gain->out(), outGain.id, outGain->port());
  in.connect(synth.id, synth->out(), outSynth.id, outSynth->port());

  const auto sinkGain = place<CaptureSinkNode>(g, 1);
  const auto sinkSynth = place<CaptureSinkNode>(g, 1);
  g.connect(super.id, outGain->port(), sinkGain.id, sinkGain->in());
  g.connect(super.id, outSynth->port(), sinkSynth.id, sinkSynth->in());

  Engine e(g, 48000.0, 256);
  render(e, 1024, 256);
  for (float s : sinkGain->samples()) CHECK(s == 0.0f);  // gain of 1.0 on silence
  for (float s : sinkSynth->samples()) CHECK(s == 0.0f); // a synth sent no note
}

// --- the sprint demo: the milestone graph, grouped ---

TEST_CASE("the milestone graph renders bit-identically with its clip section grouped") {
  // The sprint's falsifiable claim: grouping is grouping, so the whole
  // 20000-frame render must match sample for sample, not approximately.
  const auto flat = renderMilestone(Grouping::Flat, 512);
  REQUIRE(flat.size() == 20000);
  REQUIRE(isSounding(flat));

  CHECK(renderMilestone(Grouping::Clip, 512) == flat);
}

TEST_CASE("a supernode nested inside a supernode renders bit-identically to flat") {
  // Nesting is the same claim applied to itself: if grouping changes nothing,
  // grouping a group changes nothing either.
  CHECK(renderMilestone(Grouping::Instrument, 512) ==
        renderMilestone(Grouping::Flat, 512));
}

TEST_CASE("output stays independent of block size with supernodes in the graph") {
  // Block size is dispatch granularity, never a time unit, and a boundary is
  // the obvious place for one to leak into timing. 331 is prime, so it shares
  // no structure with 512, with the trigger frame, or with the clip's offsets.
  const auto grouped = renderMilestone(Grouping::Instrument, 331);
  REQUIRE(isSounding(grouped));
  CHECK(renderMilestone(Grouping::Instrument, 512) == grouped);
  // Against flat at 331 too, not only at 512: a boundary that shifted timing
  // by the same amount at both grouped block sizes would satisfy the check
  // above while still making grouping audible.
  CHECK(renderMilestone(Grouping::Flat, 331) == grouped);
}

// --- node identity: a node id is an id, not a position ---

TEST_CASE("deleting an unwired node leaves every surviving wire on the same nodes") {
  // The claim at the node level. Under positional identity, removing the
  // node that was added first slides every later node down one, so the wire
  // that said "node 4" would find itself reading node 5 — silently, and the
  // render would change. Ids make the removal inaudible.
  auto renderWith = [](bool removeSpare) {
    Graph g;
    // Added first on purpose: it is the one whose removal would shift the rest.
    const auto spare = place<ConstFloatNode>(g, 0.9f);
    const auto synth = addSource(g);
    const auto v = place<ConstFloatNode>(g, 0.25f);
    const auto gain = place<GainNode>(g, 1);
    const auto sink = place<CaptureSinkNode>(g, 1);
    g.connect(synth.id, synth->out(), gain.id, gain->in());
    g.connect(v.id, v->value(), gain.id, gain->gain());
    g.connect(gain.id, gain->out(), sink.id, sink->in());
    if (removeSpare) g.removeNode(spare.id);

    Engine e(g, 48000.0, 256);
    render(e, 2003, 256); // prime: the note onset cannot align with a block
    return sink->samples();
  };

  const auto kept = renderWith(false);
  REQUIRE(isSounding(kept)); // otherwise the comparison below is vacuous
  CHECK(renderWith(true) == kept);
}

TEST_CASE("node ids are not reused, so a deleted node's id resolves to nothing") {
  Graph g;
  const auto a = place<GainNode>(g, 1);
  const auto b = place<GainNode>(g, 1);
  CHECK(a.id != b.id);

  g.removeNode(a.id);
  CHECK_THROWS_AS(g.node(a.id), std::invalid_argument);
  CHECK_THROWS_AS(g.removeNode(a.id), std::invalid_argument);

  // The next node must not inherit the dead id: a stale reference has to fail
  // rather than quietly address whatever was added after it.
  const auto c = place<GainNode>(g, 1);
  CHECK(c.id != a.id);
  CHECK(c.id != b.id);
  REQUIRE(g.nodeCount() == 2);
  CHECK(g.nodeIds() == std::vector<NodeId>{b.id, c.id}); // insertion order, minus a
}

TEST_CASE("deleting a node that still carries a wire throws; unwiring first succeeds") {
  Graph g;
  const auto synth = place<SineSynthNode>(g);
  const auto v = place<ConstFloatNode>(g, 0.5f);
  const auto gain = place<GainNode>(g, 1);
  const auto spare = place<GainNode>(g, 1);
  g.connect(synth.id, synth->out(), gain.id, gain->in());
  g.connect(v.id, v->value(), gain.id, gain->gain());

  // RESTRICT: the kernel never decides what the author meant by deleting a
  // wired node, the same argument that already rejected fan-in.
  CHECK_THROWS_AS(g.removeNode(gain.id), std::invalid_argument);  // two wires in
  CHECK_THROWS_AS(g.removeNode(synth.id), std::invalid_argument); // one wire out
  CHECK_NOTHROW(g.removeNode(spare.id));                          // no wires at all

  // connectionsOf() is what makes RESTRICT usable: it reports exactly the
  // wires deletion would refuse over, at either end of the node.
  const auto touching = g.connectionsOf(gain.id);
  REQUIRE(touching.size() == 2);
  CHECK(touching[0].srcNode == synth.id);
  CHECK(touching[0].dstNode == gain.id);
  CHECK(touching[0].dstPort == gain->in());
  CHECK(touching[1].srcNode == v.id);
  CHECK(touching[1].dstPort == gain->gain());

  // The other end counts too: synth carries no inbound wire, and is still
  // undeletable because one leaves it.
  const auto leaving = g.connectionsOf(synth.id);
  REQUIRE(leaving.size() == 1);
  CHECK(leaving[0].srcNode == synth.id);
  CHECK(leaving[0].dstNode == gain.id);

  for (const Connection& c : touching) g.disconnect(c.dstNode, c.dstPort);
  CHECK(g.connectionsOf(gain.id).empty());
  CHECK_NOTHROW(g.removeNode(gain.id));
  CHECK_NOTHROW(g.removeNode(synth.id));
  CHECK(g.connections().empty());
}

TEST_CASE("disconnecting an input frees it, and the render follows the new wire") {
  auto renderWith = [](bool rewire) {
    Graph g;
    const auto synth = addSource(g);
    const auto loud = place<ConstFloatNode>(g, 1.0f);
    const auto quiet = place<ConstFloatNode>(g, 0.5f);
    const auto gain = place<GainNode>(g, 1);
    const auto sink = place<CaptureSinkNode>(g, 1);
    g.connect(synth.id, synth->out(), gain.id, gain->in());
    g.connect(loud.id, loud->value(), gain.id, gain->gain());
    g.connect(gain.id, gain->out(), sink.id, sink->in());
    if (rewire) {
      // One connection per input still holds, so rewiring means unwiring first.
      CHECK_THROWS_AS(g.connect(quiet.id, quiet->value(), gain.id, gain->gain()),
                      std::invalid_argument);
      g.disconnect(gain.id, gain->gain());
      CHECK_THROWS_AS(g.disconnect(gain.id, gain->gain()),
                      std::invalid_argument); // already gone
      g.connect(quiet.id, quiet->value(), gain.id, gain->gain());
    }
    Engine e(g, 48000.0, 256);
    render(e, 2003, 256);
    return sink->samples();
  };

  const auto full = renderWith(false);
  REQUIRE(isSounding(full));
  const auto halved = renderWith(true);
  REQUIRE(halved.size() == full.size());
  // 0.5 and 1.0 are exact in binary, so this is equality, not approximation.
  for (std::size_t i = 0; i < full.size(); ++i) CHECK(halved[i] == 0.5f * full[i]);
}

TEST_CASE("moving a graph keeps its ids minting where they left off") {
  // Why the id source is allocated apart from the graph: Graph is movable and
  // a Supernode holds one by value, so the source has to survive a move with
  // its address and its high-water mark intact. Every node in the graph holds
  // that address, so a move that relocated it would break port minting too.
  Graph a;
  const auto gain = place<GainNode>(a, 1);
  const auto v = place<ConstFloatNode>(a, 0.5f);
  a.connect(v.id, v->value(), gain.id, gain->gain());

  Graph b = std::move(a);
  REQUIRE(b.nodeCount() == 2);
  CHECK(b.node(gain.id).inputs().size() == 2);
  REQUIRE(b.connections().size() == 1);
  CHECK(b.connections()[0].dstNode == gain.id);

  const auto fresh = place<GainNode>(b, 1);
  CHECK(fresh.id != gain.id);
  CHECK(fresh.id != v.id);
  CHECK(fresh->in() != gain->in());

  // The graph that was moved from is empty, not sourceless: it still mints.
  CHECK(a.nodeCount() == 0);
  CHECK(a.connections().empty());
  CHECK_NOTHROW(a.addNode<GainNode>(1));
}

// --- port identity: a port id is an id, not a position ---

TEST_CASE("one counter mints every id in a graph, so no two ever name the same thing") {
  Graph g;
  const auto gain = place<GainNode>(g, 1);
  const auto sum = place<WeightedSumNode>(g, std::vector<float>{1.0f, 2.0f, 4.0f});
  std::vector<std::uint32_t> all = {gain.id,     gain->in(),  gain->gain(), gain->out(),
                                    sum.id,      sum->in(0),  sum->in(1),   sum->in(2),
                                    sum->out()};
  CHECK(std::find(all.begin(), all.end(), kNoNode) == all.end()); // never minted
  std::sort(all.begin(), all.end());
  CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());
}

TEST_CASE("deleting a port leaves every surviving wire on the port it named") {
  // The claim, at the port level, and the demo of the whole effort. Under
  // positional identity, deleting the first of four inputs slides the rest
  // down one, so the wire that said "input 2" would land on what used to be
  // input 3 — a different weight, silently, and the render would change.
  //
  // 1/2/4/8 are exact in binary, so every comparison below is equality.
  auto renderWith = [](std::size_t wireTo, bool dropFirst) {
    Graph g;
    const auto synth = addSource(g);
    const auto sum = place<WeightedSumNode>(g, std::vector<float>{1.0f, 2.0f, 4.0f, 8.0f});
    const auto sink = place<CaptureSinkNode>(g, 1);
    g.connect(synth.id, synth->out(), sum.id, sum->in(wireTo));
    g.connect(sum.id, sum->out(), sink.id, sink->in());
    if (dropFirst) g.removePort(sum.id, sum->in(0));
    Engine e(g, 48000.0, 256);
    render(e, 2003, 256); // prime: the note onset cannot align with a block
    return sink->samples();
  };

  const auto kept = renderWith(2, false);
  REQUIRE(isSounding(kept)); // otherwise the comparisons below are vacuous
  // Not vacuous either way: the inputs are told apart in the output, so a wire
  // that slid one place — which is exactly what a position would have done —
  // is a render this comparison catches.
  CHECK(renderWith(3, false) != kept);
  CHECK(renderWith(2, true) == kept);
}

TEST_CASE("deleting a port leaves every other port's id untouched") {
  Graph g;
  const auto sum = place<WeightedSumNode>(g, std::vector<float>{1.0f, 2.0f, 4.0f, 8.0f});
  const std::vector<PortId> before = {sum->in(0), sum->in(1), sum->in(2), sum->in(3)};
  const PortId out = sum->out();

  g.removePort(sum.id, before[1]);

  // The node's own members are one shorter and otherwise unchanged: the ports
  // after the deleted one slid a position, and kept their ids doing it.
  REQUIRE(sum->inputCount() == 3);
  CHECK(sum->in(0) == before[0]);
  CHECK(sum->in(1) == before[2]);
  CHECK(sum->in(2) == before[3]);
  CHECK(sum->out() == out);

  const auto ins = g.node(sum.id).inputs();
  REQUIRE(ins.size() == 3);
  CHECK(ins[0].id == before[0]);
  CHECK(ins[1].id == before[2]);
  CHECK(ins[2].id == before[3]);

  // The dead id is not recycled, and naming it again fails rather than
  // resolving to whatever took its place in the list.
  const PortId fresh = sum->addWeightedInput(16.0f);
  CHECK(fresh != before[1]);
  CHECK_THROWS_AS(g.removePort(sum.id, before[1]), std::invalid_argument);
}

TEST_CASE("deleting a wired port throws; connectionsOf says what to unwire first") {
  Graph g;
  const auto synth = addSource(g);
  const auto v = place<ConstFloatNode>(g, 0.5f);
  const auto gain = place<GainNode>(g, 1);
  const auto sink = place<CaptureSinkNode>(g, 1);
  g.connect(synth.id, synth->out(), gain.id, gain->in());
  g.connect(v.id, v->value(), gain.id, gain->gain());
  g.connect(gain.id, gain->out(), sink.id, sink->in());

  // RESTRICT, one rule for all three kinds of deletion: a port with a wire on
  // it does not go, at either end of the wire.
  CHECK_THROWS_AS(g.removePort(gain.id, gain->in()), std::invalid_argument);
  CHECK_THROWS_AS(g.removePort(gain.id, gain->gain()), std::invalid_argument);
  CHECK_THROWS_AS(g.removePort(gain.id, gain->out()), std::invalid_argument);
  CHECK_THROWS_AS(g.removePort(gain.id, kNoPort), std::invalid_argument); // no such port

  // The port-level query reports exactly those wires, where the node-level one
  // reports every wire on the node.
  const auto onIn = g.connectionsOf(gain.id, gain->in());
  REQUIRE(onIn.size() == 1);
  CHECK(onIn[0].srcNode == synth.id);
  CHECK(onIn[0].dstPort == gain->in());
  const auto onOut = g.connectionsOf(gain.id, gain->out());
  REQUIRE(onOut.size() == 1);
  CHECK(onOut[0].dstNode == sink.id);
  CHECK(g.connectionsOf(gain.id).size() == 3);

  // Unwire first and the same deletion succeeds — on a node that keeps its own
  // members in step with it.
  const auto sum = place<WeightedSumNode>(g, std::vector<float>{1.0f, 2.0f});
  const PortId wired = sum->in(0);
  const PortId spare = sum->in(1);
  g.connect(synth.id, synth->out(), sum.id, wired);
  CHECK(g.connectionsOf(sum.id, spare).empty());
  CHECK_THROWS_AS(g.removePort(sum.id, wired), std::invalid_argument);
  g.disconnect(sum.id, wired);
  CHECK(g.connectionsOf(sum.id, wired).empty());
  CHECK_NOTHROW(g.removePort(sum.id, wired));
  REQUIRE(sum->inputCount() == 1);
  CHECK(sum->in(0) == spare);
}

TEST_CASE("a node can gain a port while it sits wired in the graph") {
  // Nothing is frozen by joining a graph. The new port is an ordinary port:
  // connectable, type-checked, and audible in the render.
  auto renderWith = [](bool addSecond) {
    Graph g;
    const auto synth = addSource(g);
    const auto sum = place<WeightedSumNode>(g, std::vector<float>{1.0f});
    const auto sink = place<CaptureSinkNode>(g, 1);
    g.connect(synth.id, synth->out(), sum.id, sum->in(0));
    g.connect(sum.id, sum->out(), sink.id, sink->in());
    if (addSecond) {
      // Declared after the node was placed, wired, and read from.
      const PortId late = sum->addWeightedInput(2.0f);
      CHECK(g.node(sum.id).inputs().size() == 2);
      CHECK_THROWS_AS(g.connect(synth.id, synth->out(), sum.id, kNoPort),
                      std::invalid_argument);
      g.connect(synth.id, synth->out(), sum.id, late); // fan-out: one source, twice
    }
    Engine e(g, 48000.0, 256);
    render(e, 2003, 256);
    return sink->samples();
  };

  const auto one = renderWith(false);
  const auto both = renderWith(true);
  REQUIRE(isSounding(one));
  REQUIRE(both.size() == one.size());
  // Weights 1 and 2 over the same source: each sample is exactly three times
  // what the single wire gave.
  for (std::size_t i = 0; i < one.size(); ++i) CHECK(both[i] == 3.0f * one[i]);
}

// --- the supernode face: an outer port id is its boundary node's port id ---

TEST_CASE("a derived port is not removable as a port; the boundary node is the port") {
  Graph g;
  const auto super = place<Supernode>(g);
  const auto b = exposeInput(super->inner(), PortType::audio(1), "in");

  // A supernode's face is its boundary nodes, so it is not the graph holding
  // the supernode that removes one of its ports.
  CHECK_THROWS_AS(g.removePort(super.id, b->port()), std::invalid_argument);
  // Nor is a boundary node's own port removable: it *is* the exposure.
  CHECK_THROWS_AS(super->inner().removePort(b.id, b->port()), std::invalid_argument);

  // Removing the boundary node is what removes the port, and the face follows.
  CHECK(g.node(super.id).inputs().size() == 1);
  super->inner().removeNode(b.id);
  CHECK(g.node(super.id).inputs().empty());
}

namespace {

// One lane across a supernode's face: an exposed input feeding a weighted sum,
// feeding an exposed output. The weight makes the lane audible as itself, so
// which lane a wire reaches shows in the render.
struct Lane {
  Placed<BoundaryNode> in;
  Placed<WeightedSumNode> sum;
  Placed<BoundaryNode> out;
};

Lane addLane(Graph& inner, float weight, const std::string& name) {
  const auto inB = exposeInput(inner, PortType::audio(1), "in " + name);
  const auto sum = place<WeightedSumNode>(inner, std::vector<float>{weight});
  const auto outB = exposeOutput(inner, PortType::audio(1), "out " + name);
  inner.connect(inB.id, inB->port(), sum.id, sum->in(0));
  inner.connect(sum.id, sum->out(), outB.id, outB->port());
  return {inB, sum, outB};
}

// Unwires a lane inside and removes its two boundary nodes, as an author
// removing one exposed pair would: the inner graph's own RESTRICT rule wants
// the boundary nodes unwired first.
void dropLane(Graph& inner, const Lane& lane) {
  inner.disconnect(lane.sum.id, lane.sum->in(0));
  inner.disconnect(lane.out.id, lane.out->port());
  inner.removeNode(lane.in.id);
  inner.removeNode(lane.out.id);
}

// A supernode with three lanes, of weight 1, 2 and 4 in face order. The
// weights are exact in binary, so renders compare by equality.
struct LanedSupernode {
  Placed<Supernode> super;
  std::vector<Lane> lanes;
};

LanedSupernode addLanedSupernode(Graph& parent) {
  LanedSupernode s{place<Supernode>(parent), {}};
  for (const float weight : {1.0f, 2.0f, 4.0f})
    s.lanes.push_back(addLane(s.super->inner(), weight, std::to_string(s.lanes.size())));
  return s;
}

} // namespace

TEST_CASE("deleting an earlier boundary node leaves a later outer wire on its own lane") {
  // The claim at the supernode face, and the demo that retires sprint 2's
  // recorded fragility (ADR-0001): an outer wire used to name a face
  // *position*, so removing an earlier boundary node slid every later port
  // down one and the wire into the second lane would have reached the third.
  // It names the backing boundary node's port id instead, so it stays put.
  auto renderWith = [](std::size_t lane, bool dropFirst) {
    Graph g;
    const auto synth = addSource(g);
    const LanedSupernode s = addLanedSupernode(g);
    const auto sink = place<CaptureSinkNode>(g, 1);
    // Wired by the ids the supernode's own face shows, as a view walking it
    // in display order would hand them to a wiring gesture.
    const Node& face = g.node(s.super.id);
    const PortId wiredIn = face.inputs()[lane].id;
    const PortId wiredOut = face.outputs()[lane].id;
    g.connect(synth.id, synth->out(), s.super.id, wiredIn);
    g.connect(s.super.id, wiredOut, sink.id, sink->in());
    if (dropFirst) {
      dropLane(s.super->inner(), s.lanes[0]);
      // Both wired ports slid one place on the face, and kept their ids.
      REQUIRE(face.inputs().size() == 2);
      REQUIRE(face.outputs().size() == 2);
      CHECK(face.inputs()[lane - 1].id == wiredIn);
      CHECK(face.outputs()[lane - 1].id == wiredOut);
    }
    Engine e(g, 48000.0, 256);
    render(e, 2003, 256);
    return sink->samples();
  };

  const auto kept = renderWith(1, false);
  REQUIRE(isSounding(kept)); // otherwise the comparisons below are vacuous
  // The lane a positional wire would have slid onto renders differently, so
  // the comparison after it can fail.
  CHECK(renderWith(2, false) != kept);
  CHECK(renderWith(1, true) == kept);
}

TEST_CASE("a boundary node wired inside its supernode cannot be deleted until unwired") {
  // The inner graph's ordinary RESTRICT rule, applied to a boundary node like
  // any other node: a wire leaving it counts, and so does one arriving.
  Graph g;
  const auto super = place<Supernode>(g);
  Graph& in = super->inner();
  const auto inB = exposeInput(in, PortType::audio(1), "in");
  const auto gain = place<GainNode>(in, 1);
  const auto outB = exposeOutput(in, PortType::audio(1), "out");
  in.connect(inB.id, inB->port(), gain.id, gain->in());
  in.connect(gain.id, gain->out(), outB.id, outB->port());

  CHECK_THROWS_AS(in.removeNode(inB.id), std::invalid_argument);
  CHECK_THROWS_AS(in.removeNode(outB.id), std::invalid_argument);
  // Refused means untouched: the face still shows both ports.
  CHECK(g.node(super.id).inputs().size() == 1);
  CHECK(g.node(super.id).outputs().size() == 1);

  // connectionsOf() names what to unwire, exactly as for any node.
  for (const NodeId b : {inB.id, outB.id}) {
    const auto wires = in.connectionsOf(b);
    REQUIRE(wires.size() == 1);
    in.disconnect(wires[0].dstNode, wires[0].dstPort);
    CHECK_NOTHROW(in.removeNode(b));
  }
  CHECK(g.node(super.id).inputs().empty());
  CHECK(g.node(super.id).outputs().empty());
}

TEST_CASE("a supernode's outer port ids survive any edit to its inner graph") {
  // Work done inside a supernode must not disturb a wire into it. The face is
  // read before and after a run of ordinary inner edits; every outer port
  // that was not itself removed keeps its id and its boundary node.
  Graph g;
  const auto synth = place<SineSynthNode>(g);
  const auto sink = place<CaptureSinkNode>(g, 1);
  const LanedSupernode s = addLanedSupernode(g);
  Graph& in = s.super->inner();
  const Node& face = g.node(s.super.id);
  const PortId in1 = face.inputs()[1].id;
  const PortId in2 = face.inputs()[2].id;
  const PortId out1 = face.outputs()[1].id;
  const PortId out2 = face.outputs()[2].id;
  g.connect(synth.id, synth->out(), s.super.id, in1);
  g.connect(s.super.id, out2, sink.id, sink->in());

  // An ordinary node comes and goes, wired in between.
  const auto extra = place<GainNode>(in, 1);
  in.connect(s.lanes[1].in.id, s.lanes[1].in->port(), extra.id, extra->in());
  in.disconnect(extra.id, extra->in());
  in.removeNode(extra.id);
  // An inner node gains a port and loses it again.
  const PortId added = s.lanes[2].sum->addWeightedInput(8.0f);
  in.removePort(s.lanes[2].sum.id, added);
  // The face grows on both sides, then loses its first pair.
  const auto lateIn = exposeInput(in, PortType::audio(1), "late in");
  const auto lateOut = exposeOutput(in, PortType::audio(1), "late out");
  dropLane(in, s.lanes[0]);

  REQUIRE(face.inputs().size() == 3);
  CHECK(face.inputs()[0].id == in1);
  CHECK(face.inputs()[1].id == in2);
  CHECK(face.inputs()[2].id == lateIn->port());
  REQUIRE(face.outputs().size() == 3);
  CHECK(face.outputs()[0].id == out1);
  CHECK(face.outputs()[1].id == out2);
  CHECK(face.outputs()[2].id == lateOut->port());
  CHECK(s.super->boundaryFor(in1) == s.lanes[1].in.id);
  CHECK(s.super->boundaryFor(in2) == s.lanes[2].in.id);
  CHECK(s.super->boundaryFor(out1) == s.lanes[1].out.id);
  CHECK(s.super->boundaryFor(out2) == s.lanes[2].out.id);

  // The outer wires were never touched, and still name ports the face has.
  REQUIRE(g.connections().size() == 2);
  CHECK(g.connections()[0].dstPort == in1);
  CHECK(g.connections()[1].srcPort == out2);
}

namespace {

// A section built on its own graph, as a saved one would be, with the outer
// port ids its boundary nodes minted.
struct Section {
  Graph graph;
  PortId audioIn = kNoPort;
  PortId audioOut = kNoPort;
};

Section buildDoubler() {
  Section s;
  const Lane lane = addLane(s.graph, 2.0f, "doubled");
  s.audioIn = lane.in->port();
  s.audioOut = lane.out->port();
  return s;
}

} // namespace

TEST_CASE("an inner graph is its own id namespace, so a snapshot drops in unchanged") {
  // Ids are minted per graph, so a section mints the same ids however often it
  // is built — nothing global leaks in that a new project would have to
  // renumber.
  CHECK(buildDoubler().audioIn == buildDoubler().audioIn);
  CHECK(buildDoubler().audioOut == buildDoubler().audioOut);

  // Dropped into projects that have minted different amounts before it
  // arrives, the snapshot keeps the ids it came with as its face, and renders
  // the same in both.
  auto renderInto = [](int clutter) {
    Graph g;
    for (int i = 0; i < clutter; ++i) place<GainNode>(g, 1);
    const auto synth = addSource(g);
    const auto super = place<Supernode>(g);
    Section section = buildDoubler();
    super->inner() = std::move(section.graph);
    // By value: the supernode now owns the section, and nothing else holds it.
    CHECK(section.graph.nodeCount() == 0);
    REQUIRE(g.node(super.id).inputs().size() == 1);
    CHECK(g.node(super.id).inputs()[0].id == section.audioIn);
    CHECK(g.node(super.id).outputs()[0].id == section.audioOut);

    const auto sink = place<CaptureSinkNode>(g, 1);
    g.connect(synth.id, synth->out(), super.id, section.audioIn);
    g.connect(super.id, section.audioOut, sink.id, sink->in());
    Engine e(g, 48000.0, 256);
    render(e, 2003, 256);
    return sink->samples();
  };

  const auto plain = renderInto(0);
  REQUIRE(isSounding(plain));
  CHECK(renderInto(7) == plain);
}

TEST_CASE("a wire into a deleted outer port fails at compile, never lands elsewhere") {
  // The inner graph's RESTRICT rule cannot see an outer wire, so it lets the
  // boundary node go: a node keeps no pointer to its graph, so an inner graph
  // cannot look outward, and the kernel deliberately adds no delete-time check
  // for this (ADR-0004, 2026-11-15): removing such a wire together with the
  // edit is the workspace's job, arriving with supernode editing. A caller
  // that bypasses the workspace still cannot misroute: the wire names a port
  // the face no longer has, and the compile says so rather than resolving it
  // somewhere plausible.
  Graph g;
  const auto super = place<Supernode>(g);
  const auto exposed = exposeOutput(super->inner(), PortType::audio(1), "a");
  const auto sink = place<CaptureSinkNode>(g, 1);
  g.connect(super.id, exposed->port(), sink.id, sink->in());

  REQUIRE(super->inner().connectionsOf(exposed.id).empty());
  super->inner().removeNode(exposed.id);
  CHECK(g.node(super.id).outputs().empty());

  CHECK_THROWS_WITH_AS(Engine(g, 48000.0, 256),
                       "engine: connection names an outer port that no longer exists",
                       std::invalid_argument);
}
