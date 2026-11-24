#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "modular/Graph.h"
#include "modular/nodes/SoundFontNode.h"

namespace modular::web {

// One render's samples: the sample rate asked for, unchanged, and the left and
// right channels, always of equal length. A file of zero duration gives none.
struct Rendering {
  double sampleRate = 0;
  std::vector<float> left, right;
};

// A soundfont, parsed once, that renders Standard MIDI Files to stereo, clip
// after clip. It keeps one graph,
//
//   TriggerAt(0) -> MidiFile -> SoundFont -> the render core's stereo sink
//
// with no gain in it, so a rendering holds the soundfont node's own samples.
// Each render imports its bytes into the kept MIDI file node and builds a fresh
// Engine, so every node starts from scratch. Errors throw std::invalid_argument:
// the nodes' messages unchanged, the render core's own as "render: ...".
class Instrument {
public:
  // Plays the soundfont's lowest preset from a pool of `voiceLimit` voices. A
  // limit outside 1 to 65535 throws here; one below the voices a note of the
  // preset starts throws when a render prepares the node.
  explicit Instrument(
      std::span<const std::uint8_t> soundFont,
      std::uint32_t voiceLimit = nodes::SoundFontNode::kDefaultVoiceLimit);

  // Renders the file and at most two seconds past its end, then trims that
  // release tail after its last frame where either channel reaches -80 dBFS.
  // The file's own frames are always kept whole. A file longer than ten minutes
  // is refused before anything is sized for it, and a message a node reports
  // while rendering fails the render. Running out of memory throws
  // std::bad_alloc; the instrument renders on after any failure.
  Rendering render(std::span<const std::uint8_t> midiFile, double sampleRate);

private:
  Graph graph_;
  NodeId midi_ = kNoNode, sink_ = kNoNode;
};

} // namespace modular::web
