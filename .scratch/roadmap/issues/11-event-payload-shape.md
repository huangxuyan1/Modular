# Probe: can the event payload carry an ordinary note?

Type: prototype
Status: resolved

## Question

Ranked **first, catastrophic-class** by the ticket-01 research (§10), on the grounds that
every event node written under today's assumption raises the cost of the fix. The node
library is small right now. That is the whole argument for attacking it early.

CLAUDE.md files oversized event payloads as a later SysEx concern, with the inline limit
justified as "fixed-stride records in the audio hot path". The oracle says that is wrong on
both counts:

- VST3's plain `NoteOnEvent` is **20 bytes** — the 16-byte inline payload cannot carry an
  ordinary note, never mind SysEx.
- CLAP's event **header alone** is 16 bytes; its note event is ~40.
- CLAP is a modern realtime ABI that deliberately chose **variable-stride** events with a
  `size` field — so fixed stride is not the price of admission to the hot path.

See `.scratch/roadmap/research/01-plugin-host-obligations.md` §5 and §9.

Spike it, don't design it. The cheapest probe that would expose the answer: express a VST3
note-on through today's event model and see what breaks. Then try the candidate shapes
against the same probe:

- widen the inline payload to a fixed size that fits a note (what size, and on whose
  authority?);
- variable-stride records with a `size` field, CLAP-style — what it costs the block loop,
  and whether `EventBuffer`'s v1 push (the documented allocation exception) survives it;
- an out-of-line handle subtype for large payloads only, keeping small events inline.

The falsifiable output is a claim for `docs/roadmap.md`, plus a ruling on whether this is
genuinely catastrophic-class (payload shape is part of the block contract) or lands
moderate because the change is confined to `EventBuffer` and its typed views.

Note `Event::as<T>` is `memcpy`-based with a `is_trivially_copyable_v` static_assert, so the
kernel never interprets payloads — whatever shape wins must keep that property.

## Answer

**Probe:** `.scratch/roadmap/prototypes/11-event-payload/` — `./run.sh`. It includes the real
`kernel/include/modular/Buffers.h`, so every number about today's model is the shipped
number, and it implements each candidate shape far enough to push the same note stream
through all of them.

### 1. Confirmed, and it is a compile error rather than an opinion

`PROTOTYPE_today_fails.cpp` asks the shipped kernel to carry a VST3 `NoteOnEvent`:

```
Buffers.h:48: static assertion failed due to requirement
              'sizeof(Vst3NoteOnBody) <= kInlineBytes': payload too large for inline event
```

Measured bodies: VST3 note-on **20**, CLAP note **24**, a Modular-shaped tagged note **24**,
CLAP `param_value` **40**. Note-*off* is exactly **16** — so today's model can express half a
note pair, which is how the gap stayed invisible while `MidiMsg` (3 bytes) was the only
payload in the library. `sizeof(Event)` is already 20, so "16" was never a stride aligned to
anything.

### 2. Ruling: `kInlineBytes` 16 → **32** (fixed inline, candidate A)

Authority is the oracle, not a round number: the largest *ordinary* (non-pointer) note body
either standard defines is CLAP's 24, plus 8 bytes headroom. Headroom is the explicit lesson
here — the 16-byte bet failed for having none. `sizeof(Event)` becomes 36.

Footprint is not a constraint: 256 event ports × 64 events/block goes 320K → 576K. One
stereo 1024-frame audio buffer is 8K.

### 3. A Modular event does **not** owe a musical position

VST3 puts `ppqPosition` in every event's common header. Modular does not follow it: timeline
position is already settled as user-space, so a node needing musical position reads a
`value` input, and a VST3 boundary node samples that port when filling the plugin's header.
Stapling ppq to the event record would raise the floor to 40 bytes and charge every trigger
and transcription word for a tempo concept the kernel is supposed not to have.

### 4. Variable stride (CLAP-style) — **rejected**, and not on performance

The probe's surprise: performance cannot decide this. Variable stride costs 2.4x per event
(27.4 vs 11.2 ns), which is **0.88µs against a 23,220µs block budget** — 0.0038% versus
0.0015%. Both are noise. CLAUDE.md's stated justification for the 16-byte limit, "fixed-stride
records in the audio hot path", is therefore falsified *as an argument*: fixed stride is not
bought with hot-path performance.

It is rejected on the contract instead:

- `sizeof(T) <= capacity` demotes from `static_assert` to a runtime check: the compile-time
  guarantee that a node cannot over-read a payload is gone.
- `Event` stops being a value type, which bites hardest on the *unbuilt* piece: an event-path
  delay line is a ring buffer of `Event` under fixed records, and a second arena with lifetime
  rules under variable ones.
- Preallocation changes kind: a fixed record bounds an event *count*, a variable one bounds
  *bytes*, so exhaustion means dropping events rather than hitting a capacity sized at
  `prepare()`.
- `operator[](i)` and `const Event* begin()/end()` cannot survive without a stride. **Correction
  to an earlier draft of this answer:** I first wrote that this churns every event node. It
  does not — nothing in `nodes/` indexes an event buffer (`MidiNodes.h:44` and `:92` use
  range-for; only `:153` walks raw pointers), so an iterator-shaped cursor preserves most sites.
  The interface argument is therefore the weakest of these, not the strongest.
- Structural, and only found by testing both arrival orders: a variable-stride arena **cannot
  be traversed backward** (no stride, no footer), so the O(1) append that a fixed-stride
  vector gets free for already-sorted input needs a cached last-offset. Without it, sorted
  arrival costs 4x rather than 2.4x.
- It does not even solve unbounded payloads: CLAP's own SysEx event is a *pointer* inside a
  variable-size event.

### 5. Out-of-line handle — deferred, because it composes

Candidate C is candidate A plus a side arena; nothing about widening forecloses it. No node
in the library needs an unbounded payload today, so the kernel does not yet buy a
payload-arena concept, a lifetime rule, and a `resolve()` on `EventBuffer` (reading a blob
needs the *buffer*, not just the `Event` — the one real asymmetry C introduces).

### 6. Class ruling: **moderate**, not catastrophic — and the urgency argument dissolves

The ticket, following research §10, ranked this first and catastrophic *because* every event
node written under the 16-byte assumption raised the cost of the fix. That premise was
conditional on the fix being variable-stride. It isn't: `Event::make`/`Event::as` are `memcpy`
plus `static_assert`, so widening changes no source under `nodes/` — it is a recompile and a
footprint change, confined to the kernel. **The size of the node library never mattered**, so
this did not have to be attacked early; it was still worth attacking early, because that is
what showed the catastrophic path is unnecessary.

Ranking correction for the map: catastrophic → moderate. `Event::as<T>` stays `memcpy`-based
with the `is_trivially_copyable_v` static_assert, so the kernel still never interprets
payloads.

### The probe

Kept in `.scratch/roadmap/prototypes/11-event-payload/` rather than on a throwaway branch:
`.scratch/` is tracked in this repo, so the evidence this answer cites lives in history with
the answer. `./run.sh` still runs.

## Comments

**2026-08-05 — landed.** `kInlineBytes` is 32 in `Buffers.h`; `EventBuffer::operator[]` removed
(its only consumer was three assertions in `test_kernel.cpp`, rewritten to read forward), so the
surface is cursor-compatible and a future variable-stride move stays recompile-scale. `begin()`
and `end()` still return `const Event*` — compatible, not yet cursor-shaped. New test: "the event payload carries a
conformant plugin note" — VST3 `NoteOnEvent` and CLAP `clap_event_note` mirrored field-for-field
with `static_assert`s on their sizes, so the test fails if either standard's shape is
misremembered. All 3 ctest suites green, and the moderate-class falsifier held: `git diff --stat`
shows zero lines under `nodes/`, `node_tests` passed untouched.

Discussion after the ruling produced one finding worth more than the ruling: switching paradigm
later costs **one class rewrite plus three call sites** (`Engine.cpp` needs zero changes — it
holds `EventBuffer` opaquely and never touches an event). Cheap migration is what makes
deferring correct rather than merely convenient. The window closes when **PDC lands on event
paths**, not when the node library grows — an event delay line is what would make the migration
hard. Recorded in ADR-0002 and CLAUDE.md.

Part 0 of the probe was re-pointed at a 40-byte note+ppq payload, since the original 20-byte
note-on now compiles.
