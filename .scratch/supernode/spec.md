# Spec: Supernodes — grouping that flattens

Status: ready-for-agent

## Problem Statement

Modular's core bet is that DAW concepts (tracks, mixers, clips, instrument racks) are
user-composed from plain nodes, not built into the engine. That bet is untestable until
composition itself exists: today the authored graph is flat, so there is no way to bundle
a wiring pattern into a reusable, placeable unit. The reference example the whole design
leans on — a midi-clip supernode — cannot yet be expressed.

## Solution

Introduce the supernode: a node whose implementation is an inner graph it owns by value,
externally indistinguishable from a primitive node. Exposure is declared by boundary nodes
wired inside the inner graph; the supernode's outer face is derived on demand from them.
The engine's existing compile flattens supernodes by resolution — recursing through
supernode shells and rewiring connections straight through boundary nodes — so nothing
about the runtime changes. The falsifiable claim, and the sprint demo: the milestone-1
graph with its midi-clip section grouped into a supernode renders bit-identically to the
flat graph. Decisions and rejected alternatives are recorded in ADR-0001; vocabulary in
CONTEXT.md.

## User Stories

1. As a graph author, I want to group a set of wired nodes into a supernode, so that I can
   treat a wiring pattern as one unit.
2. As a graph author, I want a placed supernode to behave identically to placing its inner
   nodes directly, so that grouping never changes what I hear.
3. As a graph author, I want to expose an inner input by wiring it to a boundary node, so
   that exposure is the same gesture as all other graph editing.
4. As a graph author, I want to expose an inner output the same way, so that inputs and
   outputs are symmetric.
5. As a graph author, I want inner ports I did not expose to stay hidden and read their
   defaults, so that optional inputs don't clutter the supernode's face.
6. As a graph author, I want the supernode's outer ports to update as I add or remove
   boundary nodes in its inner graph, so that the face never drifts from the contents.
7. As a graph author, I want to connect to a supernode's ports with the same type rules as
   any node, so that I don't need to know whether a node is primitive or composed.
8. As a graph author, I want one exposed input to feed several inner nodes, so that
   fan-out works the same inside a boundary as outside.
9. As a graph author, I want to nest supernodes inside supernodes, so that compositions
   can themselves be composed.
10. As a graph author, I want a placed supernode to be a snapshot, so that later edits to
    wherever it came from never silently change my project.
11. As a graph author, I want all three domains (audio, value, event) to cross a supernode
    boundary, so that grouping is not limited to audio chains.
12. As a graph author, I want connecting mismatched types through a boundary to fail at
    connect time, so that composition doesn't weaken type safety.
13. As a node-library author, I want to ship common wiring patterns (e.g. a midi-clip
    supernode) as supernodes, so that the standard library can grow without kernel changes.
14. As an application developer, I want the executed behavior derivable from the authored
    graph alone, so that views and serialization have a single source of truth.
15. As the kernel maintainer, I want supernodes and boundary nodes to add zero media
    knowledge to the kernel, so that the purity rule (grep for "midi" finds nothing)
    survives composition.
16. As the kernel maintainer, I want supernodes erased at compile, so that the runtime
    block loop stays exactly as fast and simple as it is today.

## Implementation Decisions

All recorded with reasoning in ADR-0001 and CLAUDE.md ("Supernode decisions"); summary:

- Supernode is a kernel concept: a node subclass owning an inner graph by value, editable
  through the supernode while authoring. The compiler must understand what it erases —
  the sorting test for kernel membership is "must the compiler understand it," not "is it
  needed at runtime."
- Boundary nodes (kernel, same header as the supernode) declare exposure: an input-side
  boundary node presents one outer input; output-side likewise. Each carries a configured
  PortType, fully opaque to the kernel. They never execute.
- The supernode's outer port lists are derived on demand by walking its boundary nodes in
  insertion order — never stored, so nothing can drift. Primitive nodes keep
  freeze-on-graph-add; derived faces are as editable as their inner graph, and everything
  freezes at compile.
- Flattening is resolution inside the existing engine compile: enumerate primitive nodes
  recursively through supernode shells; when wiring, resolve any connection that touches a
  boundary node through to the real source/destination. No flat graph is materialized; no
  clone contract on nodes. Topo sort, storage allocation, and the block loop operate on
  the resolved set exactly as they do today on flat graphs.
- Composite execution (supernode running its inner graph inside process()) is rejected —
  see ADR-0001.
- Connection cardinality is unchanged kernel law: exactly one connection per input,
  unlimited fan-out — including through boundaries.
- Authoring-path errors (e.g. connecting a supernode port that doesn't exist, type
  mismatch through a boundary) throw invalid_argument per the existing error convention;
  process paths stay assert-only.

## Testing Decisions

Good tests here observe only external behavior at the one existing seam: build a graph,
compile an engine, process blocks, observe captured samples or probe state. The wiring
tables are implementation; peeking at them would test the mechanism instead of the claim.
"Supernode is grouping" means its only observable behavior IS the rendered output.

Trivial cases first (localize failures the big demo would only report as "bytes differ"):

- A supernode containing a single gain node passes audio through identically to the bare
  gain node.
- A value connection through a boundary delivers the value (and an unexposed/unconnected
  value input reads zero).
- An event connection through a boundary preserves offsets sample-accurately.
- Derived ports: adding boundary nodes to the inner graph changes the supernode's
  inputs()/outputs(); order follows boundary-node insertion order.
- Type mismatch through a boundary throws at connect time.

The sprint demo and invariants:

- Bit-identical: the milestone-1 graph with its midi-clip section inside a supernode
  renders byte-for-byte the same as the flat build. Prior art: the block-size-invariance
  test, which already compares full sample vectors across two builds.
- Nested: a supernode inside a supernode, still bit-identical to flat.
- Boundary fan-out: one exposed input feeding two inner consumers.
- Block-size invariance must keep passing with supernodes in the graph (render the
  supernode build at two coprime block sizes).
- Kernel purity: grep the kernel for media names still returns nothing.

Test style follows the existing conventions: doctest in the kernel test file, test names
as invariant sentences, frame numbers that cannot align with block boundaries by accident,
graphs built fresh per render, helper nodes local to the test file.

## Out of Scope

- Node deletion / disconnection APIs (the graph has no deletion at all yet; stale-exposure
  semantics become relevant only then).
- Outer-port renaming and reordering (future boundary-node configuration).
- Definition-vs-instance ("propagate edit to all instances") — snapshot semantics mean
  deferring loses nothing.
- Serialization of supernodes.
- Port groups / multi-input presentation (annotation layer, still open).
- PDC / cycles (the latency hook exists; compensation is a later sprint).
- Any UI or view work.

## Further Notes

- Sprint 2 definition of done, agreed in review of sprint 1: tests pass (including the
  block-size-invariance suite), CLAUDE.md updated, commit only when asked.
- The midi-clip supernode built for the demo should be assembled in test/app code from
  existing milestone nodes — it is user-space wiring, not a new library node.
- Vocabulary: use "supernode", "boundary node", "authored graph", "executed graph",
  "compile", "flatten by resolution" as defined in CONTEXT.md; avoid "group", "macro",
  "subpatch", "inlet/outlet".
