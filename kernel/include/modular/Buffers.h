#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace modular {

// ---------------------------------------------------------------------------
// Audio: planar float buffers. The engine owns storage; nodes get views that
// are valid only for the duration of one process() call.
// ---------------------------------------------------------------------------

struct ConstAudioView {
  const float* const* channels = nullptr;
  std::uint32_t numChannels = 0;
  std::uint32_t numFrames = 0;
};

struct AudioView {
  float* const* channels = nullptr;
  std::uint32_t numChannels = 0;
  std::uint32_t numFrames = 0;
};

// ---------------------------------------------------------------------------
// Events: an event is an instant — a frame offset within the current block
// plus a small POD payload. The intra-block offset is the event domain's core
// mechanic; it is what makes timing sample-accurate and block-size-independent.
// The kernel never interprets payload bytes; typed access is via as<T>().
// An event carries no musical position: a node needing ppq reads a value input.
// ---------------------------------------------------------------------------

struct Event {
  // Sized by the plugin oracle: the widest ordinary note body it defines plus
  // headroom. Fixed stride is a contract choice, not a performance one — it keeps
  // overflow a compile error below, and an Event a copyable value. Rationale,
  // measurements and rejected alternatives: ADR-0002.
  static constexpr std::size_t kInlineBytes = 32;

  std::uint32_t offset = 0; // frame within block, in [0, numFrames)
  std::array<std::byte, kInlineBytes> payload{};

  template <class T>
  static Event make(std::uint32_t offset, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "event payloads are POD");
    static_assert(sizeof(T) <= kInlineBytes, "payload too large for inline event");
    Event e;
    e.offset = offset;
    std::memcpy(e.payload.data(), &v, sizeof(T));
    return e;
  }

  template <class T>
  T as() const {
    static_assert(std::is_trivially_copyable_v<T>, "event payloads are POD");
    static_assert(sizeof(T) <= kInlineBytes, "payload too large for inline event");
    T v;
    std::memcpy(&v, payload.data(), sizeof(T));
    return v;
  }
};

// Per-block event list, kept sorted by offset (stable: equal offsets keep
// push order). Cleared by the engine before each block.
//
// Iteration is forward-only by design — there is deliberately no operator[], so
// the surface stays cursor-shaped. Don't add indexed access back without first
// settling the variable-stride question it would foreclose (ADR-0002).
//
// NOTE: uses std::vector; fine for the single-threaded offline v1 engine.
// A real-time engine will preallocate.
class EventBuffer {
public:
  void push(const Event& e) {
    // Insertion keeping stable order; per-block lists are tiny.
    auto it = events_.end();
    while (it != events_.begin() && (it - 1)->offset > e.offset) --it;
    events_.insert(it, e);
  }

  void clear() { events_.clear(); }
  bool empty() const { return events_.empty(); }
  std::size_t size() const { return events_.size(); }
  const Event* begin() const { return events_.data(); }
  const Event* end() const { return events_.data() + events_.size(); }

private:
  std::vector<Event> events_;
};

// ---------------------------------------------------------------------------
// Values: a held value — always readable, changes only at discrete moments.
// Storage persists across blocks (unlike events) and is *size-free*: a value
// is whatever bytes its subtype needs (a float, a chunk of text, ...). The
// kernel stores bytes and never interprets them. Values are read at block
// rate, not per-sample, so there is no fixed-stride constraint here (unlike
// events). Writes may allocate; that is fine for the offline v1 engine and a
// known question for a future real-time engine.
//
// v1 is block-latched: valueAt() ignores the frame offset. The accessor *is*
// the contract; sample-accurate changes can arrive later without rewriting
// nodes. An empty slot (unconnected input) reads as zero / empty bytes.
// ---------------------------------------------------------------------------

class ValueSlot {
public:
  template <class T>
  void set(const T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "use setBytes for non-POD values");
    blob_.resize(sizeof(T));
    std::memcpy(blob_.data(), &v, sizeof(T));
  }

  // Reads as much of T as the slot holds; missing bytes are zero, so an
  // unconnected input's empty slot yields a zeroed T.
  template <class T>
  T get() const {
    static_assert(std::is_trivially_copyable_v<T>, "use bytes() for non-POD values");
    T v{};
    const std::size_t n = blob_.size() < sizeof(T) ? blob_.size() : sizeof(T);
    if (n > 0) std::memcpy(&v, blob_.data(), n);
    return v;
  }

  void setBytes(const std::byte* data, std::size_t size) {
    blob_.assign(data, data + size);
  }
  const std::byte* data() const { return blob_.data(); }
  std::size_t size() const { return blob_.size(); }

private:
  std::vector<std::byte> blob_; // empty = unset: reads as zero
};

class ValueView {
public:
  ValueView() = default;
  explicit ValueView(const ValueSlot* slot) : slot_(slot) {}

  template <class T>
  T valueAt(std::uint32_t /*frameOffset*/) const {
    assert(slot_ != nullptr);
    return slot_->get<T>(); // v1: block-latched
  }

  const std::byte* data() const { assert(slot_); return slot_->data(); }
  std::size_t size() const { assert(slot_); return slot_->size(); }

private:
  const ValueSlot* slot_ = nullptr;
};

} // namespace modular
