// PROTOTYPE — throwaway probe for .scratch/roadmap/issues/11-event-payload-shape.md
//
// Question: can today's 16-byte inline event payload carry an ordinary note,
// and if not, which candidate shape wins?
//
// This is a spike, not a design. It includes the *real* kernel header so every
// number about today's model is the shipped number. The three candidate shapes
// are implemented far enough to be pushed through the same workload and to make
// their costs show up, and no further.
//
// Build/run: ./run.sh

#include "modular/Buffers.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// Allocation counting. "No allocation in the block loop" is a kernel rule, so
// any candidate that allocates per block has to say so out loud.
// ---------------------------------------------------------------------------

namespace probe {
std::size_t allocCount = 0;
std::size_t allocBytes = 0;
bool counting = false;
} // namespace probe

void* operator new(std::size_t n) {
  if (probe::counting) {
    ++probe::allocCount;
    probe::allocBytes += n;
  }
  void* p = std::malloc(n);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// ---------------------------------------------------------------------------
// What the standards actually put in an ordinary note. Fields verbatim from
// the headers as recorded in research/01-plugin-host-obligations.md §5.3.
// ---------------------------------------------------------------------------

struct Vst3NoteOnBody { // ivstevents.h NoteOnEvent
  std::int16_t channel;
  std::int16_t pitch;
  float tuning;
  float velocity;
  std::int32_t length;
  std::int32_t noteId;
};

struct Vst3NoteOffBody { // ivstevents.h NoteOffEvent
  std::int16_t channel;
  std::int16_t pitch;
  float velocity;
  std::int32_t noteId;
  float tuning;
};

struct ClapNoteBody { // clap_event_note minus its 16-byte header
  std::int32_t noteId;
  std::int16_t portIndex;
  std::int16_t channel;
  std::int16_t key;
  double velocity;
};

// The largest *ordinary* (non-pointer) event body either standard defines, for
// the "what size, and on whose authority?" half of the ticket. Field list as
// recorded in research §3.2 from CLAP events.h; types are clap_id = uint32,
// cookie = void*. In Modular a parameter change is the VALUE domain, not an
// event -- so this is an upper bound on what the event payload could ever be
// asked to carry, not a requirement on it.
struct ClapParamValueBody {
  std::uint32_t paramId;
  void* cookie;
  std::int32_t noteId;
  std::int16_t portIndex;
  std::int16_t channel;
  std::int16_t key;
  double value;
};

struct Vst3DataEventBody { // SysEx and friends: size + type + out-of-line ptr
  std::uint32_t size;
  std::uint32_t type;
  const std::uint8_t* bytes;
};

// Today's node-library payload, for scale.
struct MidiMsg {
  std::uint8_t status;
  std::uint8_t data1;
  std::uint8_t data2;
};

// ---------------------------------------------------------------------------
// A Modular-shaped note payload. Modular does NOT need CLAP's 16-byte header:
// `size` is only needed if events are variable-stride, `time` is already
// Event::offset, and `space_id`/`type` are the *port's* subtype id. But one
// event port carries note-on AND note-off AND poly-pressure, so the variant tag
// cannot live on the port — it must live in the payload. That tag is the only
// header Modular owes.
// ---------------------------------------------------------------------------

enum class NoteKind : std::uint8_t { On, Off, PolyPressure, Expression };

struct NoteEvent {
  NoteKind kind;
  std::int16_t channel;
  std::int16_t pitch;
  std::int32_t noteId; // -1 when the source has no note id
  float tuning;        // cents, VST3 semantics
  float value;         // velocity / pressure / expression value
  std::int32_t length; // note-on only, 0 = unknown
};

// The same, if you also want VST3's per-event musical position (ppqPosition is
// in VST3's *common* header, so a conformant pass-through carries it).
struct NoteEventWithPpq {
  NoteEvent note;
  double ppqPosition;
  std::uint16_t flags;
};

// Out-of-line variant: the whole payload is a handle into a side arena.
struct BlobHandle {
  std::uint32_t arenaOffset;
  std::uint32_t size;
};

// ---------------------------------------------------------------------------
// Part 1 — does it fit today?
// ---------------------------------------------------------------------------

struct Row {
  const char* name;
  std::size_t size;
  std::size_t align;
};

template <class T>
Row row(const char* name) {
  return Row{name, sizeof(T), alignof(T)};
}

void part1_fit() {
  std::printf("\n=== Part 1: does an ordinary note fit today's payload? ===\n");
  std::printf("kernel Event::kInlineBytes = %zu, sizeof(Event) = %zu, alignof = %zu\n\n",
              modular::Event::kInlineBytes, sizeof(modular::Event),
              alignof(modular::Event));

  const Row rows[] = {
      row<MidiMsg>("MidiMsg (nodes/Payloads.h today)"),
      row<Vst3NoteOffBody>("VST3 NoteOffEvent body"),
      row<Vst3DataEventBody>("VST3 DataEvent body (ptr + size)"),
      row<Vst3NoteOnBody>("VST3 NoteOnEvent body"),
      row<ClapNoteBody>("CLAP clap_event_note body"),
      row<NoteEvent>("NoteEvent (Modular-shaped, tagged)"),
      row<NoteEventWithPpq>("NoteEvent + ppqPosition + flags"),
      row<ClapParamValueBody>("CLAP param_value body (value domain)"),
      row<BlobHandle>("BlobHandle (out-of-line)"),
  };

  std::printf("%-38s %6s %6s  %s\n", "payload", "bytes", "align", "fits in 16?");
  for (const Row& r : rows) {
    std::printf("%-38s %6zu %6zu  %s\n", r.name, r.size, r.align,
                r.size <= modular::Event::kInlineBytes ? "yes" : "NO");
  }

  std::printf("\nSmallest fixed inline size that fits each candidate note shape:\n");
  std::printf("  VST3 note-on alone .................. %zu\n", sizeof(Vst3NoteOnBody));
  std::printf("  CLAP note alone ..................... %zu\n", sizeof(ClapNoteBody));
  std::printf("  Modular tagged note ................. %zu\n", sizeof(NoteEvent));
  std::printf("  Modular tagged note + ppq + flags ... %zu\n", sizeof(NoteEventWithPpq));
}

// ---------------------------------------------------------------------------
// Candidate A — widen kInlineBytes. Same shape, bigger array.
// ---------------------------------------------------------------------------

template <std::size_t N>
struct FixedEvent {
  static constexpr std::size_t kInlineBytes = N;
  std::uint32_t offset = 0;
  alignas(8) std::array<std::byte, N> payload{};

  template <class T>
  static FixedEvent make(std::uint32_t offset, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "event payloads are POD");
    static_assert(sizeof(T) <= N, "payload too large for inline event");
    FixedEvent e;
    e.offset = offset;
    std::memcpy(e.payload.data(), &v, sizeof(T));
    return e;
  }

  template <class T>
  T as() const {
    static_assert(std::is_trivially_copyable_v<T>, "event payloads are POD");
    static_assert(sizeof(T) <= N, "payload too large for inline event");
    T v;
    std::memcpy(&v, payload.data(), sizeof(T));
    return v;
  }
};

template <std::size_t N>
class FixedEventBuffer {
public:
  using EventT = FixedEvent<N>;

  void push(const EventT& e) {
    auto it = events_.end();
    while (it != events_.begin() && (it - 1)->offset > e.offset) --it;
    events_.insert(it, e);
  }
  void clear() { events_.clear(); }
  std::size_t size() const { return events_.size(); }
  const EventT& operator[](std::size_t i) const { return events_[i]; }
  std::size_t capacityBytes() const { return events_.capacity() * sizeof(EventT); }

private:
  std::vector<EventT> events_;
};

// ---------------------------------------------------------------------------
// Candidate B — variable stride, CLAP-style. Records live contiguously in a
// byte arena: [uint32 size][uint32 offset][payload...], padded to 8.
// ---------------------------------------------------------------------------

class VarEventBuffer {
public:
  struct Header {
    std::uint32_t size;   // total record bytes including this header
    std::uint32_t offset; // frame within block
  };

  template <class T>
  void push(std::uint32_t offset, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "event payloads are POD");
    pushBytes(offset, &v, sizeof(T));
  }

  void pushBytes(std::uint32_t offset, const void* data, std::size_t n) {
    const std::size_t rec = pad8(sizeof(Header) + n);
    // Sorted, stable. A fixed-stride vector scans BACKWARD from the end, which
    // is O(1) for already-sorted arrival. A variable-stride arena cannot be
    // traversed backward at all (no stride, no footer), so it must either walk
    // forward from record 0 -- O(n) per push -- or keep the last offset cached
    // to catch the append case. `fastAppend` toggles that fast path so the
    // measurement separates "the shape is slower" from "my walk was naive".
    std::size_t at = arena_.size();
    if (!(fastAppend && offset >= lastOffset_ && !arena_.empty())) {
      std::size_t cur = 0;
      while (cur < arena_.size()) {
        const Header* h = headerAt(cur);
        if (h->offset > offset) { at = cur; break; }
        cur += h->size;
      }
    }
    if (at == arena_.size()) lastOffset_ = offset;
    // This is the cost the shape imposes: inserting mid-arena memmoves the tail.
    arena_.insert(arena_.begin() + static_cast<std::ptrdiff_t>(at), rec, std::byte{});
    Header h{static_cast<std::uint32_t>(rec), offset};
    std::memcpy(arena_.data() + at, &h, sizeof(Header));
    std::memcpy(arena_.data() + at + sizeof(Header), data, n);
    ++count_;
  }

  void clear() {
    arena_.clear();
    count_ = 0;
    lastOffset_ = 0;
  }
  std::size_t size() const { return count_; }
  std::size_t capacityBytes() const { return arena_.capacity(); }

  bool fastAppend = false;

  class Cursor {
  public:
    Cursor(const std::byte* base, std::size_t bytes) : base_(base), bytes_(bytes) {}
    bool done() const { return at_ >= bytes_; }
    void next() { at_ += header()->size; }
    std::uint32_t offset() const { return header()->offset; }
    std::size_t payloadBytes() const {
      return header()->size - sizeof(Header);
    }
    // The contract change: sizeof(T) <= capacity can no longer be a
    // static_assert, because capacity is a runtime property of the record.
    template <class T>
    T as() const {
      static_assert(std::is_trivially_copyable_v<T>, "event payloads are POD");
      T v{};
      const std::size_t n = payloadBytes() < sizeof(T) ? payloadBytes() : sizeof(T);
      std::memcpy(&v, base_ + at_ + sizeof(Header), n);
      return v;
    }

  private:
    const Header* header() const {
      return reinterpret_cast<const Header*>(base_ + at_);
    }
    const std::byte* base_;
    std::size_t bytes_;
    std::size_t at_ = 0;
  };

  Cursor cursor() const { return Cursor(arena_.data(), arena_.size()); }

private:
  static std::size_t pad8(std::size_t n) { return (n + 7u) & ~std::size_t{7}; }
  const Header* headerAt(std::size_t at) const {
    return reinterpret_cast<const Header*>(arena_.data() + at);
  }
  std::vector<std::byte> arena_;
  std::size_t count_ = 0;
  std::uint32_t lastOffset_ = 0;
};

// ---------------------------------------------------------------------------
// Candidate C — small inline (fits a note) + out-of-line handle for the rest.
// The buffer grows a payload arena, and the kernel grows a lifetime rule.
// ---------------------------------------------------------------------------

class HybridEventBuffer {
public:
  static constexpr std::size_t kInlineBytes = 32;
  using EventT = FixedEvent<kInlineBytes>;

  template <class T>
  void push(std::uint32_t offset, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "event payloads are POD");
    if constexpr (sizeof(T) <= kInlineBytes) {
      pushEvent(EventT::make(offset, v));
    } else {
      pushBlob(offset, &v, sizeof(T));
    }
  }

  // Oversized payloads: copied into the side arena, event carries a handle.
  void pushBlob(std::uint32_t offset, const void* data, std::size_t n) {
    BlobHandle h{static_cast<std::uint32_t>(blobs_.size()),
                 static_cast<std::uint32_t>(n)};
    blobs_.insert(blobs_.end(), static_cast<const std::byte*>(data),
                  static_cast<const std::byte*>(data) + n);
    pushEvent(EventT::make(offset, h));
  }

  // ...and reading one needs the buffer, not just the event. That is the
  // asymmetry: `Event::as<T>()` alone stops being sufficient.
  const std::byte* resolve(const BlobHandle& h) const {
    return blobs_.data() + h.arenaOffset;
  }

  void clear() {
    events_.clear();
    blobs_.clear();
  }
  std::size_t size() const { return events_.size(); }
  const EventT& operator[](std::size_t i) const { return events_[i]; }
  std::size_t capacityBytes() const {
    return events_.capacity() * sizeof(EventT) + blobs_.capacity();
  }

private:
  void pushEvent(const EventT& e) {
    auto it = events_.end();
    while (it != events_.begin() && (it - 1)->offset > e.offset) --it;
    events_.insert(it, e);
  }
  std::vector<EventT> events_;
  std::vector<std::byte> blobs_;
};

// ---------------------------------------------------------------------------
// Part 2 — push the same note stream through each shape.
// ---------------------------------------------------------------------------

struct Workload {
  std::vector<std::uint32_t> offsets;
  const char* name;
};

NoteEvent makeNote(std::uint32_t i) {
  NoteEvent n{};
  n.kind = (i % 2 == 0) ? NoteKind::On : NoteKind::Off;
  n.channel = static_cast<std::int16_t>(i % 16);
  n.pitch = static_cast<std::int16_t>(36 + (i % 48));
  n.noteId = static_cast<std::int32_t>(i);
  n.tuning = 0.0f;
  n.value = 0.8f;
  n.length = 0;
  return n;
}

struct Result {
  double nsPerEvent;
  std::size_t allocs;
  std::size_t bytesResident;
  std::uint64_t checksum; // proves the payloads actually round-tripped
};

template <class F>
Result timeIt(std::size_t blocks, const Workload& w, F&& body) {
  probe::allocCount = 0;
  probe::allocBytes = 0;
  probe::counting = true;
  const auto t0 = std::chrono::steady_clock::now();
  std::uint64_t checksum = 0;
  std::size_t resident = 0;
  for (std::size_t b = 0; b < blocks; ++b) body(w, checksum, resident);
  const auto t1 = std::chrono::steady_clock::now();
  probe::counting = false;
  const double ns =
      std::chrono::duration<double, std::nano>(t1 - t0).count() /
      static_cast<double>(blocks * w.offsets.size());
  return Result{ns, probe::allocCount, resident, checksum};
}

void part2_shapes(std::size_t blocks, const Workload& w) {
  std::printf("\n=== Part 2: %s, %zu events/block, %zu blocks ===\n", w.name,
              w.offsets.size(), blocks);
  std::printf("%-34s %10s %10s %10s\n", "shape", "ns/event", "allocs", "bytes");

  FixedEventBuffer<32> fixed32;
  auto r32 = timeIt(blocks, w, [&](const Workload& wl, std::uint64_t& sum, std::size_t& res) {
    fixed32.clear();
    for (std::size_t i = 0; i < wl.offsets.size(); ++i)
      fixed32.push(FixedEvent<32>::make(wl.offsets[i], makeNote(static_cast<std::uint32_t>(i))));
    for (std::size_t i = 0; i < fixed32.size(); ++i)
      sum += static_cast<std::uint64_t>(fixed32[i].as<NoteEvent>().pitch);
    res = fixed32.capacityBytes();
  });
  std::printf("%-34s %10.1f %10zu %10zu\n", "A: fixed inline 32", r32.nsPerEvent,
              r32.allocs, r32.bytesResident);

  FixedEventBuffer<64> fixed64;
  auto r64 = timeIt(blocks, w, [&](const Workload& wl, std::uint64_t& sum, std::size_t& res) {
    fixed64.clear();
    for (std::size_t i = 0; i < wl.offsets.size(); ++i)
      fixed64.push(FixedEvent<64>::make(wl.offsets[i], makeNote(static_cast<std::uint32_t>(i))));
    for (std::size_t i = 0; i < fixed64.size(); ++i)
      sum += static_cast<std::uint64_t>(fixed64[i].as<NoteEvent>().pitch);
    res = fixed64.capacityBytes();
  });
  std::printf("%-34s %10.1f %10zu %10zu\n", "A: fixed inline 64", r64.nsPerEvent,
              r64.allocs, r64.bytesResident);

  VarEventBuffer var;
  auto rv = timeIt(blocks, w, [&](const Workload& wl, std::uint64_t& sum, std::size_t& res) {
    var.clear();
    for (std::size_t i = 0; i < wl.offsets.size(); ++i)
      var.push(wl.offsets[i], makeNote(static_cast<std::uint32_t>(i)));
    for (auto c = var.cursor(); !c.done(); c.next())
      sum += static_cast<std::uint64_t>(c.as<NoteEvent>().pitch);
    res = var.capacityBytes();
  });
  std::printf("%-34s %10.1f %10zu %10zu\n", "B: variable stride (CLAP-style)",
              rv.nsPerEvent, rv.allocs, rv.bytesResident);

  VarEventBuffer varFast;
  varFast.fastAppend = true;
  auto rvf = timeIt(blocks, w, [&](const Workload& wl, std::uint64_t& sum, std::size_t& res) {
    varFast.clear();
    for (std::size_t i = 0; i < wl.offsets.size(); ++i)
      varFast.push(wl.offsets[i], makeNote(static_cast<std::uint32_t>(i)));
    for (auto c = varFast.cursor(); !c.done(); c.next())
      sum += static_cast<std::uint64_t>(c.as<NoteEvent>().pitch);
    res = varFast.capacityBytes();
  });
  std::printf("%-34s %10.1f %10zu %10zu\n", "B': variable stride + append path",
              rvf.nsPerEvent, rvf.allocs, rvf.bytesResident);
  if (rvf.checksum != rv.checksum)
    std::printf("  !! B' fast path changed the result -- it is not sort-equivalent\n");

  HybridEventBuffer hyb;
  auto rh = timeIt(blocks, w, [&](const Workload& wl, std::uint64_t& sum, std::size_t& res) {
    hyb.clear();
    for (std::size_t i = 0; i < wl.offsets.size(); ++i)
      hyb.push(wl.offsets[i], makeNote(static_cast<std::uint32_t>(i)));
    for (std::size_t i = 0; i < hyb.size(); ++i)
      sum += static_cast<std::uint64_t>(hyb[i].as<NoteEvent>().pitch);
    res = hyb.capacityBytes();
  });
  std::printf("%-34s %10.1f %10zu %10zu\n", "C: inline 32 + blob arena",
              rh.nsPerEvent, rh.allocs, rh.bytesResident);

  if (r32.checksum != rv.checksum || r32.checksum != rh.checksum ||
      r32.checksum != r64.checksum) {
    std::printf("  !! checksums differ — a shape did not round-trip the payload\n");
  } else {
    std::printf("  (all shapes round-tripped identically: checksum %llu)\n",
                static_cast<unsigned long long>(r32.checksum));
  }

  // The number that retires the performance argument: cost of one whole block's
  // event handling against the wall-clock budget for that block.
  const double blockBudgetUs = 1024.0 / 44100.0 * 1e6; // 1024 frames @ 44.1kHz
  const double n = static_cast<double>(w.offsets.size());
  std::printf("  per-block: A=%.2fus  B'=%.2fus  of a %.0fus budget "
              "(1024 frames @ 44.1kHz) => %.4f%% vs %.4f%%\n",
              r32.nsPerEvent * n / 1000.0, rvf.nsPerEvent * n / 1000.0, blockBudgetUs,
              r32.nsPerEvent * n / 10.0 / blockBudgetUs,
              rvf.nsPerEvent * n / 10.0 / blockBudgetUs);
}

// ---------------------------------------------------------------------------
// Part 3 — the memory arithmetic behind "fixed stride is the hot-path price".
// ---------------------------------------------------------------------------

void part3_footprint() {
  std::printf("\n=== Part 3: what widening the inline payload actually costs ===\n");
  const std::size_t eventPorts[] = {16, 256, 4096};
  const std::size_t eventsPerBlock = 64; // generous for one port in one block
  std::printf("preallocated event storage, %zu events/port/block:\n", eventsPerBlock);
  std::printf("%12s %12s %12s %12s %12s\n", "event ports", "16B", "32B", "48B", "64B");
  for (std::size_t ports : eventPorts) {
    std::printf("%12zu", ports);
    for (std::size_t inl : {std::size_t{16}, std::size_t{32}, std::size_t{48},
                            std::size_t{64}}) {
      const std::size_t per = 4 /*offset*/ + inl; // ignoring padding
      const double kb = static_cast<double>(ports * eventsPerBlock * per) / 1024.0;
      std::printf(" %11.1fK", kb);
    }
    std::printf("\n");
  }
  std::printf("\nfor scale: one stereo audio buffer at 1024 frames = %.1fK\n",
              static_cast<double>(2 * 1024 * sizeof(float)) / 1024.0);
}

// ---------------------------------------------------------------------------
// Part 4 — the thing the timings cannot show: what each shape does to the
// *contract*. Printed so the probe's verdict is not read off ns/event alone.
// ---------------------------------------------------------------------------

void part4_contract() {
  std::printf("\n=== Part 4: contract consequences (not measurable, decisive) ===\n");

  std::printf("\nA: widen kInlineBytes\n");
  std::printf("  - Event::as<T> keeps its static_assert: overflow is a COMPILE error.\n");
  std::printf("  - nodes/ source unchanged; recompile only. Kernel-internal.\n");
  std::printf("  - random access by index survives (Event is fixed-stride).\n");
  std::printf("  - does NOT solve SysEx/text/chord: still needs an out-of-line subtype.\n");

  std::printf("\nB: variable stride\n");
  std::printf("  - sizeof(T) <= capacity becomes a RUNTIME check. The compile-time\n");
  std::printf("    guarantee that a node cannot over-read a payload is gone.\n");
  std::printf("  - operator[](i) and Event* begin()/end() cannot survive: no stride.\n");
  std::printf("    EventBuffer's public interface changes => every event node changes.\n");
  std::printf("  - sorted insert memmoves the arena tail, not fixed-size elements.\n");
  std::printf("  - still does NOT solve unbounded payloads without a lifetime rule:\n");
  std::printf("    CLAP's own sysex event is a POINTER inside a variable-size event.\n");

  std::printf("\nC: inline + out-of-line handle\n");
  std::printf("  - reading a blob needs the BUFFER, not just the Event => the kernel\n");
  std::printf("    gains a payload-arena concept and a lifetime rule.\n");
  std::printf("  - but it is the only shape that answers unbounded payloads at all,\n");
  std::printf("    and every standard does exactly this (VST3 DataEvent, CLAP sysex,\n");
  std::printf("    AU's over-allocated trailing array).\n");
  std::printf("  - A and C compose: A fixes the note, C fixes the blob, independently.\n");
}

int main() {
  part1_fit();

  std::mt19937 rng(20260804);
  const std::size_t n = 32;

  Workload sorted{{}, "nearly-sorted arrival (realistic MIDI)"};
  for (std::size_t i = 0; i < n; ++i)
    sorted.offsets.push_back(static_cast<std::uint32_t>(i * 31));

  Workload shuffled{sorted.offsets, "reverse arrival (adversarial for the arena)"};
  for (std::size_t i = 0; i < n; ++i)
    shuffled.offsets[i] = static_cast<std::uint32_t>((n - 1 - i) * 31);

  part2_shapes(20000, sorted);
  part2_shapes(20000, shuffled);
  part3_footprint();
  part4_contract();
  return 0;
}
