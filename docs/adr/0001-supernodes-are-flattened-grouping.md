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
