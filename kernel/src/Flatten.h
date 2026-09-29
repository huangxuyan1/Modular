#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "modular/Graph.h"
#include "modular/Node.h"

// Kernel-internal (deliberately not under include/modular): the executed graph
// is the engine's business, and no node or application ever names it.

namespace modular {

// One output port of an already-resolved node. Both ends are positions, not
// ids: the executed graph has no ids of its own, an id only means something
// relative to one Graph, and this is drawn from a whole hierarchy of them.
// Resolution is where authored ids turn into these — the compile chooses the
// table shape, and nothing outside the engine sees it.
struct SourcePort {
  std::size_t node;      // index into the resolved node vector
  std::uint32_t port;    // position in that node's outputs()
};

// A primitive node of the executed graph, with each of its input ports already
// resolved to the port that actually feeds it. std::nullopt = unconnected, so
// the engine substitutes that domain's default.
struct ResolvedNode {
  Node* node = nullptr;
  std::vector<std::optional<SourcePort>> inputs; // parallel to node->inputs()
};

// Flattens the authored graph by resolution: enumerates the primitive nodes,
// recursing through supernode shells, and traces every input back through
// boundary nodes to the primitive output that really feeds it. Supernodes and
// boundary nodes do not appear in the result — they are the thing being erased.
//
// No flat Graph is materialized: nodes are stateful instances the authored
// graph owns, so a copy would need a clone contract (rejected in ADR-0001).
// The result borrows the authored graph's nodes and is only valid while it
// lives unedited.
//
// Throws std::invalid_argument if resolution cannot terminate — see the cycle
// note in the implementation.
std::vector<ResolvedNode> flatten(Graph& graph);

} // namespace modular
