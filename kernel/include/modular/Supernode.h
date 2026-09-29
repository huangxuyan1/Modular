#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Graph.h"
#include "Ids.h"
#include "Node.h"
#include "PortType.h"

namespace modular {

// Which side of a supernode's face a boundary node declares. Stated from
// *outside*: Input means the supernode gains an input, so inside the inner
// graph that boundary node presents an output for inner nodes to read from.
// Output mirrors it. See BoundaryNode's constructor.
enum class BoundaryDirection : std::uint8_t { Input, Output };

// Marks one exposed port of the supernode whose inner graph holds it.
//
// Exposure is wiring, not configuration: a boundary node is an ordinary graph
// citizen, so type-checking, fan-out and deletion all fall out of the existing
// connection rules instead of being re-implemented for a side table. It never
// executes — the compiler splices it out by resolving connections straight
// through it.
class BoundaryNode : public Node {
public:
  // Declares the exposed port. The node's own port is the mirror image: an
  // exposed input *feeds* the inner graph, so it is an output in here.
  BoundaryNode(Graph& graph, BoundaryDirection direction, PortType type,
               std::string name);

  BoundaryDirection direction() const { return direction_; }

  // The port this node contributes to the supernode's face. It is the same
  // PortSpec as the mirrored inner port — stored once, so the inside and the
  // outside of a boundary can never describe different types, and so they
  // carry the one id.
  const PortSpec& exposed() const;

  // That id: the supernode's outer port id for this exposure *is* this port's
  // id. Nothing is borrowed and nothing is converted — both this node and its
  // port were minted by the inner graph's one id source, so two boundary
  // nodes in one inner graph cannot collide.
  PortId port() const { return exposed().id; }

  void process(ProcessContext&) override;

protected:
  // Refused: this node's port is the exposure itself, so removing it would
  // leave a boundary that exposes nothing. Remove the boundary node instead.
  bool erasePort(PortId port) override;

private:
  BoundaryDirection direction_;
};

// A node whose implementation is an inner graph it owns *by value*: placing a
// supernode snapshots it, and later edits to whatever it was built from do not
// propagate. Graph is explicitly move-only (see Graph.h), so a shared inner
// graph cannot be spelled — the snapshot rule is enforced by the type system
// rather than by discipline. An inner graph is also a closed id namespace, so
// a snapshot drops into any project unchanged.
//
// Externally indistinguishable from a primitive node: only its ports show.
// Flattened away at compile, so it never executes; latencySamples() therefore
// stays 0 — latency is a property of the primitives inside, and per-path
// latency emerges as sums along the paths the compiler can see.
//
// It never calls addInput()/addOutput(): a supernode's ports come from its
// boundary nodes and nowhere else, so the inherited port vectors stay empty.
class Supernode : public Node {
public:
  explicit Supernode(Graph& graph) : Node(graph) {}

  // The inner graph, open for authoring edits (adding nodes, wiring, and
  // placing boundary nodes to change the face).
  Graph& inner() { return inner_; }
  const Graph& inner() const { return inner_; }

  // Derived from the inner graph's boundary nodes, in insertion order, on
  // every call — never stored, so the face cannot drift from its contents.
  // Each PortSpec, id included, is the backing boundary node's own.
  //
  // The returned span is invalidated by any edit to the inner graph. Calling
  // the same accessor again re-derives into the same buffer: the result is
  // identical, and the span stays put, unless the inner graph changed — which
  // is exactly when the older span was stale anyway.
  std::span<const PortSpec> inputs() const override;
  std::span<const PortSpec> outputs() const override;

  // The inner-graph ids of the boundary nodes backing this face, in the same
  // order as inputs()/outputs(). Order is what a view walks; it is not what a
  // wire names, so a boundary node deleted from inside shifts nobody.
  std::vector<NodeId> boundaryIds(BoundaryDirection direction) const;

  // The boundary node backing outer port `id`, or kNoNode if this face has no
  // such port. A lookup, not a conversion: the outer port id and the boundary
  // node's port id are the same id.
  NodeId boundaryFor(PortId id) const;

  void process(ProcessContext&) override;

protected:
  // Refused: a supernode's ports are its boundary nodes, so the way to remove
  // one is to remove the boundary node from the inner graph — where the inner
  // graph's own RESTRICT rule applies to it like any other node.
  bool erasePort(PortId port) override;

private:
  std::span<const PortSpec> derive(BoundaryDirection direction,
                                   std::vector<PortSpec>& into) const;

  Graph inner_;
  // Return buffers for the spans above, not state: rewritten on every call.
  // Two invariants make that safe, and both must survive future edits — the
  // two faces must never share one buffer, and derive() must never re-enter
  // on the same supernode (it does not recurse into nested inner graphs).
  mutable std::vector<PortSpec> inputFace_;
  mutable std::vector<PortSpec> outputFace_;
};

} // namespace modular
