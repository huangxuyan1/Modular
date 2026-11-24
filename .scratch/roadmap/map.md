# Map: Roadmap to a DAW that projects

Labels: wayfinder:map

## Destination

`docs/roadmap.md` — an ordered list of falsifiable claims running to this terminal claim:

> A recognizable DAW runs on the engine while (a) the kernel-purity grep still returns
> nothing, and (b) the DAW stores nothing that isn't derivable from the authored graph.
>
> Falsified by: any DAW feature that forced a media concept into the kernel, or that
> needed out-of-band storage.

Half of that is already under continuous test — `scripts/check_kernel_purity.sh` runs as a
ctest test today. The other half is the "graph is the single source of truth" bet made
checkable: reload a project from the graph alone and the DAW's display must be identical.

## Notes

**The unit is a claim, not a sprint.** Sprints were inherited from team practice — a
cadence for feedback that doesn't exist here (5 days, then 2, no time box, no stakeholder
demo). The repo already agreed: the tracker directory is `.scratch/supernode/`, named for
the claim, never `.scratch/sprint-2/`. One claim, phrased so it could be false. Sized by
cutting, never by extending — a big area becomes several claims, not one long push.

Claim anatomy, as observed in the two closed claims:

1. spec with an explicit cut list (`.scratch/<slug>/spec.md`)
2. 2–3 tickets under `.scratch/<slug>/issues/`
3. an ADR with the rejected alternatives
4. a demo plus tests that fail if the claim is false
5. a **close-out review** — this is what found the fixed-size-values design bug; it
   attaches to a claim's close and must not vanish with the sprint boundary
6. CLAUDE.md updated
7. commit `Close: <the claim>`

**Method:**

- An entry is a claim with a demo that can fail. A feature that proves nothing can't appear.
- **Applications are adversaries, not requirement sources.** You don't ask a DAW what the
  kernel should have; you build one to find out what the kernel is missing. A flaw *is* an
  application that can't be expressed in user space without a kernel change.
- **Spikes, not designs.** Attack an unknown with the cheapest probe that would expose it
  — *try* to express a 4x-oversampled effect in today's model. It either survives or hands
  you a concrete, non-speculative requirement. Never speculatively design for a
  hypothetical.
- **Ordering discipline: proof dependency first, then blast radius descending.** This is
  method, not part of the terminal claim — "every expensive unknown was attacked" has no
  demo that fails, so it is not a claim.

| class | example | cost |
|---|---|---|
| cheap | a new node in `nodes/` | zero kernel impact |
| moderate | compile-time delay insertion | kernel-internal, no interface change |
| expensive | `latencySamples()` on every node | whole library churns |
| catastrophic | port-domain model or block contract changes | everything churns |

- **Plugin standards are the requirements oracle** (VST3 primary; AU/CLAP where they
  diverge). They are a decades-old distilled enumeration of what audio tools need, so
  conformance replaces speculation about "common requirements". The engine bends to
  plugins, not the reverse.
- **Committed near, ordered far.** The head claim is fully specified (claim + demo + cut
  list); the tail is coarse and ordered, re-cut whenever a claim closes. Tail claims are
  deliberately *not sized* — you can't write a cut list for something you don't understand
  yet.
- **Realtime:** cheap probe early, full device IO and JUCE late. Offline renders are
  bit-comparable and realtime output is not, so offline stays the primary verification for
  everything structural.
- `docs/roadmap.md` is a **running artifact**: resolving a ticket here appends or sharpens
  its claims there. There is no final write-up ticket.

**Skills every session should consult:** `/grilling` and `/domain-modeling` (default),
`/prototype` for spike tickets, `/research` for research tickets. Vocabulary belongs in
`CONTEXT.md`, decisions with rejected alternatives in `docs/adr/`.

**Standing preference:** read CLAUDE.md's "Design discipline" before proposing anything.
Concede cleanly when a generalization adds a concept without adding information.

## Decisions so far

<!-- one line per closed ticket: enough to judge relevance, then open the ticket for detail -->

- [Extract the host-side obligations of VST3 (and AU/CLAP where they diverge)](issues/01-plugin-host-obligations.md)
  — checklist delivered in `research/01-plugin-host-obligations.md` (1015 lines, spec-cited).
  Six contradictions with CLAUDE.md, the top two catastrophic: the 16-byte event payload is
  too small for an *ordinary note* (VST3 `NoteOnEvent` is 20 bytes), and `audio<N>` carries
  strictly less information than every standard's channel *layout*. Dropout-free recompile
  turns out stricter than any spec requires — stop-recompile-resume is conformant.
  Block-latched parameters are legal but degraded. `valueAt(offset)` vindicated.
- [Name the layer: "engine" the product vs `Engine` the class](issues/02-name-the-engine-layer.md)
  — Modular is a *language* + compiler + runtime; the product sense of "engine" retires and
  `Engine` stays the class. A fourth target, **`modular_workspace`** (provisional), owns
  stable ids, atomic edits, undo, constraints and the Engine lifecycle — mechanism only, the
  app calls recompile as needed. Only mutations reify as commands. Purity extends to it:
  media words there would foreclose non-music tools like batch captioning. Templates are
  plain functions living in `apps/daw/`; the DAW stays in this repo as an adversary.

- [Probe: can the event payload carry an ordinary note?](issues/11-event-payload-shape.md)
  — no: 16 bytes holds a note-*off* (exactly 16) but not a note-*on* (20), proven as a
  `static_assert` failure in the shipped kernel. Ruled `kInlineBytes` 16 → **32** (ADR-0002),
  authority being the largest ordinary note body in the oracle plus headroom. Variable stride
  rejected — *not* on performance, which the probe showed cannot decide this (0.0038% of a
  block budget), but because it costs `EventBuffer`'s public interface and demotes payload
  overflow from a compile error to a runtime check. Out-of-line handles deferred: they are
  additive. **Ranking corrected catastrophic → moderate**, and the "attack it before the node
  library grows" urgency dissolved with it — widening changes no source under `nodes/`.
  Events carry no musical position; ppq is a `value` input.

- [Is `audio<N>` enough, or does the port type need a channel *layout*?](issues/12-channel-layout-vs-count.md)
  — no layout: the port type needs an opaque **subtype**, and layout is merely one thing a
  subtype can mean (ADR-0003). The defect was an *asymmetry* — audio was the only domain whose
  `sub` was spent, on a count the kernel interprets, leaving it no slot for what a signal is.
  So `PortType` becomes `{domain, sub, channels}` with `sub` uniform across all three domains.
  The motivating case is not surround, which alone would have justified deferral, but the
  target workflows: an 80-band spectrogram and an 80-channel speaker bus type identically
  today. Kernel-matter test passes narrowly — only a *slot* is added, and `connect` is the one
  chokepoint where a connect-time guarantee could ever live. Subtype 0 is concrete
  ("unspecified audio"), not a wildcard; wildcard-on-inputs rejected as type inference in the
  compile. **Ranking corrected catastrophic → moderate — the second consecutive overshoot**,
  and the method finding is that "touches the port type" measures conceptual centrality and
  reports it as mechanical cost.

- [Can a node's ports change after it joins a graph?](issues/03-variable-arity-after-graph-add.md)
  — yes, added *and deleted* by command, and deleting one that still carries a wire throws
  (**RESTRICT**, the user's framing; CASCADE would have the kernel destroy wiring nobody asked
  it to touch). One rule for ports, nodes and boundary nodes, so **node deletion lands with it**
  and ADR-0001's known fragility retires. The ruling reached past the question because "a port
  can be deleted" is unanswerable while a port *is* its position: identity moves to **ids,
  graph-scoped**, minted by one `IdSource` per graph for nodes and ports alike, and used all the
  way into `process()`. `NodeId` stops being an index; construction moves into the graph
  (`graph.addNode<GainNode>(1)`) because an id must be right when minted — a node keeps its port
  ids in private members nothing outside can rewrite. That impossibility killed process-global
  ids; node-scoped ids died on the supernode face, where two boundary nodes both claim id 0.
  Port-per-parameter was tested first and killed by Serum, whose effect sections switch in and
  out: **params are not ports**. The flat "supernode as replayed recipe" model dissolves the
  whole difficulty but costs encapsulation — resolved by noting ticket 02 already named it:
  Supernode is the container, **Template** the recipe. **Class: expensive, and the first of the
  four that earns the label** — node constructor signatures change. ADR-0004.

- [Probe: can a 4x-oversampled effect be expressed in today's model?](issues/04-rate-probe-oversampling.md)
  — yes, with no kernel change, so **no claim needed**: one global rate survives and rate leaves
  the catastrophic class. Only rate as buffer length fails, because every port's view is the
  block's `numFrames` and an upsampler has nowhere to put 4N frames. Two other forms render equal
  to a plain 4x reference at every block size, cutting aliasing from −20.6 to −111.8 dB: the 4x
  signal carried as `audio<4>` polyphase channels between an upsampling and a downsampling node,
  and a node that owns a 4x `Engine`. Polyphase needs phase-aware stateful nodes (a stock
  per-channel one is silently wrong, yet block-size invariant) and types identically to a
  4-channel bus, which ADR-0003's subtype already covers. The nested Engine runs stock nodes at the
  right rate, but its inner graph is invisible to compile, Save/Open and the workspace. **The
  third catastrophic ranking in a row that its probe did not bear out.**

## Not yet specified

In scope, not yet sharp enough to ticket. Graduates as the frontier advances.

- **PDC and cycles, implementation shape.** Settled in principle (engine owns
  compensation; cycle-breaking and PDC are one mechanism), unimplemented. It was waiting on the
  rate and arity rulings, since both could change what the compiler inserts delay into; both are
  now in. *Sharpened by ticket 04:* rate adds no new kind of delay, since every node still runs
  at the base rate. But `latencySamples()` counts whole base frames, so an oversampled section
  must keep its total latency whole and absorb any fraction inside itself.
- **Threading / parallel execution.** Engine v1 is single-threaded. Parallelism changes the
  compile (scheduling) and possibly node contracts (thread affinity). Expensive-class.
- **Dynamic polyphony.** Spawning subgraph copies at runtime fights both "ports frozen at
  compile" and "no allocation in the block loop". Today's `VoiceAllocatorNode` presumably
  handles a fixed voice count; true dynamic instantiation is unexamined.
- **The per-sample subgraph domain.** Deliberately reserved as the escape hatch for
  sub-block feedback (à la Reaktor / `gen~`). It is a *second execution model* in the
  kernel, so catastrophic-class if it turns out to be needed early.
- **Persistence and the subtype-id policy.** Serialization forces the deferred
  `value<T>`/`event<T>` parameterized-vs-dynamic ruling, since type ids must survive a
  round trip. Also gates stable node ids, which the command API needs. *Promoted by
  ticket 02:* if Modular is a language, the authored graph is its program text, so this
  stops being a storage question and becomes the type-system question. *Widened by ticket 12:*
  audio now has subtype ids too, so all three domains draw on the id space that `Payloads.h`
  defers to "a real registry is a later problem" — the ids are per-domain namespaces for free,
  since equality includes the domain, but a registry that must survive serialization has three
  domains to number, not two. *Widened again by ticket 03:* node and port ids are now minted at
  authoring time, so a save file must round-trip every id **and** each graph's `IdSource`
  high-water mark — reusing an id after a reload would resurrect a wire onto the wrong port.
  Stable node ids are no longer waiting on this question; ticket 03 delivered them.
- **Variable-length event payloads.** Two distinct cases, and ADR-0002 only settled one.
  *Unbounded* (SysEx, chord/scale text) is ruled — an out-of-line handle subtype, additive to
  the fixed inline payload — so what is unspecified is its trigger and lifetime rule: it costs
  the kernel a payload arena plus an `EventBuffer::resolve()`, since reading a blob needs the
  buffer rather than just the `Event`. Deliberately unticketed, because nothing needs it yet
  and a ticket would put it on the frontier.

  *Short-but-variable* is the open one, and `event<word>` from transcription is the case:
  UTF-8 plus confidence plus speaker, too small to justify an arena round-trip and too
  variable to sit well in a 32-byte slot. Not sharp enough to ticket until a real transcription
  node exists, but it is the payload most likely to reopen the fixed-size ruling — and if it
  does, the shape to reach for is *per-port* variable stride over records sharing one subtype,
  **not** CLAP's per-record `size`, whose motivating heterogeneity the port model already
  removed. The hedge that keeps that a recompile-scale change: nothing in `nodes/` indexes an
  event buffer today, so keep event iteration forward-only.
- **Plugin-host node design.** Opaque state blobs, bus arrangements, bypass, tail. Ticket 01's
  checklist now exists, so this is sharpenable: the ppq a boundary node must put in every VST3
  event header, which ticket 11 ruled arrives as a `value` input rather than on the event; and
  opaque state that is *storable in* the graph yet not *derivable from* it — which forces the
  terminal claim to split authored from derived data. **Tail** is ranked expensive and
  cheap-now/expensive-later: the exact twin of `latencySamples()`, and it earns its place
  independent of plugins because offline render is the primary verification path. *Narrowed by
  ticket 03:* parameter mapping is settled — params are not ports, the node owns the mapping,
  ports are created and deleted by command. The unconnected-value-reads-zero defect graduated
  out of here into ticket 13.
- **The DAW view layer.** Piano roll, arrangement, mixer as projections of one graph.
  Blocked behind the constraint/annotation ruling.
- **Node library growth** — what "most common requirements" means concretely once the
  checklist exists. Cheap-class throughout, so it never competes for ordering. *Sharpened by
  ticket 11:* a note payload needs a **variant tag in the payload** (one port carries
  note-on, note-off and poly-pressure, so the discriminator cannot live on the port the way
  the subtype id does), and today's 3-byte `MidiMsg` is not that shape — whether raw MIDI
  survives alongside a structured note payload is open. *Sharpened by ticket 12:* audio subtype
  ids now exist as a mechanism with **none defined**, deliberately — the first surround node
  defines its layout ids and the first spectral node defines its own, each when it exists.
  Watch for the failure mode where every generic node acquires a subtype parameter nobody ever
  passes, which would leave the guarantee permanently off in practice. *Sharpened by ticket 04:*
  oversampling is node-library work. Its polyphase stream is one more audio subtype for the first
  oversampling node to define, and a stateful node on that stream must step through the phases
  in time order, which a stock per-channel node silently does not.
- **Rate beyond oversampling.** Ticket 04 found that oversampling needs no claim, and left three
  pressures unprobed. The first is an *authorable* at-rate subgraph: the nested-`Engine` form
  hides its inner graph from compile, Save/Open and the workspace, which matters only if a user
  must edit what runs at 4x. The second is sub-rate analysis streams, which events carry
  block-size invariantly and v1's block-latched values do not. The third is varispeed of a live
  stream, whose signal-dependent latency the kernel contract rules out. None is sharp enough to
  ticket until a node or a workflow needs it.

## Out of scope

Ruled beyond this destination. Returns only if the destination is redrawn, and then as a
fresh effort.

- **Multi-user collaboration** (CRDT/OT over the graph). Flagged as the single biggest
  redraw risk on the list: concurrent editing is a fundamentally different source-of-truth
  model, and retrofitting it would revisit the deepest settled decision in the project.
- **Binary third-party node ABI.** Freezing the kernel's C++ interface into a compatibility
  promise is precisely what you must not do while still discovering kernel-level
  requirements — and this session found one such requirement. Note the split: contributing
  nodes as **source** in `nodes/`, recompiled with the project, needs nothing and is
  available today. Only the loadable/binary form is out. Would be pulled back in "if room
  is available", which in practice means after the kernel stops changing.
- **Video / picture sync** (video track, timecode, frame-accurate post). Mostly a new
  payload type plus a clock relationship rather than a stress on the graph model, so closer
  to cheap-class work than to a load-bearing claim.
- **Mobile platforms and GPU DSP.** Large platform efforts; GPU DSP in particular would
  revisit the block-processing contract and threading model, so it belongs with the
  catastrophic-class unknowns rather than as a late addition.
- **Engine as a plugin guest** — in scope but explicitly last, and judged easy: a boundary
  node marshalling the host's buffers, with device-ness already living in the node and
  block size already unstable. Consequence: since it's last, it cannot be what retires the
  rate question, so rate is attacked independently (ticket 04).
