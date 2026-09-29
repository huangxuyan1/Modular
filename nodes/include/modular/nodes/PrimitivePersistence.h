#pragma once

#include "modular/GraphPersistence.h"

namespace modular::nodes {

// Authored primitive settings and owned MIDI content, including the Windows
// output codec on Windows. Other kinds require their own codec; no GUI state
// or prepared/runtime activity is serialized.
std::span<const NodeCodec> primitiveCodecs();

} // namespace modular::nodes
