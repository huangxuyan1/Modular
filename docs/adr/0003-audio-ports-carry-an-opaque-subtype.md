# Audio ports carry an opaque subtype, and `sub` stops meaning a count

`PortType` gains a third field. `sub` becomes the **opaque subtype for all three domains**,
and a new `channels` field carries the audio-only count:

```cpp
struct PortType {
  Domain domain{Domain::Audio};
  std::uint32_t sub{0};       // opaque subtype; kernel compares, never interprets
  std::uint32_t channels{0};  // audio only; the one field the kernel reads
};
```

Connection stays exact equality over all three fields. `PortType::audio(N, subtype = 0)`
keeps its existing call shape, so no port declaration under `nodes/` changes.

The question asked was whether the port type needs a channel *layout*. It does not. Layout is
one thing an audio subtype can mean: VST3's `k30Cine` (L R C) and `k30Music` (L R Cs) become
two subtype ids defined in `nodes/`, distinct, refused at connect, with the kernel never
learning what a speaker is.

**The defect was an asymmetry, not a missing feature.** `value<T>` and `event<T>` each carry a
subtype the kernel checks and never reads. Audio's one slot was spent on a count the kernel
*does* read — so audio was the only domain with no channel for "what is this signal", and the
kernel was reusing an allocation quantity as if it were a meaning check. That coincidence
holds only while everything is mono or stereo.

**Why the kernel, and not user space.** `Graph::connect` is the only chokepoint that runs at
connect time. A layout carried on a `value` port is read during `process()`, which can report
a mismatch but never refuse one — so a connect-time guarantee cannot be expressed in user
space without a kernel change, which is this roadmap's definition of a flaw. What enters the
kernel is only the *slot*, the same one value and event already have; that `0x03` means L|R
stays in `nodes/`, and the purity grep stays clean.

**The case is not surround.** Modular is nowhere near 5.1, and a mono/stereo host is fully
conformant, so speaker arrangement alone would have justified deferral. The case that lands
inside the project's stated target workflows: an 80-band mel spectrogram emits `audio<80>`, an
80-channel speaker bus is `audio<80>`, and they connect silently. Analysis pipelines produce
audio-domain buffers that are not speaker channels, and the typed connect check exists to
catch exactly that class of mistake.

## Considered Options

- **Add a `layout` field, keeping `sub` as the audio count** — rejected. This was the research's
  framing. It fixes the surround case, leaves `sub` meaning two different things by domain
  permanently, and leaves the spectral, stem and control-rate cases untyped. A subtype subsumes
  layout; layout does not subsume a subtype.
- **Bit-pack subtype and count into `sub`** — rejected: no struct change, but it caps both at
  16 bits and keeps the kernel decoding one field differently per domain, which is the
  overloading being removed. `PortType` is authoring-time and never enters the block loop, so
  the four saved bytes buy nothing.
- **Subtype 0 as a wildcard** — rejected. Zero friction, but the guarantee would be off by
  default, the kernel would gain a magic value, and it breaks the symmetry with value/event,
  where id 0 is already an ordinary unnamed subtype (`subtype::Trigger` is 1). 0 is instead
  *concrete*, meaning "unspecified audio".
- **Wildcard on inputs only** ("this node accepts any audio subtype") — rejected, and named
  because it will return. The output's subtype must then be derived from whatever got
  connected, which is type inference inside the compile — far larger than the slot being
  added. Polymorphism is instead construction-time configuration, the mechanism already in
  use: `GainNode(channels, subtype = 0)` is the identical shape to the `channels` parameter
  every generic audio node already takes, and matches the settled rule that arity and
  configuration are fixed at construction.
- **Mandatory subtype, no default** — rejected: most rigorous, but it churns every declaration
  under `nodes/` and every test for no conformance gain, turning a purely additive change into
  a breaking one.
- **Nothing; count is the kernel's whole job** — rejected once a connect-time guarantee was
  wanted. Defensible on its own terms — match buffer shapes, leave meaning to authoring
  discipline — but it leaves `connect` type-checking two domains and shape-checking the third,
  and there is no other place the check could ever live.

## Consequences

- **Class: moderate, and the second consecutive ranking overshoot.** Research ranked this
  catastrophic on the grounds that `PortType` *is* the port-domain model. The mechanical change
  is a defaulted struct field plus `Engine.cpp:133` and `Engine.cpp:157` switching `type.sub`
  → `type.channels`; nothing under `nodes/`, nothing in `tests/`. Combined with ADR-0002's
  identical correction, the finding is about the *ranking method*: "touches the port type"
  measures conceptual centrality and reports it as mechanical cost. Blast radius must be
  re-derived per item, never inherited from which struct gets named.
- **Subtype id namespaces are per-domain, for free.** Equality includes `domain`, so
  `audio(80, 2)` never compares equal to `event(2)`. Whether `nodes/` numbers audio subtypes in
  the same sequence as `subtype::Midi` is a convention question for the registry `Payloads.h`
  already defers, not a kernel one.
- **No layout ids are defined.** No node emits surround, and ids without consumers are the
  speculative design the project forbids. `Payloads.h` gains nothing today; the first surround
  or spectral node adds what it needs.
- **`channels` is meaningless for value and event ports** and stays 0 there. Audio ports should
  assert `channels >= 1`: `audio(0)` currently allocates nothing and would read as silence
  rather than fail.
- **Bus arrangement negotiation stays cheap and stays out of the kernel.** VST3's negotiation
  happens at node configuration time, before ports are declared, and renegotiation is a graph
  edit → recompile per the settled model. `connect` remains a pure equality check with no
  negotiation — and now has something faithful to check.

**When this reopens.** If sample rate ever enters the port type (map ticket 04), it lands
beside `channels` as a second kernel-interpreted field rather than competing for `sub` — the
overload removed here is what keeps that additive. And if a node genuinely needs subtype
polymorphism that construction-time configuration cannot express, the wildcard question returns
with the type-inference cost attached.

Ruled by `.scratch/roadmap/issues/12-channel-layout-vs-count.md`, from the oracle checklist in
`.scratch/roadmap/research/01-plugin-host-obligations.md` §4.2 and §10.
