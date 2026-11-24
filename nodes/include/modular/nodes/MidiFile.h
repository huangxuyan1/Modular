#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "Payloads.h"

namespace modular::nodes {

// Standard MIDI File formats 0/1 with PPQ timing. File I/O and parsing happen
// during authoring, never in process(). Tempo is resolved to absolute time so
// a later prepare can choose the graph's sample rate without rereading the file.
// Times are double, never long double: its width differs between builds (64 bits
// under MSVC, 128 on wasm32), which put events on exact half-frame ties a frame apart.
class MidiFile {
public:
  struct Entry {
    double seconds;
    MidiMsg message;
  };

  static MidiFile read(const std::filesystem::path& path);
  static MidiFile parse(std::span<const std::uint8_t> bytes);
  // Reconstruct owned authored meaning, before sample-rate preparation.
  static MidiFile fromEntries(std::vector<Entry> entries, double durationSeconds,
                              std::uint32_t skippedSysEx = 0);

  const std::vector<Entry>& entries() const { return entries_; }
  double durationSeconds() const { return duration_seconds_; }
  std::uint32_t skippedSysExEvents() const { return skipped_sysex_; }

private:
  std::vector<Entry> entries_;
  double duration_seconds_ = 0;
  std::uint32_t skipped_sysex_ = 0;
};

} // namespace modular::nodes
