#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "Ids.h"
#include "Node.h"

namespace modular {

// A wire between two ports, naming ids at both ends and on both axes. Never
// positions: deleting a node must not repoint the wires that named the nodes
// around it, and deleting a port must not repoint the wires that named the
// ports beside it.
struct Connection {
  NodeId srcNode;
  PortId srcPort;
  NodeId dstNode;
  PortId dstPort;
};

// The authored graph: nodes and typed connections. Authoring-time only —
// the Engine compiles it into an executable form. Throws std::invalid_argument
// on bad ids, type mismatches, or double-connecting an input.
//
// Fan-out (one output feeding many inputs) is allowed. Fan-in to a single
// input is not — merging is a node's job (e.g. an event mux), not the graph's.
//
// Deleting a node, a port or a boundary node is RESTRICT: anything that still
// carries a wire cannot be removed. The kernel would otherwise have to decide
// what the author meant, which is the argument that rejected fan-in. Clients
// can explicitly opt into shared unwire-then-delete behavior above the kernel;
// connectionsOf() provides the affected wires. Primitive removal stays RESTRICT.
//
// RESTRICT sees wires in this graph only. A boundary node's outer wire lives in
// the graph holding its supernode, which this one cannot see — a node keeps no
// pointer to its graph — so removing the boundary node leaves that wire naming
// a vanished outer port, and the compile refuses it. Removing such wires with
// the edit is the workspace's job (ADR-0004, 2026-11-15).
class Graph {
public:
  Graph() : ids_(std::make_unique<IdSource>()) {}

  // Starts an empty reconstruction with its saved allocation history reserved.
  // Restored ids must be below nextIdentity; ordinary additions mint above it.
  explicit Graph(std::uint64_t nextIdentity)
      : ids_(std::make_unique<IdSource>(nextIdentity)) {}
  std::uint64_t nextIdentity() const { return ids_->next(); }

  // Move-only, explicitly: nodes are stateful instances this graph owns, so
  // copying one would need a clone contract on Node — rejected in ADR-0001.
  // This is also what makes a supernode's inner graph a real snapshot instead
  // of an alias: there is no way to spell the sharing.
  Graph(const Graph&) = delete;
  Graph& operator=(const Graph&) = delete;

  // Hand-written rather than defaulted so a moved-from graph is an *empty*
  // graph rather than a sourceless one. Every graph mints from an id source,
  // and leaving that source null would make a later edit undefined behaviour
  // instead of an ordinary one.
  Graph(Graph&& other);
  Graph& operator=(Graph&& other);

  // Constructs a node in this graph and returns its id. The node is built
  // here rather than handed in ready-made because it mints its port ids from
  // this graph's id source inside its own constructor: T's first constructor
  // parameter is the Graph&, and everything here is forwarded after it.
  template <class T, class... Args>
  NodeId addNode(Args&&... args) {
    if (restored_ports_)
      throw std::invalid_argument("restore: nested construction in the same graph");
    return adopt(std::make_unique<T>(*this, std::forward<Args>(args)...));
  }

  // Primitive reconstruction, in original node order. The constructor declares
  // the saved ports in their respective input/output orders and must consume
  // all of them with matching types. Saved names and ids are authoritative.
  template <class T, class... Args>
  NodeId restoreNode(NodeId id, const NodePorts& ports, Args&&... args) {
    validateRestoration(id, ports);
    restored_ports_ = &ports;
    try {
      auto node = std::make_unique<T>(*this, std::forward<Args>(args)...);
      node->finishRestoration();
      nodes_.push_back(Entry{id, std::move(node)});
      restored_ports_ = nullptr;
    } catch (...) {
      restored_ports_ = nullptr;
      throw;
    }
    return id;
  }

  // Throws if the node is unknown, or if any wire still names it at either
  // end — unwire first, per the RESTRICT rule above. Its id is not recycled.
  void removeNode(NodeId id);

  // Removes one port from a node, under the same RESTRICT rule: a port a wire
  // still names cannot go. No other port's id changes, so the members the node
  // keeps its ids in stay valid; the node is told which one died so it can
  // forget it (Node::portRemoved).
  //
  // Removal lives here rather than beside addInput() because only the graph
  // knows what is wired. A node that wants to drop one of its own ports
  // therefore needs a graph to say so through — which is the command layer's
  // job, and is why nothing in the kernel initiates it.
  void removePort(NodeId node, PortId port);

  // Connection requires exact type equality: same domain, same sub
  // (channel count for audio, subtype id for value/event).
  void connect(NodeId srcNode, PortId srcPort, NodeId dstNode, PortId dstPort);

  // Removes the wire feeding one input, unconditionally: a wire is what
  // RESTRICT protects, so removing one has nothing to protect. An input takes
  // exactly one connection, so naming its end names the wire. Throws if that
  // input is not connected.
  void disconnect(NodeId dstNode, PortId dstPort);

  // Every wire naming this node at either end, in connection order — what a
  // caller has to unwire before removeNode() will accept it.
  std::vector<Connection> connectionsOf(NodeId id) const;

  // The same question one level finer: every wire naming this exact port, at
  // either end — what removePort() would refuse over.
  std::vector<Connection> connectionsOf(NodeId node, PortId port) const;

  Node& node(NodeId id);
  const Node& node(NodeId id) const;

  // The graph's nodes in insertion order. Order is how a face and an
  // enumeration are read off a graph; identity stays the id.
  std::vector<NodeId> nodeIds() const;

  std::size_t nodeCount() const { return nodes_.size(); }
  const std::vector<Connection>& connections() const { return connections_; }

private:
  friend class Node; // takes the id source at construction

  struct Entry {
    NodeId id;
    std::unique_ptr<Node> node;
  };

  // Takes ownership of a node built by addNode() above and gives it its id.
  // Its ports already have theirs: they were minted while it was constructed.
  NodeId adopt(std::unique_ptr<Node> node);

  IdSource& idSource() { return *ids_; }
  void validateRestoration(NodeId id, const NodePorts& ports) const;

  // Resolves an id, or throws std::invalid_argument(message) — the one place a
  // stale reference is caught, so every entry point below reports it the same
  // way. nodes_ is sorted by id (ids are minted in increasing order, entries
  // are appended in mint order, and deletion only removes), so this is a binary
  // search over the one list, with no side table to fall out of step with it.
  const Entry& require(NodeId id, const char* message) const;

  std::unique_ptr<IdSource> ids_; // separately allocated: see Ids.h
  std::vector<Entry> nodes_;
  std::vector<Connection> connections_;
  const NodePorts* restored_ports_ = nullptr;
};

} // namespace modular
