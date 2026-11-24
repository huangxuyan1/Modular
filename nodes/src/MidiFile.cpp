#include "modular/nodes/MidiFile.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace modular::nodes {
namespace {

[[noreturn]] void invalid(const char* reason) {
  throw std::invalid_argument(std::string("midi file: ") + reason);
}

// Every length is checked against its containing chunk, not just the file:
// a malformed event must not consume the next track as its payload.
class Reader {
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
  bool empty() const { return bytes_.empty(); }
  std::span<const std::uint8_t> take(std::size_t count) {
    if (count > bytes_.size()) invalid("truncated chunk or event");
    const auto result = bytes_.first(count);
    bytes_ = bytes_.subspan(count);
    return result;
  }
  std::uint8_t byte() { return take(1)[0]; }
  std::uint32_t integer(std::size_t count) {
    std::uint32_t result = 0;
    for (const auto b : take(count)) result = (result << 8) | b;
    return result;
  }
  std::uint32_t variable() {
    std::uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
      const auto b = byte();
      result = (result << 7) | (b & 0x7f);
      if ((b & 0x80) == 0) return result;
    }
    invalid("variable-length quantity exceeds four bytes");
  }

private:
  std::span<const std::uint8_t> bytes_;
};

struct Timed {
  std::uint64_t tick;
  MidiMsg message{};
  std::uint32_t tempo = 0; // nonzero marks a tempo event
};

bool tag(std::span<const std::uint8_t> bytes, std::string_view expected) {
  return std::equal(bytes.begin(), bytes.end(), expected.begin(), expected.end());
}

std::uint64_t readTrack(Reader track, std::vector<Timed>& events,
                        std::uint32_t& skippedSysEx, bool allowTempo) {
  std::uint64_t tick = 0;
  std::uint8_t running = 0;
  while (!track.empty()) {
    const auto delta = track.variable();
    if (tick > std::numeric_limits<std::uint64_t>::max() - delta)
      invalid("tick count overflow");
    tick += delta;
    const auto first = track.byte();
    if (first == 0xff) {
      running = 0;
      const auto type = track.byte();
      const auto length = track.variable();
      const auto data = track.take(length);
      if (type == 0x2f) {
        if (length != 0 || !track.empty()) invalid("invalid end-of-track event");
        return tick;
      }
      if (type == 0x51) {
        if (!allowTempo) invalid("format 1 tempo must be in the first track");
        if (length < 3) invalid("truncated tempo event");
        const std::uint32_t tempo = (data[0] << 16) | (data[1] << 8) | data[2];
        if (tempo == 0) invalid("zero tempo");
        events.push_back({tick, {}, tempo});
      }
    } else if (first == 0xf0 || first == 0xf7) {
      running = 0;
      track.take(track.variable());
      ++skippedSysEx;
    } else {
      const bool hasStatus = (first & 0x80) != 0;
      const auto status = hasStatus ? first : running;
      if (status < 0x80 || status >= 0xf0) invalid("invalid channel/running status");
      running = status;
      const auto data1 = hasStatus ? track.byte() : first;
      const auto kind = status & 0xf0;
      const std::uint8_t data2 = (kind == 0xc0 || kind == 0xd0) ? 0 : track.byte();
      if (data1 >= 0x80 || data2 >= 0x80) invalid("channel data byte has status bit");
      events.push_back({tick, MidiMsg{status, data1, data2}, 0});
    }
  }
  invalid("missing end-of-track event");
}

} // namespace

MidiFile MidiFile::read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) invalid("cannot open input file");
  const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  if (file.bad()) invalid("failed to read input file");
  return parse(bytes);
}

MidiFile MidiFile::fromEntries(std::vector<Entry> entries, double durationSeconds,
                              std::uint32_t skippedSysEx) {
  if (!std::isfinite(durationSeconds) || durationSeconds < 0)
    invalid("invalid stored duration");
  double previous = 0;
  for (const auto& entry : entries) {
    if (!std::isfinite(entry.seconds) || entry.seconds < previous ||
        entry.seconds > durationSeconds)
      invalid("invalid stored timestamp order or range");
    if (!entry.message.isValidChannelMessage())
      invalid("invalid stored channel message");
    previous = entry.seconds;
  }
  MidiFile result;
  result.entries_ = std::move(entries);
  result.duration_seconds_ = durationSeconds;
  result.skipped_sysex_ = skippedSysEx;
  return result;
}

MidiFile MidiFile::parse(std::span<const std::uint8_t> bytes) {
  Reader file(bytes);
  if (!tag(file.take(4), "MThd")) invalid("missing MThd header");
  const auto headerLength = file.integer(4);
  Reader header(file.take(headerLength));
  const auto format = header.integer(2);
  const auto trackCount = header.integer(2);
  const auto division = header.integer(2);
  if (format > 1) invalid("only formats 0 and 1 are supported");
  if (trackCount == 0 || (format == 0 && trackCount != 1)) invalid("invalid track count");
  if ((division & 0x8000) != 0) invalid("SMPTE timing is not supported");
  if (division == 0) invalid("zero ticks per quarter note");

  MidiFile result;
  std::vector<Timed> events;
  std::uint64_t endTick = 0;
  std::uint32_t tracks = 0;
  while (!file.empty()) {
    const auto chunkTag = file.take(4);
    const auto length = file.integer(4);
    Reader chunk(file.take(length));
    if (!tag(chunkTag, "MTrk")) continue; // extensions are length-delimited
    if (tracks == trackCount) invalid("more tracks than declared");
    endTick = std::max(endTick,
        readTrack(chunk, events, result.skipped_sysex_, format == 0 || tracks == 0));
    ++tracks;
  }
  if (tracks != trackCount) invalid("fewer tracks than declared");

  // Tracks were appended in file order; stable sorting preserves both that
  // tie-breaker and the original ordering of simultaneous events in a track.
  std::stable_sort(events.begin(), events.end(),
                   [](const Timed& a, const Timed& b) { return a.tick < b.tick; });
  std::uint64_t previous = 0;
  std::uint32_t tempo = 500000;
  double seconds = 0;
  for (const auto& event : events) {
    seconds += static_cast<double>(event.tick - previous) * tempo /
               (static_cast<double>(division) * 1000000);
    previous = event.tick;
    if (event.tempo != 0) tempo = event.tempo;
    else result.entries_.push_back({seconds, event.message});
  }
  result.duration_seconds_ = seconds + static_cast<double>(endTick - previous) *
      tempo / (static_cast<double>(division) * 1000000);
  return result;
}

} // namespace modular::nodes
