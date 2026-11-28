#pragma once

#include <cstdint>

namespace modular {

// The three port domains, distinguished by *when* a port has a value:
//   Audio — dense, a value at every sample; ports carry buffers.
//   Value — held/latched; readable at any time; changes at discrete moments.
//   Event — exists only at instants; empty unless something fired.
enum class Domain : std::uint8_t { Audio, Value, Event };

// A port's full type. `sub` is an opaque subtype id in every domain: the
// kernel never interprets it, it only checks equality when connecting. Subtype
// ids are defined by node libraries (see nodes/) or user code, not here: the
// kernel does not know what MIDI, triggers, floats or a spectrum are. Audio
// subtype 0 is unspecified audio, a concrete type rather than a wildcard.
//
// `channels` is audio's width, audio<N>, and the one field the kernel reads: it
// sizes audio buffers. An audio port has at least one channel; declaring one
// with none throws. Value and event ports have none.
struct PortType {
  Domain domain{Domain::Audio};
  std::uint32_t sub{0};
  std::uint32_t channels{0};

  static constexpr PortType audio(std::uint32_t channels, std::uint32_t subtypeId = 0) {
    return {Domain::Audio, subtypeId, channels};
  }
  static constexpr PortType value(std::uint32_t subtypeId) {
    return {Domain::Value, subtypeId, 0};
  }
  static constexpr PortType event(std::uint32_t subtypeId) {
    return {Domain::Event, subtypeId, 0};
  }

  friend constexpr bool operator==(PortType a, PortType b) {
    return a.domain == b.domain && a.sub == b.sub && a.channels == b.channels;
  }
  friend constexpr bool operator!=(PortType a, PortType b) { return !(a == b); }
};

} // namespace modular
