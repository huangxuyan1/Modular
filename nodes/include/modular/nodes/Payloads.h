#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "modular/Buffers.h"

// Payload subtypes live in the node library, not the kernel: the kernel treats
// subtype ids as opaque and never interprets payload bytes. This file is the
// beginning of a "standard library" of types — user code can define its own
// subtypes the same way (ids must merely be unique within a project; a real
// registry is a later problem).

namespace modular::nodes {

namespace subtype {
inline constexpr std::uint32_t Trigger = 1; // event: fire, no data
inline constexpr std::uint32_t Midi = 2;    // event: MidiMsg
inline constexpr std::uint32_t Float = 3;   // value: float
inline constexpr std::uint32_t Text = 4;    // value: UTF-8 bytes, any length
} // namespace subtype

// Text is a value-domain convention: the slot's bytes are the string. The
// kernel just stores bytes; these helpers are the whole "text type".
inline void setText(ValueSlot& slot, std::string_view text) {
  slot.setBytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
}
inline std::string getText(const ValueView& v) {
  return {reinterpret_cast<const char*>(v.data()), v.size()};
}

struct TriggerPayload {}; // an instant with no data

struct MidiMsg {
  std::uint8_t status = 0; // e.g. 0x90 note-on, 0x80 note-off (channel in low nibble)
  std::uint8_t data1 = 0;  // note number
  std::uint8_t data2 = 0;  // velocity

  bool isValidChannelMessage() const {
    const auto kind = status & 0xf0;
    return status >= 0x80 && status < 0xf0 && data1 < 0x80 && data2 < 0x80 &&
           ((kind != 0xc0 && kind != 0xd0) || data2 == 0);
  }

  bool isNoteOn() const { return (status & 0xF0) == 0x90 && data2 > 0; }
  bool isNoteOff() const {
    return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0);
  }
};

} // namespace modular::nodes
