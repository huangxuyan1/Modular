// PROTOTYPE — throwaway. Part 0 of the ticket-11 probe.
//
// This file is EXPECTED NOT TO COMPILE. run.sh compiles it and fails loudly if
// it *succeeds*. It is the falsifiable half of the payload-size claim: not an
// opinion about sizes, a static_assert in the shipped kernel.
//
// HISTORICAL NOTE: this originally used a 20-byte VST3 NoteOnEvent, and that is
// what proved the 16-byte payload could not carry an ordinary note. The fix
// landed (kInlineBytes = 32, ADR-0002), so that case now compiles. The payload
// below is the *next* thing over the line — a note plus VST3's ppqPosition, at
// 40 bytes — kept so the probe still demonstrates that the guard is a compile
// error rather than a runtime surprise. Modular deliberately does not carry ppq
// on events; a node reads it from a value input.

#include "modular/Buffers.h"

#include <cstdint>

// VST3 ivstevents.h NoteOnEvent, verbatim field-for-field.
struct Vst3NoteOnBody {
  std::int16_t channel;
  std::int16_t pitch;
  float tuning;
  float velocity;
  std::int32_t length;
  std::int32_t noteId;
};

struct NoteWithPpq { // 40 bytes: over the line even after the widening
  Vst3NoteOnBody note;
  double ppqPosition;
  std::uint16_t flags;
};

int main() {
  auto e = modular::Event::make(0u, NoteWithPpq{});
  return static_cast<int>(e.offset);
}
