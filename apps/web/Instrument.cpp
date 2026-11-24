#include "Instrument.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "StereoSinkNode.h"
#include "modular/Engine.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/MidiFileNode.h"

namespace modular::web {
namespace {

using nodes::MidiFileNode;
using nodes::SoundFontNode;
using nodes::TriggerAtNode;

// The render core's own choice: no node's output depends on the block size.
constexpr std::uint32_t kBlockFrames = 512;
// The longest file a render accepts. A longer one is refused before anything is
// sized for it: ten minutes of stereo floats at 48 kHz already take 230 MB.
constexpr int kLongestFileMinutes = 10;
// The release tail renders for at most this long past the file's end.
constexpr double kTailSeconds = 2;
// The tail is kept through its last frame at or above -80 dBFS. A float
// literal rather than computed, so every build compares against the same value.
constexpr float kTailThreshold = 1e-4f;

// The file's frames, whole, then the tail through its last frame where either
// channel reaches the threshold.
std::size_t trimmedLength(const Channels& channels, std::size_t fileFrames) {
  for (std::size_t end = channels.left.size(); end > fileFrames; --end)
    if (std::abs(channels.left[end - 1]) >= kTailThreshold ||
        std::abs(channels.right[end - 1]) >= kTailThreshold)
      return end;
  return fileFrames;
}

// The most frames a channel can hold. On wasm32 a vector of floats holds fewer
// than size_t counts, so a larger rendering would fail as a bare length error.
std::size_t mostFrames() {
  return std::min(std::numeric_limits<std::size_t>::max(), std::vector<float>().max_size());
}

// Refuses a file longer than the limit, or one whose rendering could not be
// counted at this rate, from its duration alone: before any node prepares at
// the rate and before anything is sized for the rendering.
void checkLength(double fileSeconds, double sampleRate) {
  if (fileSeconds > 60.0 * kLongestFileMinutes)
    throw std::invalid_argument("render: file is longer than " +
                                std::to_string(kLongestFileMinutes) + " minutes");
  if ((fileSeconds + kTailSeconds) * sampleRate >= static_cast<double>(mostFrames()))
    throw std::invalid_argument("render: too many frames");
}

// The file's frames plus the whole frames of the tail: the sink's size. Counted
// in size_t, which is 32 bits on wasm32, so a sum that does not fit throws
// rather than wrapping.
std::size_t renderedFrames(std::uint64_t fileFrames, double sampleRate) {
  const auto most = mostFrames();
  const double tail = std::floor(kTailSeconds * sampleRate);
  if (tail >= static_cast<double>(most) || fileFrames > most - static_cast<std::size_t>(tail))
    throw std::invalid_argument("render: too many frames");
  return static_cast<std::size_t>(fileFrames) + static_cast<std::size_t>(tail);
}

} // namespace

Instrument::Instrument(std::span<const std::uint8_t> soundFont,
                       std::uint32_t voiceLimit) {
  const auto startId = graph_.addNode<TriggerAtNode>(0);
  midi_ = graph_.addNode<MidiFileNode>();
  const auto synthId = graph_.addNode<SoundFontNode>();
  sink_ = graph_.addNode<StereoSinkNode>();
  auto& start = static_cast<TriggerAtNode&>(graph_.node(startId));
  auto& midi = static_cast<MidiFileNode&>(graph_.node(midi_));
  auto& synth = static_cast<SoundFontNode&>(graph_.node(synthId));
  auto& sink = static_cast<StereoSinkNode&>(graph_.node(sink_));
  synth.load(soundFont);
  synth.setVoiceLimit(voiceLimit);
  graph_.connect(startId, start.trigger(), midi_, midi.trigger());
  graph_.connect(midi_, midi.midi(), synthId, synth.midiIn());
  graph_.connect(synthId, synth.out(), sink_, sink.in());
}

Rendering Instrument::render(std::span<const std::uint8_t> midiFile, double sampleRate) {
  if (!std::isfinite(sampleRate) || sampleRate <= 0)
    throw std::invalid_argument("render: sample rate must be finite and positive");
  auto& midi = static_cast<MidiFileNode&>(graph_.node(midi_));
  auto& sink = static_cast<StereoSinkNode&>(graph_.node(sink_));
  midi.import(midiFile);
  checkLength(midi.file().durationSeconds(), sampleRate);

  std::size_t fileFrames = 0;
  {
    Engine engine(graph_, sampleRate, kBlockFrames);
    // The file's frames are known once the Engine has prepared its node at
    // this rate, so the sink is sized after the compile and before any block.
    // A file of zero duration gives zero frames, whatever a tail would hold.
    if (midi.durationFrames() == 0) return {sampleRate, {}, {}};
    const std::size_t frames = renderedFrames(midi.durationFrames(), sampleRate);
    fileFrames = static_cast<std::size_t>(midi.durationFrames());
    sink.setCapacity(frames);
    while (engine.streamTime() < frames)
      engine.process(static_cast<std::uint32_t>(
          std::min<std::uint64_t>(kBlockFrames, frames - engine.streamTime())));
    // A node reports a failure while processing rather than throwing, so the
    // render asks once every frame is rendered.
    if (auto error = engine.errorMessage(); !error.empty())
      throw std::invalid_argument(error);
  }

  // The rendering takes the sink's channels rather than copying them, so a
  // render holds one set of buffers.
  auto channels = sink.take();
  const auto length = trimmedLength(channels, fileFrames);
  channels.left.resize(length);
  channels.right.resize(length);
  return {sampleRate, std::move(channels.left), std::move(channels.right)};
}

} // namespace modular::web
