# Modular foundations

This document holds project decisions and implementation conventions that do not need to
load at agent startup. Read the relevant sections before proposing changes to the kernel,
workspace, nodes or apps. The domain glossary is `CONTEXT.md`; decisions with their own
trade-off records are under `docs/adr/`. The agreed Windows GUI scope is in
`.scratch/graph-editor/spec.md`, not here.

## Purpose and layers

The experiment is whether a DAW can *emerge* from a minimal graph language instead of
being hardcoded into its kernel. A traditional DAW is one possible client; speech,
transcription, podcast and offline-render workflows are also targets. Test each proposal:
does the capability arise from the graph model, or is it inherited from DAW tradition?

- `kernel/` (`modular_kernel`): graph, typed ports, connections, compile and block loop.
  Framework-free and media-agnostic. `Supernode.h` holds composition; private
  `kernel/src/Flatten.h` and `.cpp` erase it at compile.
- `nodes/`: user-space primitive nodes, outside the kernel. `Payloads.h` declares media
  payloads. `BasicNodes.h` stays medium-neutral; `MidiNodes.h` knows about notes.
  Compiled adapters include MIDI-file parsing and Windows audio output.
- `workspace/`: GUI-independent authored Graph ownership, NodeId layout and reusable
  explicit deletion. Policy and media-specific content remain with clients.
- `apps/render_cli/`, `apps/midi_cli/`, `apps/graph_editor/`: offline render, MIDI-file
  render/playback and the Windows editor respectively.
- `tests/`: doctest suites for kernel, nodes, authoring, MIDI files and the real editor
  app. `scripts/check_kernel_purity.cmake` enforces media-free kernel code.

Everything commonly called a DAW feature—tracks, mixers, buses, clips, transport,
playhead and arrangements—is user-space node wiring or a view of the authored graph,
not a kernel concept. Views do not become separate sources of truth. JUCE, if used,
is an implementation dependency, not part of the graph/compile/runtime/serialization
contracts. The provisional Windows output backend uses the Windows SDK; its choice was
not settled by the feature spec.

## Kernel contract

- Three port domains are distinguished by *when* they have a value: `audio` is dense
  per sample; `value` is held and readable at any time, persists across blocks and
  needs a per-type default; `event` exists only at instants. MIDI and triggers are
  event subtypes, not domains. A hardware boundary node marshals device data;
  device-ness is not part of a port type.
- Every event is `(intra-block sample offset, payload)`. The stream clock is a
  monotonic, engine-owned frame counter and never jumps. Timeline position, seeking,
  looping and the draggable playhead are user-space projections, never kernel time.
  Hardware callback buffer sizes vary; neither block number nor per-sample graph
  evaluation is the time model. A compiled per-sample supernode domain remains a
  possible future escape hatch for sub-block feedback.
- The kernel owns latency/delay compensation: nodes report latency, and cycle breaking
  and compensation are the same mechanism. Compilation is necessary because the
  executed graph differs from the authored graph. Mid-playback, dropout-free
  recompilation remains open. Latency is structural rather than signal-dependent;
  runtime switching chooses between statically aligned paths.
- A graph runs at one global sample rate: every node is prepared with it and processes
  the same block. Oversampling happens inside nodes, as polyphase channels between an
  upsampling and a downsampling node or in a node that owns a faster `Engine`; it is
  never a per-port rate. A stateful node in a polyphase section steps through its
  phases in time order, and an oversampled section's total latency must be a whole
  number of base-rate frames. See map ticket 04
  (`.scratch/roadmap/issues/04-rate-probe-oversampling.md`).
- Port types have `{domain, sub, channels}`. `sub` is opaque to the kernel in every
  domain; only audio's `channels` is interpreted there. Speaker layout belongs in a
  nodes-layer subtype. Audio subtype 0 is a concrete unspecified subtype, not a
  wildcard. See ADR-0003.
- A value port has a `valueAt(offset)`-style accessor; today's value is block-latched,
  but the contract allows finer resolution later. Values are size-free, with an empty
  slot reading as zero. Events use a fixed 32-byte inline payload, copyable as a
  value; overflow remains a `static_assert`. An event carries no musical position:
  a node needing ppq reads a value input. `EventBuffer` iteration is forward-only.
  Event-path PDC or unbounded words may require revisiting the stride; out-of-line
  handles are deferred. See ADR-0002.
- Ports may be added or removed while authoring, including on wired-in nodes, but
  a compiled form is an immutable snapshot. Ports and nodes have graph-minted ids;
  identity is never a display position. Graph construction mints ids before a node
  caches its own private port ids (for example, `graph.addNode<GainNode>(1)`).
  `IdSource` lives per graph, separately allocated so moving a Graph cannot dangle
  a node's source; its high-water mark survives persistence so ids cannot be
  resurrected after reload. A supernode's exposed port id is its boundary node's
  port id. Params inside a plugin are not necessarily ports. Deleting a port, node
  or boundary node that carries a wire in its own graph throws (RESTRICT);
  an explicit shared operation may unwire then delete, while media-specific cascades
  remain client policy. RESTRICT cannot see a boundary node's outer wires: removing
  them with the inner edit is the workspace's job (not yet built), and a wire left
  on a vanished outer port fails at compile. See ADR-0004 and
  `docs/graph-persistence.md`.
- One connection per input, unlimited fan-out. A node implements fan-in with variable
  arity; the kernel must not decide how to sum audio, order events or arbitrate
  values from multiple incoming wires. A visual multi-input port is a
  presentation-level group over individual ports. The runtime never changes arity
  within an executed graph.
- An externally writable value source is needed for UI/configuration input, without
  adding transport concepts to the kernel. Subtype registration/checking policy,
  push vs pull evaluation, a graph-owned annotation layer and per-sink latency
  compensation policy remain open.

A MIDI-clip supernode demonstrates the distinction: an event processor consumes a
transport-start event and clip-position value, then drives a MIDI block node. Delay,
seek and tempo-follow can follow from ordinary wiring; no kernel transport is needed.

## Supernodes and history

A supernode owns its embedded graph *by value*: placing it snapshots it. Its boundary
nodes determine its outer face on demand. During compilation the engine recursively
resolves connections through the boundaries and enumerates primitives; it does not
materialize a second flat Graph. Supernodes and boundaries do not execute. Running
each supernode as a mini-engine was rejected because of per-level copies and per-path
latency at the wrong abstraction. See ADR-0001.

Sprint 1 (2026-07-27) delivered the kernel, offline CLI and block-processing
contract: `Node` exposes ports, `prepare`, `process` and `latencySamples`, while
`ProcessContext` supplies frame count, block start, audio/event views and `valueAt`.
Values were corrected from fixed-size to size-free. Sprint 2 (2026-07-30) proved
the milestone graph is bit-identical flat, in a supernode and nested one level
deeper, and independent of block size. These are historical results, not current
test counts. Definition/instance propagation and supernode import are deferred;
Save/Open of an ordinary graph prepares for later reuse without implementing import.

Sprint 2 did not deliver outer-port rename/reorder or shared definition-to-instance
propagation. Later identity work settled stale-exposure semantics with ids and
RESTRICT; do not treat the older index-based fragility as an open constraint.

## Implementation conventions

- C++20, two-space indent and approximately 90-column lines. One class per concern;
  `PascalCase` types, `camelCase` methods, trailing-underscore private members,
  `kPascalCase` constants, not SCREAMING names. Header declarations use
  `#pragma once`; kernel implementation belongs in `kernel/src/`. Use fixed-width
  integers for contractual frames, ports and ids, `std::size_t` for indices,
  floats for samples and doubles for phase/time math.
- Kernel code never names or interprets a media concept. Subtype ids are opaque
  `uint32` values; connection checking compares them for equality. Typed payload
  access uses `memcpy` and requires trivially copyable types, never aliasing casts.
- A new primitive subclasses `Node`, declares ports with `addInput`/`addOutput` and
  implements `process()`. Node-owned changes to ports are allowed while authoring;
  constructors are not the only declaration opportunity. Nodes write all outputs
  each block and never keep views after the call. Block size is dispatch granularity,
  not behavior: timing and output must not change with block size.
- Authoring/compile errors throw `std::invalid_argument` with short lowercase
  `"context: what went wrong"` messages. Processing asserts instead of throwing;
  the kernel block loop does not allocate (the documented v1 `EventBuffer::push`
  exception remains). Comments explain why and state invariants rather than
  restating code.
- Test observable invariants with doctest, one layer per test file; name tests as
  sentences stating those invariants. Use odd or prime frame offsets that cannot
  accidentally align with blocks. Keep helper nodes in test files. Build fresh
  graphs for independent renders: nodes hold state. The kernel purity test strips
  comments before checking for media vocabulary.

## Related contracts

- `docs/graph-editor.md`: editor interactions, node settings and execution contracts.
- `docs/node-io.md`: MIDI-file and hardware-node behavior.
- `docs/graph-persistence.md`: Save/Open, id restoration and content codecs.
- `docs/windows.md`: native Windows build, tests and editor setup.
- `.scratch/graph-editor/planning.md`: ticket links and recorded verification evidence.
