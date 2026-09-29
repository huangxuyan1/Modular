#pragma once

#include <cstdint>

namespace modular {

// The three port domains, distinguished by *when* a port has a value:
//   Audio — dense, a value at every sample; ports carry buffers.
//   Value — held/latched; readable at any time; changes at discrete moments.
//   Event — exists only at instants; empty unless something fired.
enum class Domain : std::uint8_t { Audio, Value, Event };

// A port's full type. For Audio, `sub` is the channel count (audio<N>).
// For Value/Event, `sub` is an opaque subtype id — the kernel never
// interprets it, it only checks equality when connecting. Subtype ids are
// defined by node libraries (see nodes/), not here: the kernel does not
// know what MIDI, triggers, or floats are.
struct PortType {
  Domain domain{Domain::Audio};
  std::uint32_t sub{0};

  static constexpr PortType audio(std::uint32_t channels) {
    return {Domain::Audio, channels};
  }
  static constexpr PortType value(std::uint32_t subtypeId) {
    return {Domain::Value, subtypeId};
  }
  static constexpr PortType event(std::uint32_t subtypeId) {
    return {Domain::Event, subtypeId};
  }

  friend constexpr bool operator==(PortType a, PortType b) {
    return a.domain == b.domain && a.sub == b.sub;
  }
  friend constexpr bool operator!=(PortType a, PortType b) { return !(a == b); }
};

} // namespace modular
