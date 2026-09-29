#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

#include "Workspace.h"

namespace modular {

// Both fields belong to the node codec. The shared container preserves them
// without interpreting settings or stored payloads. Execution state is excluded
// by the codec, which must read only authored state when a graph is running.
struct NodeContent {
  std::string settings;
  std::vector<std::byte> stored;
};

struct NodeCodec {
  std::string kind; // stable, versioned name, e.g. "modular.gain/1"
  bool (*matches)(const Node&);
  NodeContent (*save)(const Node&);
  NodeId (*restore)(Graph&, NodeId, const NodePorts&, const NodeContent&);
};

// MODULAR_GRAPH version 1. Unsupported kinds/structure and malformed content
// throw invalid_argument. Open builds a separate idle workspace; a caller only
// replaces its current workspace after success. No Engine is created here.
void saveGraph(const Workspace&, std::ostream&, std::span<const NodeCodec>);
Workspace openGraph(std::istream&, std::span<const NodeCodec>);

// Unicode-native filesystem paths. Save replaces the destination only after
// complete encoding and a successful temporary-file write/close.
void saveGraphFile(const Workspace&, const std::filesystem::path&,
                   std::span<const NodeCodec>);
Workspace openGraphFile(const std::filesystem::path&, std::span<const NodeCodec>);

} // namespace modular
