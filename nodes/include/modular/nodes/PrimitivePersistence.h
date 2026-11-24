#pragma once

#include "modular/GraphPersistence.h"

namespace modular::nodes {

// Authored primitive settings and owned MIDI content, including the Windows
// output codec on Windows. Other kinds require their own codec; no GUI state
// or prepared/runtime activity is serialized.
std::span<const NodeCodec> primitiveCodecs();

// The soundfont node's codec, modular.soundfont/1: the loaded file's bytes,
// whole, and its preset and voice limit. It stays out of primitiveCodecs(), so
// a client with no presentation for the node, such as the Windows editor,
// refuses a graph holding one at Open with the ordinary unsupported-kind error.
// A client that saves soundfont nodes passes it beside primitiveCodecs().
std::span<const NodeCodec> soundFontCodecs();

} // namespace modular::nodes
