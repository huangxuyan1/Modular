# Is `audio<N>` enough, or does the port type need a channel *layout*?

Type: grilling
Status: resolved

## Question

Ranked **second, catastrophic-class** by the ticket-01 research (§10) — it touches the port
type, and the port type is the settled three-domain model.

The pre-coding decision of 2026-07-22 made channel count part of the port type: `audio<N>`,
connection requires matching N, stereo is one wire. The oracle says every standard carries
strictly more than a count:

- VST3 negotiates `SpeakerArrangement`, a bitmask of speaker **positions**. `k30Cine`
  (L R C) and `k30Music` (L R Cs) are both 3 channels and are *different*.
- CLAP carries `channel_count` **plus** a separate `port_type`.
- AU carries counts **plus** a separate layout property.

See `.scratch/roadmap/research/01-plugin-host-obligations.md` §4 and §9.

Decide:

- Does the port type gain a layout, or does layout become a `value` port / node
  configuration that the kernel never sees? The latter keeps the kernel from naming speaker
  positions — which is a media concept, and therefore a purity question, not only a design
  one.
- If connection compatibility stays count-only, what happens when L-R-C meets L-R-Cs? A
  silently wrong render is the failure mode, and it is exactly the class of error the typed
  connect check exists to prevent.
- Is this catastrophic-class in practice? Changing `audio<N>` churns every node's port
  declarations. Establish whether an additive answer exists before assuming it does not.

Ordering note: the research ranks this behind ticket 11 (event payload), on the same
"attack it before the library grows" logic — but the audio node population is smaller than
the event node population, so the urgency is genuinely lower.

## Answer

**No — the port type does not need a channel layout. It needs an opaque *subtype*, and
layout is one thing a subtype can mean.** Ruled as ADR-0003.

`PortType` gains a third field: `sub` becomes the opaque subtype for *all three* domains,
and a new `channels` field carries the audio-only count.

```cpp
struct PortType {
  Domain domain{Domain::Audio};
  std::uint32_t sub{0};       // opaque subtype; kernel compares, never interprets
  std::uint32_t channels{0};  // audio only; the one field the kernel reads
};
```

**The defect was an asymmetry, not a missing feature.** `value<T>` and `event<T>` each carry
a subtype the kernel checks and never reads. Audio's one slot was spent on a count the kernel
*does* read — so audio was the only domain with no channel for "what is this signal", and the
kernel was reusing an allocation quantity as if it were a meaning check. That coincidence
holds only while everything is mono or stereo.

**The case is not surround.** Modular is nowhere near 5.1 and a mono/stereo host is fully
conformant, so speaker arrangement alone would have justified deferral — which is why the
ticket's own framing was too narrow. The case that lands inside this project's stated target
workflows: an 80-band mel spectrogram emits `audio<80>`, an 80-channel speaker bus is
`audio<80>`, and they connect silently. Analysis pipelines produce audio-domain buffers that
are not speaker channels.

**On "is it a kernel matter?"** — three jobs, and they separate cleanly. Sizing buffers from
the count is already kernel (`Engine.cpp:157`). Checking compatibility at connect is already
kernel, for all three domains. Knowing that `0x03` means L|R never becomes kernel. Only a
*slot* is added — the same slot value/event already have. And the guarantee cannot live
anywhere else: `Graph::connect` is the sole chokepoint running at connect time, so a layout
on a `value` port could report a mismatch during `process()` but never refuse one. By this
map's rule — *a flaw is an application that can't be expressed in user space without a kernel
change* — this qualifies.

**Subtype 0 is concrete ("unspecified audio"), not a wildcard.** Ids already start at 1
(`subtype::Trigger`), so 0 is an ordinary unnamed subtype in the existing model. Generic nodes
take a defaulted `subtype = 0` constructor parameter — the identical shape to the `channels`
parameter `GainNode`, `MixNode` and `CaptureSinkNode` already take — so nothing under `nodes/`
or `tests/` changes. Wildcard-on-inputs was rejected explicitly: deriving an output's subtype
from whatever got connected is type inference inside the compile.

**Class corrected catastrophic → moderate**, the second consecutive overshoot after ticket 11.
The mechanical change is a defaulted struct field plus two lines in `Engine.cpp`
(`:133` and `:157`, `type.sub` → `type.channels`). The finding is about the ranking method:
"touches the port type" measures conceptual centrality and reports it as mechanical cost.

**No layout ids defined yet** — no node emits surround, and ids without consumers are the
speculative design CLAUDE.md forbids.

Ruling only, per wayfinder's plan-don't-do default. Implementation is a claim on
`docs/roadmap.md`, not part of this ticket.
