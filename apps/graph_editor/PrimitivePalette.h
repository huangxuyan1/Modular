#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <filesystem>

#include "modular/Graph.h"

namespace modular::editor {

struct CreationSettings {
  float constant = 1;
  std::uint32_t channels = 1;
  std::uint32_t inputs = 2;
  std::filesystem::path directory = ".";
  std::string prefix = "take";
  friend bool operator==(const CreationSettings&, const CreationSettings&) = default;
};

// App presentation for the supported primitive types. Settings remain in
// the nodes; this describes how the inspector reads and edits their public APIs.
struct PrimitivePresentation {
  const wchar_t* name;
  const wchar_t* help;
  bool hasConstant;
  bool hasChannels;
  bool hasInputs;
  bool (*matches)(const Node&);
  NodeId (*create)(Graph&, const CreationSettings&);
  CreationSettings (*settings)(const Node&);
  void (*apply)(Graph&, NodeId, const CreationSettings&);
  bool hasRecording = false;
};

inline constexpr std::size_t kPaletteSize = 10;
const std::array<PrimitivePresentation, kPaletteSize>& palette();
const PrimitivePresentation& presentation(const Node& node);
std::wstring portTypeName(PortType type);
std::wstring widen(const std::string& utf8);

} // namespace modular::editor
