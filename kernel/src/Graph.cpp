#include "modular/Graph.h"

#include <algorithm>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <set>

namespace modular {

namespace {

// A port by id. Linear on purpose: a port list is short, this is an authoring
// path, and scanning assumes nothing about the list's order — which is display
// order and free to change without a wire noticing.
const PortSpec* findPort(std::span<const PortSpec> ports, PortId id) {
  for (const PortSpec& p : ports)
    if (p.id == id) return &p;
  return nullptr;
}

} // namespace

Graph::Graph(Graph&& other)
    : ids_(std::exchange(other.ids_, std::make_unique<IdSource>())),
      nodes_(std::move(other.nodes_)),
      connections_(std::move(other.connections_)) {}

Graph& Graph::operator=(Graph&& other) {
  if (this == &other) return *this;
  ids_ = std::exchange(other.ids_, std::make_unique<IdSource>());
  nodes_ = std::move(other.nodes_);
  connections_ = std::move(other.connections_);
  // Move-assigning a vector is not required to leave the source empty, and an
  // emptied graph is what the new id source above was handed out for.
  other.nodes_.clear();
  other.connections_.clear();
  return *this;
}

const Graph::Entry& Graph::require(NodeId id, const char* message) const {
  const auto at = std::lower_bound(nodes_.begin(), nodes_.end(), id,
                                   [](const Entry& e, NodeId v) { return e.id < v; });
  if (at == nodes_.end() || at->id != id) throw std::invalid_argument(message);
  return *at;
}

NodeId Graph::adopt(std::unique_ptr<Node> node) {
  if (!node) throw std::invalid_argument("addNode: null node");
  // Minted after the node's ports, since those were minted while it was being
  // constructed. Node ids therefore still ascend with insertion order, which
  // is what keeps nodes_ sorted for require() above.
  const NodeId id = ids_->mint();
  nodes_.push_back(Entry{id, std::move(node)});
  return id;
}

void Graph::validateRestoration(NodeId id, const NodePorts& ports) const {
  if (restored_ports_ || (!nodes_.empty() && id <= nodes_.back().id))
    throw std::invalid_argument("restore: nodes must have increasing identities");
  std::set<std::uint32_t> used;
  for (const auto& entry : nodes_) {
    used.insert(entry.id);
    for (const auto list : {entry.node->inputs(), entry.node->outputs()})
      for (const auto& port : list) used.insert(port.id);
  }
  const auto claim = [&](std::uint32_t identity) {
    if (identity == 0 || identity >= ids_->next() || !used.insert(identity).second)
      throw std::invalid_argument("restore: duplicate or unreserved identity");
  };
  claim(id);
  for (const auto* list : {&ports.inputs, &ports.outputs})
    for (const auto& port : *list) claim(port.id);
}

void Graph::removeNode(NodeId id) {
  const Entry& entry = require(id, "removeNode: no such node id");
  // RESTRICT, stated as exactly what connectionsOf() reports, so a caller that
  // unwires everything that query returns can always then delete.
  if (!connectionsOf(id).empty())
    throw std::invalid_argument("removeNode: node is still wired; disconnect first");
  nodes_.erase(nodes_.begin() + (&entry - nodes_.data()));
}

void Graph::removePort(NodeId node, PortId port) {
  const Entry& entry = require(node, "removePort: no such node id");
  // The same RESTRICT rule as removeNode, one level finer — one rule for all
  // three kinds of deletion rather than one per kind.
  if (!connectionsOf(node, port).empty())
    throw std::invalid_argument("removePort: port is still wired; disconnect first");
  if (!entry.node->erasePort(port))
    throw std::invalid_argument("removePort: no such port id on this node");
  entry.node->portRemoved(port);
}

void Graph::connect(NodeId srcNode, PortId srcPort, NodeId dstNode, PortId dstPort) {
  const Entry& src = require(srcNode, "connect: bad src node id");
  const Entry& dst = require(dstNode, "connect: bad dst node id");

  const PortSpec* out = findPort(src.node->outputs(), srcPort);
  const PortSpec* in = findPort(dst.node->inputs(), dstPort);
  if (out == nullptr) throw std::invalid_argument("connect: bad src port");
  if (in == nullptr) throw std::invalid_argument("connect: bad dst port");

  if (out->type != in->type) {
    throw std::invalid_argument(
        "connect: type mismatch (" + out->name + " -> " + in->name + ")");
  }

  for (const Connection& c : connections_) {
    if (c.dstNode == dstNode && c.dstPort == dstPort) {
      throw std::invalid_argument("connect: input already connected (" + in->name +
                                  "); merge with a node, not the graph");
    }
  }

  connections_.push_back({srcNode, srcPort, dstNode, dstPort});
}

void Graph::disconnect(NodeId dstNode, PortId dstPort) {
  const Entry& dst = require(dstNode, "disconnect: bad dst node id");
  if (findPort(dst.node->inputs(), dstPort) == nullptr)
    throw std::invalid_argument("disconnect: bad dst port");

  for (auto it = connections_.begin(); it != connections_.end(); ++it) {
    if (it->dstNode == dstNode && it->dstPort == dstPort) {
      connections_.erase(it);
      return;
    }
  }
  throw std::invalid_argument("disconnect: input is not connected");
}

std::vector<Connection> Graph::connectionsOf(NodeId id) const {
  require(id, "connectionsOf: no such node id");
  std::vector<Connection> touching;
  for (const Connection& c : connections_)
    if (c.srcNode == id || c.dstNode == id) touching.push_back(c);
  return touching;
}

std::vector<Connection> Graph::connectionsOf(NodeId node, PortId port) const {
  require(node, "connectionsOf: no such node id");
  std::vector<Connection> touching;
  for (const Connection& c : connections_) {
    const bool leaves = c.srcNode == node && c.srcPort == port;
    const bool arrives = c.dstNode == node && c.dstPort == port;
    if (leaves || arrives) touching.push_back(c);
  }
  return touching;
}

const Node& Graph::node(NodeId id) const {
  return *require(id, "node: no such node id").node;
}

Node& Graph::node(NodeId id) {
  return const_cast<Node&>(static_cast<const Graph*>(this)->node(id));
}

std::vector<NodeId> Graph::nodeIds() const {
  std::vector<NodeId> ids;
  ids.reserve(nodes_.size());
  for (const Entry& e : nodes_) ids.push_back(e.id);
  return ids;
}

} // namespace modular
