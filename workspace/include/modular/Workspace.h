#pragma once

#include <cmath>
#include <map>
#include <stdexcept>

#include "modular/Graph.h"

namespace modular {

struct NodePosition {
  double x = 0;
  double y = 0;
  friend bool operator==(const NodePosition&, const NodePosition&) = default;
};

// Owns authored presentation alongside its graph, outside kernel processing.
// Topology is always read from Graph; positions are associated only by identity.
// Clients must release any Engine borrowing graph() before structural edits.
class Workspace {
public:
  Graph& graph() { return graph_; }
  const Graph& graph() const { return graph_; }

  NodePosition position(NodeId node) const {
    graph_.node(node);
    const auto at = positions_.find(node);
    return at == positions_.end() ? NodePosition{} : at->second;
  }

  void setPosition(NodeId node, NodePosition position) {
    graph_.node(node);
    if (!std::isfinite(position.x) || !std::isfinite(position.y))
      throw std::invalid_argument("position: coordinates must be finite");
    positions_[node] = position;
  }

  // Explicitly opted into by a client. Primitive Graph removal stays RESTRICT.
  void deleteNodeWithConnections(NodeId node) {
    const auto wires = graph_.connectionsOf(node);
    for (const auto& wire : wires) graph_.disconnect(wire.dstNode, wire.dstPort);
    graph_.removeNode(node);
    positions_.erase(node);
  }

private:
  Graph graph_;
  std::map<NodeId, NodePosition> positions_;
};

} // namespace modular
