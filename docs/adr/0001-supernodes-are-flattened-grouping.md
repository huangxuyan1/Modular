# Supernodes are flattened grouping: boundary-node exposure, erased at compile

A supernode is a node whose implementation is an inner graph it owns *by value* (placing
one snapshots it; source edits never propagate). Its exposed ports are declared by
**boundary nodes** wired inside the inner graph, and its outer face is *derived on demand*
from them — never stored. At compile the engine **flattens by resolution**, like a compiler
inlining a function: it recurses through supernode shells and rewires connections straight
through boundary nodes into its wiring tables. No flat graph is ever materialized, and
neither supernodes nor boundary nodes exist at runtime.

## Considered Options

- **Port-mapping table** instead of boundary nodes — rejected: a table entry is the same
  (node, port) reference a connection already is, but stored outside the graph, so
  deletion cleanup, type-checking, and fan-out would each need re-implementing; boundary
  nodes get all three free from existing connection rules.
- **Composite execution** (the supernode runs its inner graph inside `process()`) —
  rejected: it duplicates the engine (storage, topo sort, event clearing, inner PDC) per
  supernode per nesting level, and it forces per-path latency onto the node interface,
  since a box with multiple internal routes has no single honest `latencySamples()`.
  Flattening keeps latency one number per *primitive*; per-path latency emerges as sums
  along visible paths, and the compiler inserts delay only at real joins.
- **Materializing a flat Graph** — rejected: nodes are stateful instances owned by the
  authored graph, so a materialized copy needs a clone contract or ownership theft.

## Consequences

- Authored graphs legally contain nodes whose `process()` is never called (supernode
  shells, boundary nodes). This is deliberate, not a bug.
- `Supernode` and `BoundaryNode` live in the *kernel*, despite the minimal-kernel rule:
  the compiler must understand what it erases (functions are language, not stdlib). The
  sorting test for kernel membership is "must the compiler understand it," not "is it
  needed at runtime." Both classes are pure structure; the kernel-purity grep stays clean.
- "Functionally identical to placing the inner nodes directly" is a testable claim:
  flat vs supernode renders must be bit-identical.

## Identity note (2026-11-20)

Sprint 2 left one known fragility here, waiting on deletion: a connection stored an outer
port *index*, "stable only because boundary nodes cannot be deleted or reordered"
(`.scratch/supernode/issues/02-engine-flattens-by-resolution.md`). ADR-0004 retires it. A
supernode's outer port id is the port id of the boundary node backing it, so the face is
addressed by id and its order is display order only: deleting a boundary node from the inner
graph leaves every surviving outer wire addressing what it did. A boundary node still wired
inside follows the inner graph's RESTRICT rule. An outer wire on its port is for shared
editing to remove with the edit, per ADR-0004's 2026-11-15 clarification; that is not built
yet, and until it is, the compile rejects the stale wire.
