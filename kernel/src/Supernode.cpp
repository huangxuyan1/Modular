#include "modular/Supernode.h"

#include <cassert>
#include <stdexcept>

namespace modular {

BoundaryNode::BoundaryNode(Graph& graph, BoundaryDirection direction, PortType type,
                           std::string name)
    : Node(graph), direction_(direction) {
  // Mirror image: an exposed input feeds the inner graph (so it is an output
  // in here), an exposed output is fed by it.
  if (direction == BoundaryDirection::Input) {
    addOutput(type, std::move(name));
  } else {
    addInput(type, std::move(name));
  }
}

const PortSpec& BoundaryNode::exposed() const {
  return direction_ == BoundaryDirection::Input ? outputs()[0] : inputs()[0];
}

bool BoundaryNode::erasePort(PortId) {
  throw std::invalid_argument(
      "removePort: a boundary node's port is its exposure; remove the node instead");
}

void BoundaryNode::process(ProcessContext&) {
  assert(false && "boundary nodes are erased at compile and never execute");
}

std::span<const PortSpec> Supernode::inputs() const {
  return derive(BoundaryDirection::Input, inputFace_);
}

std::span<const PortSpec> Supernode::outputs() const {
  return derive(BoundaryDirection::Output, outputFace_);
}

// The one place outer port order is defined: boundary nodes of the requested
// direction, in the order they were added to the inner graph. It is read by
// anything that shows a face; nothing that resolves a wire needs it, because
// a wire names the port id that boundaryFor() looks up directly.
std::vector<NodeId> Supernode::boundaryIds(BoundaryDirection direction) const {
  std::vector<NodeId> ids;
  for (const NodeId id : inner_.nodeIds()) {
    const auto* b = dynamic_cast<const BoundaryNode*>(&inner_.node(id));
    if (b != nullptr && b->direction() == direction) ids.push_back(id);
  }
  return ids;
}

NodeId Supernode::boundaryFor(PortId id) const {
  for (const NodeId node : inner_.nodeIds()) {
    const auto* b = dynamic_cast<const BoundaryNode*>(&inner_.node(node));
    if (b != nullptr && b->port() == id) return node;
  }
  return kNoNode;
}

bool Supernode::erasePort(PortId) {
  throw std::invalid_argument(
      "removePort: a supernode's ports are its boundary nodes; remove the boundary "
      "node instead");
}

std::span<const PortSpec> Supernode::derive(BoundaryDirection direction,
                                            std::vector<PortSpec>& into) const {
  into.clear();
  for (const NodeId id : boundaryIds(direction))
    into.push_back(static_cast<const BoundaryNode&>(inner_.node(id)).exposed());
  return into;
}

void Supernode::process(ProcessContext&) {
  assert(false && "supernodes are erased at compile and never execute");
}

} // namespace modular
