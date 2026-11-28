#include "modular/Node.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "modular/Graph.h"

namespace modular {

namespace {

// Checked wherever a port type enters a node, not in PortType::audio(): an
// aggregate-initialized PortType never passes through the factory.
void requireAudioChannels(PortType type, const char* operation) {
  if (type.domain == Domain::Audio && type.channels == 0)
    throw std::invalid_argument(std::string(operation) +
                                ": an audio port needs at least one channel");
}

} // namespace

// A node is built into a graph and takes its stable id source from it.
Node::Node(Graph& graph)
    : ids_(&graph.idSource()), restored_ports_(graph.restored_ports_) {}

PortId Node::addPort(std::vector<PortSpec>& ports,
                     const std::vector<PortSpec>* restored,
                     PortType type, std::string name) {
  requireAudioChannels(type, "declare");
  if (restored) {
    if (ports.size() >= restored->size() || (*restored)[ports.size()].type != type)
      throw std::invalid_argument("restore: port declaration does not match node");
    ports.push_back((*restored)[ports.size()]);
  } else {
    ports.push_back({ids_->mint(), type, std::move(name)});
  }
  return ports.back().id;
}

void Node::finishRestoration() {
  if (!restored_ports_ || inputs_.size() != restored_ports_->inputs.size() ||
      outputs_.size() != restored_ports_->outputs.size())
    throw std::invalid_argument("restore: node did not declare all saved ports");
  restored_ports_ = nullptr;
}

void Node::setPortTypes(Graph& graph, NodeId self,
                        std::span<const PortTypeChange> changes) {
  if (&graph.node(self) != this)
    throw std::invalid_argument("configure: node does not belong to this graph and id");
  auto nextInputs = inputs_;
  auto nextOutputs = outputs_;
  for (const auto& change : changes) {
    requireAudioChannels(change.type, "configure");
    bool found = false;
    for (auto* ports : {&nextInputs, &nextOutputs}) {
      for (auto& port : *ports) {
        if (port.id != change.port) continue;
        port.type = change.type;
        found = true;
      }
    }
    if (!found) throw std::invalid_argument("configure: no such primitive port");
  }
  const auto typeOf = [](std::span<const PortSpec> ports, PortId id) {
    for (const auto& port : ports) if (port.id == id) return port.type;
    throw std::invalid_argument("configure: no such connected port");
  };
  for (const auto& wire : graph.connectionsOf(self)) {
    const auto outputs = wire.srcNode == self ? std::span<const PortSpec>(nextOutputs)
                                             : graph.node(wire.srcNode).outputs();
    const auto inputs = wire.dstNode == self ? std::span<const PortSpec>(nextInputs)
                                            : graph.node(wire.dstNode).inputs();
    if (typeOf(outputs, wire.srcPort) != typeOf(inputs, wire.dstPort))
      throw std::invalid_argument("configure: type change would invalidate a wire; "
                                  "disconnect first");
  }
  inputs_.swap(nextInputs);
  outputs_.swap(nextOutputs);
}

bool Node::erasePort(PortId port) {
  const auto byId = [port](const PortSpec& p) { return p.id == port; };
  for (std::vector<PortSpec>* list : {&inputs_, &outputs_}) {
    const auto at = std::find_if(list->begin(), list->end(), byId);
    if (at != list->end()) {
      // Erasing shifts the ports after it down one position — and that is the
      // point: a position was never identity, so no wire and no member of this
      // node has anything to re-derive.
      list->erase(at);
      return true;
    }
  }
  return false;
}

} // namespace modular
