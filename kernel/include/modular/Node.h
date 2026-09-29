#pragma once

#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Buffers.h"
#include "Ids.h"
#include "PortType.h"

namespace modular {

class Graph;

// One port of a node. The id is identity — it is what a wire names and what
// process() reads through. The port's position in inputs()/outputs() is
// display order, and ordering within a homogeneous set (a mixer's inputs);
// it is never identity, so reordering or deleting a port disturbs no wire.
struct PortSpec {
  PortId id = kNoPort;
  PortType type;
  std::string name;
};

// Declaration order and identities supplied during primitive reconstruction.
// Nodes receive each saved identity from addInput/addOutput as they construct
// their own retained references; no later pass patches private node members.
struct NodePorts {
  std::vector<PortSpec> inputs;
  std::vector<PortSpec> outputs;
};

struct PortTypeChange {
  PortId port;
  PortType type;
};

struct PrepareInfo {
  double sampleRate = 0.0;
  std::uint32_t maxBlockFrames = 0;
};

// Handed to Node::process() by the engine; valid only for that call.
// Ports are named by id, the same id addInput()/addOutput() returned — there
// is no authoring/execution coordinate split, so a node has no rule to
// remember about when a position is still good.
// Accessors assert that the port's domain matches.
//
// Unconnected inputs read defaults: audio → silence, event → empty,
// value → zeroed slot.
class ProcessContext {
public:
  std::uint32_t numFrames() const { return numFrames_; }

  // Stream time of this block's frame 0 — the monotonic engine clock.
  // It never jumps. Timeline position (seek/loop/playhead) is a user-space
  // concept built on top and is unknown to the kernel.
  std::uint64_t blockStartFrame() const { return blockStart_; }

  ConstAudioView audioIn(PortId port) const;
  AudioView audioOut(PortId port) const;
  const EventBuffer& eventsIn(PortId port) const;
  EventBuffer& eventsOut(PortId port);
  ValueView valueIn(PortId port) const;
  ValueSlot& valueOut(PortId port);

private:
  friend class Engine;
  struct Wiring; // engine-internal port wiring tables
  const Wiring* wiring_ = nullptr;
  std::uint32_t numFrames_ = 0;
  std::uint64_t blockStart_ = 0;
};

// A node: typed input/output ports plus a block process function.
// Subclasses declare ports in their constructor via addInput()/addOutput()
// and keep the returned ids in their own private members, which they name
// again in process(). Nothing outside the node knows those members exist, so
// nothing outside can rewrite what a port means.
//
// Nothing is frozen. A node's ports can be added and removed while it sits
// wired in a graph: no plugin can enumerate its params up front (sections
// switch in and out with their own params), so a node wrapping one has to be
// able to follow it. What is immutable is the *compiled form* — the Engine
// takes a snapshot at compile and the authored graph stays editable
// underneath it. (Engine's own contract still stands: don't edit a graph
// while an Engine on it lives; editing means building a new Engine.)
class Node {
public:
  virtual ~Node() = default;

  // Virtual because a Supernode has no port vectors of its own: its face is
  // *derived* from the boundary nodes in its inner graph on every call, so it
  // cannot drift from what is inside.
  //
  // Deriving a face allocates, so an override must stay off the block loop.
  // It does: the only caller inside process() is ProcessContext's debug domain
  // asserts, and supernodes are erased at compile, so they never process().
  //
  // The returned span is invalidated by any edit to the node's ports, or —
  // for a supernode — to its inner graph. The ids in it are not: an id
  // outlives every rearrangement of the list that carries it.
  virtual std::span<const PortSpec> inputs() const { return inputs_; }
  virtual std::span<const PortSpec> outputs() const { return outputs_; }

  // Samples of delay this node introduces from input to output. The engine
  // will use this for delay compensation (not implemented in v1).
  virtual std::uint32_t latencySamples() const { return 0; }

  // Called once by the engine at compile time, before any process() call.
  virtual void prepare(const PrepareInfo&) {}

  // Paired with every attempted prepare, including one that throws. Called by
  // Engine teardown after processing ends; never emits a signal into the graph.
  virtual void release() noexcept {}

  // Read by the execution client on the processing thread BETWEEN blocks.
  // Formatting may allocate. An empty string means no reported failure.
  virtual std::string errorMessage() const { return {}; }

  // Process one block. Write all declared outputs for the full block.
  virtual void process(ProcessContext& ctx) = 0;

protected:
  // Nodes are constructed by the graph — graph.addNode<GainNode>(1) — because
  // an id has to be correct the moment it is minted: once a node holds its
  // port ids in members nobody else can see, no later pass can renumber them.
  // So a node holds its graph's id source before its constructor body runs.
  //
  // The *source* is kept, never the Graph: Graph is movable and a Supernode
  // holds one by value, so a stored Graph* would dangle after a move. The
  // source is separately allocated (see Ids.h) precisely so this stays good.
  explicit Node(Graph& graph);

  // Node-owned configuration can change several port types as one edit. Validate
  // every affected wire before publishing any change; identity is preserved.
  // Primitive nodes only, and no Engine may borrow the graph during this call.
  void setPortTypes(Graph& graph, NodeId self, std::span<const PortTypeChange> changes);

  // A variable-arity edit checks the entire growth before changing any ports.
  // This does not mint ids; failed edits must not leave partial configuration.
  void requirePortCapacity(std::uint64_t count) const { ids_->requireAvailable(count); }

  PortId addInput(PortType type, std::string name) {
    return addPort(inputs_, restored_ports_ ? &restored_ports_->inputs : nullptr,
                   type, std::move(name));
  }
  PortId addOutput(PortType type, std::string name) {
    return addPort(outputs_, restored_ports_ ? &restored_ports_->outputs : nullptr,
                   type, std::move(name));
  }

  // Called by Graph::removePort() once the port is gone from the lists above,
  // so a node that keeps port ids in its own members can forget the dead one.
  //
  // Adding a port needs nothing but the id source, so it stays the node's own
  // business; removing one has to consult the wires, and only the graph holds
  // those. That is the whole reason deletion is a graph operation with a hook
  // back, rather than a second method next to addInput().
  virtual void portRemoved(PortId /*port*/) {}

  // Erases the port with this id from whichever list holds it; false if
  // neither does. Graph::removePort() is the only caller — it enforces
  // RESTRICT first — so this never has to ask whether a wire still names it.
  virtual bool erasePort(PortId port);

private:
  friend class Graph; // removes ports, under the RESTRICT rule
  PortId addPort(std::vector<PortSpec>& ports, const std::vector<PortSpec>* restored,
                 PortType type, std::string name);
  void finishRestoration();
  IdSource* ids_ = nullptr;
  const NodePorts* restored_ports_ = nullptr; // only during construction
  std::vector<PortSpec> inputs_;
  std::vector<PortSpec> outputs_;
};

} // namespace modular
