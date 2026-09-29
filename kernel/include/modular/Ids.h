#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace modular {

// Identity for the things a graph edit can name. An id is minted once and is
// never reused, so a reference to something deleted fails to resolve instead
// of silently addressing whatever took its place. Position is display and
// enumeration order; it is never identity.
using NodeId = std::uint32_t;
using PortId = std::uint32_t;

// Never minted, so a default-initialized id names nothing.
inline constexpr NodeId kNoNode = 0;
inline constexpr PortId kNoPort = 0;

// The one counter behind every id in a graph. One counter rather than one per
// kind, because ids of different kinds meet: a supernode's face is addressed
// by ids drawn from its inner graph, and two of them must differ whatever they
// name.
//
// A graph allocates its source separately from itself. Graph is movable and a
// Supernode holds one by value, so anything that keeps hold of the source must
// not be left pointing into a moved-from graph. Every node does keep hold of
// it — that is how a node mints its own port ids (see Node's constructor).
class IdSource {
public:
  explicit IdSource(std::uint64_t next = 1) : next_(next) {
    if (next == 0 || next > std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1)
      throw std::invalid_argument("identity: invalid next id");
  }
  std::uint32_t mint() {
    requireAvailable(1);
    return static_cast<std::uint32_t>(next_++);
  }
  std::uint64_t next() const { return next_; }
  void requireAvailable(std::uint64_t count) const {
    const auto remaining = std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1 - next_;
    if (count > remaining) throw std::invalid_argument("identity: ids exhausted");
  }

private:
  std::uint64_t next_;
};

} // namespace modular
