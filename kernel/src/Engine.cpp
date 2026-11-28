#include "modular/Engine.h"

#include <algorithm>
#include <cassert>
#include <span>
#include <stdexcept>
#include <utility>

#include "Flatten.h"

namespace modular {

namespace {

// Engine-owned storage backing one audio output port.
struct AudioStorage {
  std::vector<std::vector<float>> data; // [channel][frame]
  std::vector<float*> ptrs;             // rebuilt after allocation

  void allocate(std::uint32_t channels, std::uint32_t maxFrames) {
    data.assign(channels, std::vector<float>(maxFrames, 0.0f));
    ptrs.resize(channels);
    for (std::uint32_t c = 0; c < channels; ++c) ptrs[c] = data[c].data();
  }
};

// Port id -> position in one node's port list: the compaction the compile
// does. Sorted by id rather than indexed by it, because ids are sparse — a
// node whose ports have churned ten thousand times has a high-water mark far
// above its live count, and a table indexed by raw id would be sized by that
// history instead of by what is alive. Sorting is also what keeps this
// independent of the node's display order, which is free to change.
//
// The cost this adds to the block loop is one lookup per port per accessor
// call — once per block, not per sample.
struct PortLookup {
  std::vector<PortId> ids;       // ascending
  std::vector<std::uint32_t> at; // parallel: position in the node's list

  void build(std::span<const PortSpec> ports) {
    std::vector<std::pair<PortId, std::uint32_t>> pairs;
    pairs.reserve(ports.size());
    for (std::size_t i = 0; i < ports.size(); ++i)
      pairs.push_back({ports[i].id, static_cast<std::uint32_t>(i)});
    std::sort(pairs.begin(), pairs.end());

    ids.clear();
    at.clear();
    ids.reserve(pairs.size());
    at.reserve(pairs.size());
    for (const auto& [id, position] : pairs) {
      ids.push_back(id);
      at.push_back(position);
    }
  }

  // Process path: assert only, never throw. A node can only get here with an
  // id it minted itself, and a port that a wire named cannot have been deleted
  // (RESTRICT), so a miss means the node named a port it had already dropped.
  std::size_t find(PortId id) const {
    const auto p = std::lower_bound(ids.begin(), ids.end(), id);
    assert(p != ids.end() && *p == id && "process(): no such port id on this node");
    return at[static_cast<std::size_t>(p - ids.begin())];
  }
};

} // namespace

// Per-node wiring tables the ProcessContext reads through. Parallel to the
// node's port lists; exactly one entry per port is non-null, per its domain.
// The two lookups above turn the id a node asks with into that position.
struct ProcessContext::Wiring {
  Node* node = nullptr;
  PortLookup inLookup;
  PortLookup outLookup;

  // inputs: pointers to upstream storage or engine defaults
  std::vector<const AudioStorage*> audioIns;
  std::vector<const EventBuffer*> eventIns;
  std::vector<const ValueSlot*> valueIns;
  // per audio input: channel count to present (silence has no inherent count)
  std::vector<std::uint32_t> audioInChannels;

  // outputs: owned storage
  std::vector<std::unique_ptr<AudioStorage>> audioOuts;
  std::vector<std::unique_ptr<EventBuffer>> eventOuts;
  std::vector<std::unique_ptr<ValueSlot>> valueOuts;
};

ConstAudioView ProcessContext::audioIn(PortId port) const {
  const std::size_t p = wiring_->inLookup.find(port);
  assert(wiring_->node->inputs()[p].type.domain == Domain::Audio);
  const AudioStorage* s = wiring_->audioIns[p];
  assert(s != nullptr);
  return {s->ptrs.data(), wiring_->audioInChannels[p], numFrames_};
}

AudioView ProcessContext::audioOut(PortId port) const {
  const std::size_t p = wiring_->outLookup.find(port);
  assert(wiring_->node->outputs()[p].type.domain == Domain::Audio);
  AudioStorage* s = wiring_->audioOuts[p].get();
  assert(s != nullptr);
  return {s->ptrs.data(), static_cast<std::uint32_t>(s->ptrs.size()), numFrames_};
}

const EventBuffer& ProcessContext::eventsIn(PortId port) const {
  const std::size_t p = wiring_->inLookup.find(port);
  assert(wiring_->node->inputs()[p].type.domain == Domain::Event);
  return *wiring_->eventIns[p];
}

EventBuffer& ProcessContext::eventsOut(PortId port) {
  const std::size_t p = wiring_->outLookup.find(port);
  assert(wiring_->node->outputs()[p].type.domain == Domain::Event);
  return *wiring_->eventOuts[p];
}

ValueView ProcessContext::valueIn(PortId port) const {
  const std::size_t p = wiring_->inLookup.find(port);
  assert(wiring_->node->inputs()[p].type.domain == Domain::Value);
  return ValueView{wiring_->valueIns[p]};
}

ValueSlot& ProcessContext::valueOut(PortId port) {
  const std::size_t p = wiring_->outLookup.find(port);
  assert(wiring_->node->outputs()[p].type.domain == Domain::Value);
  return *wiring_->valueOuts[p];
}

struct Engine::Impl {
  std::size_t prepared = 0;
  ~Impl() {
    while (prepared != 0) nodes[--prepared].node->release();
  }
  // The executed graph: primitives only, supernodes already erased. Both
  // vectors below are indexed by position in `nodes`, not by NodeId — a
  // NodeId is only meaningful relative to one Graph, and the executed graph
  // is drawn from a whole hierarchy of them.
  std::vector<ResolvedNode> nodes;
  std::vector<std::size_t> order; // topological
  std::vector<ProcessContext::Wiring> wiring;

  // defaults for unconnected inputs
  AudioStorage silence;      // zeros, never written; ptrs repeat one channel
  EventBuffer noEvents;      // always empty
  ValueSlot zeroValue;       // zeroed blob
};

Engine::Engine(Graph& graph, double sampleRate, std::uint32_t maxBlockFrames)
    : impl_(std::make_unique<Impl>()), maxBlockFrames_(maxBlockFrames) {
  if (maxBlockFrames == 0) throw std::invalid_argument("engine: maxBlockFrames == 0");
  impl_->nodes = flatten(graph);
  const std::size_t n = impl_->nodes.size();

  // --- topological order (Kahn) ---
  // Edges come from the resolved sources, not the authored connections: a
  // connection that lands on a boundary node is not a dependency between two
  // things that run, it is a step on the way to one.
  std::vector<std::uint32_t> indegree(n, 0);
  std::vector<std::vector<std::size_t>> successors(n);
  for (std::size_t i = 0; i < n; ++i) {
    for (const auto& src : impl_->nodes[i].inputs) {
      if (!src) continue;
      ++indegree[i];
      successors[src->node].push_back(i);
    }
  }
  std::vector<std::size_t> ready;
  for (std::size_t i = 0; i < n; ++i)
    if (indegree[i] == 0) ready.push_back(i);
  while (!ready.empty()) {
    const std::size_t id = ready.back();
    ready.pop_back();
    impl_->order.push_back(id);
    for (std::size_t s : successors[id])
      if (--indegree[s] == 0) ready.push_back(s);
  }
  if (impl_->order.size() != n)
    throw std::invalid_argument(
        "engine: graph has a cycle (cycles need delay compensation; not in v1)");

  // --- allocate wiring ---
  // Silence buffer: one zeroed channel, pointer repeated up to the widest
  // audio input in the graph so any channel count can view it.
  std::uint32_t maxInChannels = 1;
  for (const ResolvedNode& r : impl_->nodes)
    for (const PortSpec& p : r.node->inputs())
      if (p.type.domain == Domain::Audio)
        maxInChannels = std::max(maxInChannels, p.type.channels);
  impl_->silence.data.assign(1, std::vector<float>(maxBlockFrames, 0.0f));
  impl_->silence.ptrs.assign(maxInChannels, impl_->silence.data[0].data());

  impl_->wiring.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    Node& node = *impl_->nodes[i].node;
    auto& w = impl_->wiring[i];
    w.node = &node;

    const auto ins = node.inputs();
    w.inLookup.build(ins);
    w.audioIns.assign(ins.size(), nullptr);
    w.eventIns.assign(ins.size(), nullptr);
    w.valueIns.assign(ins.size(), nullptr);
    w.audioInChannels.assign(ins.size(), 0);

    const auto outs = node.outputs();
    w.outLookup.build(outs);
    w.audioOuts.resize(outs.size());
    w.eventOuts.resize(outs.size());
    w.valueOuts.resize(outs.size());
    for (std::size_t p = 0; p < outs.size(); ++p) {
      switch (outs[p].type.domain) {
        case Domain::Audio:
          w.audioOuts[p] = std::make_unique<AudioStorage>();
          w.audioOuts[p]->allocate(outs[p].type.channels, maxBlockFrames);
          break;
        case Domain::Event:
          w.eventOuts[p] = std::make_unique<EventBuffer>();
          break;
        case Domain::Value:
          w.valueOuts[p] = std::make_unique<ValueSlot>();
          break;
      }
    }
  }

  // Point each input at the storage that feeds it, or at this domain's
  // default when nothing does. Every output's storage exists by now.
  // The input port's own type decides the domain: resolution preserves type
  // equality end to end, since every hop it traced was itself type-checked.
  for (std::size_t i = 0; i < n; ++i) {
    auto& w = impl_->wiring[i];
    const auto ins = impl_->nodes[i].node->inputs();
    for (std::size_t p = 0; p < ins.size(); ++p) {
      const std::optional<SourcePort>& src = impl_->nodes[i].inputs[p];
      switch (ins[p].type.domain) {
        case Domain::Audio:
          w.audioIns[p] = src ? impl_->wiring[src->node].audioOuts[src->port].get()
                              : &impl_->silence;
          w.audioInChannels[p] = ins[p].type.channels;
          break;
        case Domain::Event:
          w.eventIns[p] = src ? impl_->wiring[src->node].eventOuts[src->port].get()
                              : &impl_->noEvents;
          break;
        case Domain::Value:
          w.valueIns[p] = src ? impl_->wiring[src->node].valueOuts[src->port].get()
                              : &impl_->zeroValue;
          break;
      }
    }
  }

  // --- prepare nodes ---
  const PrepareInfo info{sampleRate, maxBlockFrames};
  for (const ResolvedNode& r : impl_->nodes) {
    ++impl_->prepared;
    r.node->prepare(info);
  }
}

Engine::~Engine() = default;

std::string Engine::errorMessage() const {
  for (const auto& resolved : impl_->nodes) {
    auto error = resolved.node->errorMessage();
    if (!error.empty()) return error;
  }
  return {};
}

void Engine::process(std::uint32_t numFrames) {
  assert(numFrames <= maxBlockFrames_);
  // Events are per-block; values persist across blocks.
  for (auto& w : impl_->wiring)
    for (auto& e : w.eventOuts)
      if (e) e->clear();

  for (std::size_t id : impl_->order) {
    ProcessContext ctx;
    ctx.wiring_ = &impl_->wiring[id];
    ctx.numFrames_ = numFrames;
    ctx.blockStart_ = streamTime_;
    impl_->wiring[id].node->process(ctx);
  }
  streamTime_ += numFrames;
}

} // namespace modular
