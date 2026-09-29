# The event payload is fixed inline, widened to 32 bytes

An event's payload stays a **fixed-size inline byte array**, widened from 16 to **32 bytes**.
`Event` remains `{uint32 offset, array<byte, 32> payload}`, and typed access remains
`memcpy`-based `Event::make<T>` / `Event::as<T>` guarded by `static_assert(sizeof(T) <=
kInlineBytes)`, so payload overflow stays a *compile* error and the kernel still never
interprets payload bytes.

**32 on whose authority:** the requirements oracle, not a round number. The largest
*ordinary* — non-pointer — note body either standard defines is CLAP's `clap_event_note` at
24 bytes (VST3's `NoteOnEvent` is 20), plus 8 bytes of headroom. Headroom is the lesson of
the decision this replaces: 16 was chosen with none, and it failed by 4 bytes.

The 16-byte limit was documented as a SysEx edge case justified by "fixed-stride records in
the audio hot path". Both halves were wrong. It was not an edge case — 16 bytes cannot carry
a plain note-on, only a note-*off* (exactly 16), which is why the gap stayed invisible while
the 3-byte `MidiMsg` was the library's only payload. And the hot-path justification does not
survive measurement (see Consequences).

A Modular event does **not** carry a musical position, though VST3 puts `ppqPosition` in
every event's common header. Timeline position is already settled as user-space, so a node
that needs musical position reads a `value` input and a plugin boundary node samples that
port when filling the host's event header. Carrying it per event would raise the floor to 40
bytes and charge every trigger and transcription word for a tempo concept the kernel is
meant not to have.

## Considered Options

- **Variable-stride records with a `size` field, CLAP-style** — rejected, and *not* on
  performance. `sizeof(T) <= capacity` demotes from a `static_assert` to a runtime check,
  losing the compile-time guarantee that a node cannot over-read a payload. `Event` stops
  being a value type, which matters most for the *unbuilt* piece: inserting delay on an event
  path is a ring buffer of `Event` under fixed records and a second arena with lifetime rules
  under variable ones. Preallocation also changes kind — a fixed record bounds a *count*, a
  variable one bounds *bytes*, so exhaustion means dropping events rather than hitting a
  capacity you sized at `prepare()`. And a variable-stride arena cannot be traversed
  *backward* — no stride, no footer — so it cannot have the O(1) append that a fixed-stride
  vector gets free for already-sorted input without caching the last offset.

  **The interface argument is weaker than it first looks, and the correction matters for
  whoever revisits this.** `operator[](i)` and `const Event* begin()/end()` cannot survive
  without a stride, but nothing in `nodes/` indexes an event buffer — the midi nodes read
  events with range-for, except the sine synth, which walks raw `const Event*`. An
  iterator-shaped cursor would preserve the range-for sites, so the churn is that one node
  plus an adapter, not the library. Forward-only iteration is therefore kept *deliberately*:
  `operator[]` has been **removed** from `EventBuffer`, its only consumer being three
  assertions in `test_kernel.cpp`, rewritten to read forward. That keeps the public surface
  cursor-compatible, so every node written from here on is written against an interface that
  survives the switch. Note `begin()/end()` still return `const Event*` — the surface is
  cursor-*compatible*, not yet cursor-*shaped*.

  Note this shape does not even solve unbounded payloads: CLAP's own SysEx event is a
  *pointer* inside a variable-size event — it needed both mechanisms. Variable stride buys
  heterogeneous small records in one list, and Modular does not have that problem, because a
  port has one subtype and a buffer is therefore homogeneous. The variable-size design
  Modular would actually need is *per-port* variable stride over records sharing a subtype,
  which is a different and simpler thing than CLAP's. Adopting CLAP's shape now would mean
  copying a mechanism whose motivating constraint this model already removed.
- **Out-of-line handle for every oversized payload, keeping the inline size at 16** —
  deferred rather than rejected: it is *additive* to this decision, so nothing here forecloses
  it. Deferred because no node needs an unbounded payload yet, and adopting it early would
  buy the kernel a payload-arena concept, a buffer lifetime rule, and an `EventBuffer::resolve()`
  — reading a blob would need the *buffer*, not just the `Event`, which is the one real
  asymmetry it introduces. It returns when a real SysEx, chord/scale-text or long-text event
  node exists.
- **24 bytes — the exact fit** — rejected: it is the smallest size holding a conformant note,
  which is precisely how 16 was chosen.
- **56 bytes, for a 64-byte cache-line record** — rejected: the authority would be the cache
  line rather than the oracle, and the footprint measurement shows nothing is being bought by
  guessing larger. Widening again later is a recompile.

## Consequences

- **Class: moderate, not catastrophic.** Research ranked payload shape catastrophic and first,
  on the grounds that every event node written under the 16-byte assumption raised the cost of
  the fix. That premise held only for the variable-stride fix. Widening changes no source
  under `nodes/`, so the size of the node library never mattered. The ranking correction is
  itself the finding: the early attack was worth making because it retired the catastrophic
  path, not because delay was expensive.
- **The hot-path argument is retired.** Measured at 32 events/block, fixed stride costs
  0.36µs per block and variable stride 0.88µs, against a 23,220µs budget for 1024 frames at
  44.1kHz — 0.0015% versus 0.0038%. Payload shape is not a performance decision, and future
  arguments about it should not be framed as one. CLAP being a modern realtime ABI that chose
  variable stride is consistent with this: the choice was not forced on it by the hot path
  either.
- `sizeof(Event)` goes 20 → 36 bytes (a `uint32` offset plus 32 payload bytes, align 4).
  Footprint stays irrelevant: 256 event ports × 64 events/block is 320K → 576K, against 8K
  for a single stereo 1024-frame audio buffer.
- Node payload structs may now hold a `double` (CLAP's note velocity is one), so payloads
  have alignment requirements the 16-byte array did not meet. `memcpy`-based access makes
  this a non-issue for correctness — which is a second reason the `memcpy` view, not
  `reinterpret_cast`, is load-bearing rather than stylistic.
- An event port carrying notes needs a **variant tag inside the payload**: one port carries
  note-on, note-off and poly-pressure, so the discriminator cannot live on the port as the
  subtype id does. That tag is the only header Modular owes; CLAP's 16-byte header is
  otherwise absorbed — `time` is `Event::offset`, `space_id`/`type` are the port's subtype,
  and `size` exists only to serve variable stride.
- Payloads above 32 bytes remain unrepresentable, now including CLAP's `param_value` (40).
  That is not a gap: a parameter change is Modular's *value* domain, not its event domain.

**When this reopens.** Two triggers, and neither is the size of the node library — that
argument was wrong twice over. First, `event<word>` from transcription: short UTF-8 plus
confidence plus speaker, too small to justify an arena round-trip and too variable to sit well
in a fixed slot. It is a better argument for variable size than SysEx ever was, and if it
lands, the shape to reach for is *per-port* variable stride over records sharing one subtype,
not CLAP's per-record `size`. Second, and this is what closes the window: **PDC on event
paths**. Migration today is one class rewrite plus three call sites (`Engine.cpp` needs none —
it holds `EventBuffer` opaquely and never touches an event); once an event delay line exists,
it acquires a genuinely hard part. The window is open until PDC, not forever.

Probed in `.scratch/roadmap/prototypes/11-event-payload/`; ruled by
`.scratch/roadmap/issues/11-event-payload-shape.md`, from the oracle checklist in
`.scratch/roadmap/research/01-plugin-host-obligations.md` §5.3.
