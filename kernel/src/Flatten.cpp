#include "Flatten.h"

#include <cassert>
#include <cstddef>
#include <deque>
#include <span>
#include <stdexcept>
#include <unordered_map>

#include "modular/Supernode.h"

namespace modular {

namespace {

// One graph in the hierarchy, plus the way back out. A supernode's inner
// graph is not self-contained: an input boundary node is fed from *outside*,
// so resolution has to be able to climb to the graph that placed the
// supernode. That is the only reason a frame exists.
struct Frame {
  Graph* graph = nullptr;
  const Frame* parent = nullptr; // null at the top level
  Supernode* super = nullptr;    // the supernode owning `graph`, if any
  NodeId superId = kNoNode;      // `super`'s id in parent->graph
};

// A hop already taken by one in-progress resolution; see the cycle check.
struct Hop {
  const Node* node;
  PortId port;
};

// Where a port sits in its node's list. The executed graph is positional —
// ProcessContext::Wiring is per-node vectors parallel to the port lists — so
// this is where an id stops. It is the last translation, and nothing outside
// the engine ever sees the other side of it.
std::uint32_t positionOf(std::span<const PortSpec> ports, PortId id) {
  for (std::size_t i = 0; i < ports.size(); ++i)
    if (ports[i].id == id) return static_cast<std::uint32_t>(i);
  throw std::invalid_argument("engine: connection names a port that no longer exists");
}

class Flattener {
public:
  std::vector<ResolvedNode> run(Graph& graph) {
    frames_.push_back(Frame{&graph, nullptr, nullptr, kNoNode});
    enumerate(frames_.front());

    // Resolution is a second pass because a source may live anywhere in the
    // hierarchy — including in a supernode not yet enumerated when its
    // consumer was.
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
      const Placement& p = placements_[i];
      const auto ins = nodes_[i].node->inputs();
      nodes_[i].inputs.reserve(ins.size());
      for (const PortSpec& port : ins) {
        std::vector<Hop> trail;
        nodes_[i].inputs.push_back(resolveInput(*p.frame, p.id, port.id, trail));
      }
    }
    return std::move(nodes_);
  }

private:
  // Where a resolved node sits in the authored hierarchy — kept only until
  // its inputs have been resolved; the executed graph has no such notion.
  struct Placement {
    const Frame* frame;
    NodeId id;
  };

  // Collects the nodes that will actually run, descending through supernode
  // shells. Supernodes and boundary nodes are structure, not behavior: they
  // are skipped here, which is precisely what "erased at compile" means.
  void enumerate(const Frame& f) {
    for (const NodeId id : f.graph->nodeIds()) {
      Node& n = f.graph->node(id);
      if (auto* s = dynamic_cast<Supernode*>(&n)) {
        // Frames are held by the deque, not the stack: placements below
        // outlive this recursion. push_back never invalidates references to
        // existing elements, so `f` and its ancestors stay valid.
        frames_.push_back(Frame{&s->inner(), &f, s, id});
        enumerate(frames_.back());
        continue;
      }
      if (dynamic_cast<BoundaryNode*>(&n) != nullptr) continue;
      slotOf_[&n] = nodes_.size();
      nodes_.push_back(ResolvedNode{&n, {}});
      placements_.push_back(Placement{&f, id});
    }
  }

  // What feeds (node, port) in this frame, traced through as many boundaries
  // and supernode shells as it takes to reach something that runs.
  std::optional<SourcePort> resolveInput(const Frame& f, NodeId id, PortId port,
                                         std::vector<Hop>& trail) {
    for (const Connection& c : f.graph->connections())
      if (c.dstNode == id && c.dstPort == port)
        return resolveOutput(f, c.srcNode, c.srcPort, trail);
    return std::nullopt; // nothing feeds it: the engine substitutes a default
  }

  std::optional<SourcePort> resolveOutput(const Frame& f, NodeId id, PortId port,
                                          std::vector<Hop>& trail) {
    Node& n = f.graph->node(id);

    // Pass-through supernodes can be wired into a loop containing nothing
    // that runs, and this walk would never leave it. The topological sort
    // catches every cycle with something in it to run; this catches the rest.
    for (const Hop& h : trail)
      if (h.node == &n && h.port == port)
        throw std::invalid_argument(
            "engine: graph has a cycle (cycles need delay compensation; not in v1)");
    trail.push_back(Hop{&n, port});

    if (auto* s = dynamic_cast<Supernode*>(&n)) {
      // The outer port is backed by a boundary node whose own port carries the
      // very same id, so crossing inwards translates nothing. A face is read
      // by id, but a boundary node can be deleted from the inside, so the
      // check connect() made can go stale between wiring and compiling —
      // checked rather than asserted, because compile is an authoring path.
      const NodeId backing = s->boundaryFor(port);
      if (backing == kNoNode)
        throw std::invalid_argument(
            "engine: connection names an outer port that no longer exists");
      const Frame inner{&s->inner(), &f, s, id};
      return resolveInput(inner, backing, port, trail);
    }

    if (dynamic_cast<BoundaryNode*>(&n) != nullptr) {
      // Only an input-side boundary node has an output to be read from, and
      // it is fed from outside the supernode — so climb one level and ask
      // what feeds the matching outer port. Same id again, on the way out.
      assert(static_cast<BoundaryNode&>(n).direction() == BoundaryDirection::Input);
      if (f.parent == nullptr) return std::nullopt; // no outside to climb to
      assert(f.super != nullptr && f.super->boundaryFor(port) == id &&
             "a boundary node must appear on its own face");
      return resolveInput(*f.parent, f.superId, port, trail);
    }

    return SourcePort{slotOf_.at(&n), positionOf(n.outputs(), port)};
  }

  std::deque<Frame> frames_;
  std::vector<ResolvedNode> nodes_;
  std::vector<Placement> placements_;
  std::unordered_map<const Node*, std::size_t> slotOf_;
};

} // namespace

std::vector<ResolvedNode> flatten(Graph& graph) { return Flattener().run(graph); }

} // namespace modular
