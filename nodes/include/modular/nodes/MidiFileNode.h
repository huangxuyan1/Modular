#pragma once

#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>

#include "MidiFile.h"
#include "MidiNodes.h"

namespace modular::nodes {

// The same trigger -> MIDI contract as MidiBlockNode, with file time converted
// to stream frames during prepare. Loading a file never starts playback itself.
class MidiFileNode : public MidiBlockNode {
public:
  explicit MidiFileNode(Graph& graph) : MidiBlockNode(graph, {}) {}
  MidiFileNode(Graph& graph, const std::filesystem::path& path)
      : MidiFileNode(graph, MidiFile::read(path)) {}
  MidiFileNode(Graph& graph, MidiFile file)
      : MidiBlockNode(graph, {}), file_(std::move(file)) {}

  bool ready() const { return file_.has_value(); }
  const std::string& importError() const { return import_error_; }
  // Edit-only, from a file or from its bytes. Failed replacement preserves any
  // previous, self-contained content.
  void import(const std::filesystem::path& path) {
    replace([&] { return MidiFile::read(path); });
  }
  void import(std::span<const std::uint8_t> bytes) {
    replace([&] { return MidiFile::parse(bytes); });
  }

  void prepare(const PrepareInfo& info) override {
    if (!ready()) throw std::invalid_argument("midi file: select and load a MIDI file first");
    if (!std::isfinite(info.sampleRate) || info.sampleRate <= 0)
      throw std::invalid_argument("midi file: invalid sample rate");
    const auto frameAt = [&](double seconds) {
      const double frame = std::round(seconds * info.sampleRate);
      if (!std::isfinite(frame) || frame < 0 ||
          frame >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
        throw std::invalid_argument("midi file: frame count overflow");
      return static_cast<std::uint64_t>(frame);
    };
    std::vector<Entry> content;
    content.reserve(file_->entries().size());
    for (const auto& entry : file_->entries())
      content.push_back({frameAt(entry.seconds), entry.message});
    duration_frames_ = frameAt(file_->durationSeconds());
    replaceContent(std::move(content), duration_frames_);
  }

  std::uint64_t durationFrames() const { return duration_frames_; } // after prepare
  const MidiFile& file() const {
    if (!ready()) throw std::invalid_argument("midi file: no content loaded");
    return *file_;
  }

private:
  template <class Read>
  void replace(Read read) {
    try {
      auto replacement = read();
      file_ = std::move(replacement);
      import_error_.clear();
    } catch (const std::exception& error) {
      import_error_ = error.what();
      throw;
    }
  }

  std::optional<MidiFile> file_;
  std::string import_error_;
  std::uint64_t duration_frames_ = 0;
};

} // namespace modular::nodes
