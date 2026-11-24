#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// Writes small Standard MIDI Files in memory, so a test needs no MIDI file on
// disk, as SoundFontTestFiles.h writes soundfonts. A file is format 0 at 480
// ticks per quarter; with no tempo event a quarter lasts 500,000 us, so 960
// ticks make a second.
namespace miditest {

using Bytes = std::vector<std::uint8_t>;

// A channel message, or a meta event from its 0xff on, at a tick.
struct Event {
  std::uint32_t tick;
  Bytes bytes;
};

inline Event noteOn(std::uint32_t tick, int key, int velocity, int channel = 0) {
  return {tick, {static_cast<std::uint8_t>(0x90 | channel),
                 static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(velocity)}};
}

inline Event noteOff(std::uint32_t tick, int key, int channel = 0) {
  return {tick, {static_cast<std::uint8_t>(0x80 | channel),
                 static_cast<std::uint8_t>(key), 0}};
}

inline Event tempo(std::uint32_t tick, std::uint32_t microsecondsPerQuarter) {
  return {tick, {0xff, 0x51, 3, static_cast<std::uint8_t>(microsecondsPerQuarter >> 16),
                 static_cast<std::uint8_t>(microsecondsPerQuarter >> 8),
                 static_cast<std::uint8_t>(microsecondsPerQuarter)}};
}

namespace detail {

inline void bigEndian(Bytes& out, std::uint32_t value, int size) {
  for (int i = size - 1; i >= 0; --i)
    out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

// Seven bits a byte, most significant first.
inline void variableLength(Bytes& out, std::uint32_t value) {
  std::uint8_t groups[5];
  int count = 0;
  do {
    groups[count++] = static_cast<std::uint8_t>(value & 0x7f);
    value >>= 7;
  } while (value != 0);
  while (count > 1) out.push_back(static_cast<std::uint8_t>(groups[--count] | 0x80));
  out.push_back(groups[0]);
}

} // namespace detail

// A file of `events`, ending at `endTick`. Events at one tick keep the order
// given.
inline Bytes midiFile(std::vector<Event> events, std::uint32_t endTick) {
  std::stable_sort(events.begin(), events.end(),
                   [](const Event& a, const Event& b) { return a.tick < b.tick; });
  Bytes track;
  std::uint32_t at = 0;
  for (const auto& event : events) {
    detail::variableLength(track, event.tick - at);
    at = event.tick;
    track.insert(track.end(), event.bytes.begin(), event.bytes.end());
  }
  detail::variableLength(track, endTick - at);
  track.insert(track.end(), {0xff, 0x2f, 0});
  Bytes file{'M', 'T', 'h', 'd'};
  detail::bigEndian(file, 6, 4);
  detail::bigEndian(file, 0, 2); // format 0
  detail::bigEndian(file, 1, 2); // one track
  detail::bigEndian(file, 480, 2);
  file.insert(file.end(), {'M', 'T', 'r', 'k'});
  detail::bigEndian(file, static_cast<std::uint32_t>(track.size()), 4);
  file.insert(file.end(), track.begin(), track.end());
  return file;
}

} // namespace miditest
