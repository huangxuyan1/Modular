# 01 — Supernode and boundary nodes exist, with derived faces

**What to build:** an author can build a supernode — edit its inner graph, place boundary
nodes to declare exposure — add it to a parent graph, and treat it exactly like any node:
its outer face (inputs/outputs) is derived on demand from its boundary nodes in insertion
order, connections to it obey the normal exact-type rules, and mismatches throw at connect
time. The engine is untouched in this ticket; everything is demoable at the authoring seam.
Spec: `.scratch/supernode/spec.md`. Decisions: ADR-0001, CLAUDE.md "Supernode decisions",
vocabulary in CONTEXT.md.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [x] A supernode owns its inner graph by value and exposes it for authoring edits
- [x] Boundary nodes declare one exposed port each (direction + opaque PortType); they are
      ordinary graph citizens with no media knowledge
- [x] The supernode's inputs()/outputs() are derived from boundary nodes, never stored;
      adding a boundary node to the inner graph changes the outer face
- [x] Derived port order follows boundary-node insertion order
- [x] Connecting to supernode ports type-checks like any connection; mismatch through a
      boundary throws invalid_argument at connect time (authoring-path error convention)
- [x] Kernel-purity grep still finds no media names in the kernel
- [x] Tests follow repo conventions (invariant-sentence names, red-first via /tdd)

## Comments

**Implemented 2026-07-29.** `kernel/include/modular/Supernode.h` + `kernel/src/Supernode.cpp`
(`Supernode`, `BoundaryNode`, `BoundaryDirection`); 5 new kernel tests, suite now 16 green.
Engine untouched, as scoped. Two decisions worth carrying into ticket 02:

- **`Node::inputs()`/`outputs()` are now virtual.** Forced by "derived on demand, never
  stored": the base accessors read frozen vectors, and a supernode's face must stay
  editable after it joins a parent graph. Deriving allocates, so an override must stay off
  the block loop — true today because supernodes never `process()`. Contract is documented
  at the declaration in `Node.h`.
- **`Graph` is now explicitly move-only** (deleted copy, defaulted move). "Owns by value"
  had been resting on `vector<unique_ptr<Node>>` merely failing to compile when copied —
  which `std::is_copy_constructible_v` reports as *true*, since vector's copy ctor is not
  constrained on its element type. Now the snapshot rule is enforced, not incidental.

Not built, deliberately: outer-port → inner-boundary-node resolution (`inputBoundary()`
etc.) was written, then removed in review as ticket-02 API with no consumer yet. The
private walk in `Supernode::derive()` is the single definition of outer port order —
ticket 02's resolver must read that same order or indices and wiring will disagree.

Known residue for a later ticket: `Supernode` inherits `Node`'s unused `inputs_`/`frozen_`
storage and `addInput()`. A future subclass calling `addInput()` would silently not appear
on the face. Fixing it means splitting `Node` into an interface plus a stored-ports base —
out of scope here.
