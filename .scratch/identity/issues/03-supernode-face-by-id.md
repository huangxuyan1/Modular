# 03 — The supernode face by id, and the claim closes

**What to build:** a supernode author can delete a boundary node from an inner graph, and
every wire into the supernode's remaining outer ports still delivers where it did before.
Sprint 2 shipped this as a recorded fragility — ADR-0001 noted that a supernode's outer port
index is "stable only because boundary nodes cannot be deleted or reordered." Tickets 01 and
02 removed that precondition by giving nodes and ports ids; this ticket removes the
fragility.

The mechanism needs no new concept. Both boundary nodes and their ports draw ids from the
inner graph's one `IdSource`, so two boundary nodes in one inner graph cannot collide — which
is exactly why graph scope was chosen over node scope in ADR-0004. A supernode's outer port
id therefore **is** the port id of the boundary node backing it: nothing borrowed, nothing
converted, no side table.

This is the last ticket of the claim, so the documentation and the regression suite land with
it. The close-out review follows it and is not part of it.

**Blocked by:** 02 — Port identity, port mutation, and the claim's demo.

**Status:** done

- [x] A supernode's outer port id is the port id of the boundary node backing it, with no
      conversion step.
- [x] Two boundary nodes in one inner graph have different port ids — the collision that
      killed node-scoped ids in ADR-0004 cannot occur.
- [x] An inner graph's ids mean nothing outside it, so a supernode snapshot drops into
      another project unchanged. Its by-value ownership is unaffected.
- [x] The derived face is addressed by id rather than position; deriving it stays off the
      block loop, as it is today.
- [x] A supernode's outer port ids survive arbitrary edits to its inner graph, so a wire into
      a supernode is not disturbed by work done inside it.
- [x] **Falsifiable demo, must fail before the change:** a supernode exposes several ports;
      wire a later one from outside; delete an earlier boundary node; render. The surviving
      outer wire still delivers to the same inner consumer and the output is byte-for-byte
      unchanged.
- [x] A boundary node with wires in its own inner graph cannot be deleted: the inner graph's
      ordinary RESTRICT rule, unchanged. An outer wire left on a vanished outer port still
      fails at compile with the existing error, before anything renders. The kernel gains no
      delete-time check for outer wires (D7 below).
- [x] ADR-0001's "known fragility, waiting on deletion" note is retired in the ADR itself,
      matching the strike-through already recorded in CLAUDE.md.
- [x] Full regression: the milestone render, the grouped supernode render, the nested
      supernode render and block-size invariance all byte-identical; the whole suite green.
- [x] `scripts/check_kernel_purity.sh` still returns nothing — ids, deletion and RESTRICT
      name no medium.
- [x] CLAUDE.md updated: the status line, and anything describing the graph's authoring API
      or the frozen-ports rule.
- [x] `docs/roadmap.md` appended — it is a running artifact, so the claim sharpens it rather
      than waiting for a final write-up.
- [x] Map ticket 03 (`.scratch/roadmap/issues/03-variable-arity-after-graph-add.md`) noted as
      implemented, and its requirements handoff to map ticket 10 (`prepare` on recompile)
      left open and unchanged — that ticket is out of scope here.

## Decisions (2026-11-15)

- **D7: outer wires follow the face; the kernel does not refuse.** Refusing to delete a
  boundary node that backs an outer wire would make an author leave the supernode, remove
  the outer wire by hand and come back. Instead, shared editing in `workspace/` compares the
  supernode's outer port ids before and after each edit to its inner graph. Outer wires on a
  port that vanished, or whose type changed so the wire no longer connects, are removed as
  part of the same edit and reported to the user, so a later Undo restores both together.
  Stable port ids make this a set comparison. The kernel does not change: a node keeps no
  pointer to its graph (ADR-0004 rejected one because graphs move), so an inner graph cannot
  see outward, and a caller that skips the workspace still gets the existing compile error.
  The reconciliation arrives with supernode editing in the editor, not in this ticket.
- **D8: close-out review and commit.** Tickets 01 and 02 landed inside the single public root
  commit, so they have no diff to review against. The close-out review is `/code-review` on
  this ticket's PR plus a read-through of ADR-0004 against the current code. The squash
  commit is titled `Close: <the claim>`, per the claim anatomy in `.scratch/roadmap/map.md`.

## Comments

**2026-11-20 — implemented; the mechanism was already there.** Ticket 02 recorded that "some
of ticket 03 landed here, unavoidably": once a wire named a `PortId`, `Flatten` could no
longer read a face by position. So no kernel source changed in this ticket. What it adds is
proof, the demo in full, and the documentation. Per item:

- *Outer port id is the boundary node's port id.* `BoundaryNode::port()` is
  `exposed().id`, `Supernode::derive()` copies the boundary node's own `PortSpec`, and both
  crossings in `Flatten` pass the same id through (`resolveInput(inner, backing, port, …)`
  and `resolveInput(*f.parent, f.superId, port, …)`). Test: *"a supernode's face is derived
  from its boundary nodes in insertion order"* (face id == `b->port()`, `boundaryFor`).
- *Two boundary nodes differ.* Existing test *"two boundary nodes in one inner graph have
  different port ids"*.
- *Snapshot drops in unchanged.* New test *"an inner graph is its own id namespace, so a
  snapshot drops in unchanged"*. A section built on its own graph mints the same ids every
  time it is built. Moved into supernodes in two projects with different id histories, it
  keeps those ids as its face and renders identically, and the source graph is left empty.
  By-value ownership: existing *"a supernode owns its inner graph, so no two can share one"*.
- *Face addressed by id; derivation off the block loop.* `boundaryFor(PortId)` is a lookup,
  not a conversion. `derive()` runs only when a supernode's `inputs()`/`outputs()` are read,
  which happens while authoring (`connect()` reads them, for one). The compile never derives
  a face: `Flatten` crosses with `boundaryFor()`, and the `Engine` constructor reads the
  ports of primitive nodes only. `Engine::process` walks the compiled wiring only, and a
  supernode never executes. Read from code, since no seam observes it.
- *Ids survive arbitrary inner edits.* New test *"a supernode's outer port ids survive any
  edit to its inner graph"*. An inner node comes and goes, an inner node gains and loses a
  port, the face grows on both sides, and its first pair is deleted. All four surviving outer
  ports keep their ids and their `boundaryFor`, and both outer wires are unchanged.
- *Demo.* New test *"deleting an earlier boundary node leaves a later outer wire on its own
  lane"*. It replaces ticket 02's early version (*"deleting a boundary node leaves the rest of
  the face on its own ids"*). That version deleted an unwired boundary node, so under
  positional addressing the wire would have thrown rather than moved. The new one has three
  lanes of weight 1/2/4 and wires lane 1 by the ids the face shows. It unwires lane 0 inside,
  deletes its two boundary nodes and renders. The output is byte-identical, and an in-tree
  check that lane 2 renders differently keeps the test from passing vacuously.
  **It did not fail before the change on this branch.** The change landed with ticket 02
  inside the single public root commit, so no pre-change build exists. To falsify it anyway,
  the face was temporarily made positional: `derive()` numbered face ports 1..n,
  `boundaryFor()` indexed by that number, and both `Flatten` crossings translated to and
  from it. That change is kept as `falsify-positional-face.diff` beside this ticket; to
  rerun, `git apply` it, run `kernel_tests`, then restore the two files. On the final test
  code the demo failed on its face-id checks and on the sample comparison
  `renderWith(1, true) == kept`: the wire reached lane 2. 14 of 44 kernel tests failed in
  that run, the other two new face-id tests among them. The change was reverted.
- *Boundary RESTRICT; compile-time backstop; no delete-time check.* New test *"a boundary node
  wired inside its supernode cannot be deleted until unwired"* covers a wire leaving the node
  and one arriving. `connectionsOf` names both, and the deletes succeed once the wires are
  removed. Existing *"a wire into a deleted outer port fails at compile, never lands
  elsewhere"* still passes. Its stale comment said this ticket would move the refusal to
  deletion time; it now states D7. The kernel gained no check, and `Graph.h`'s class comment
  says that RESTRICT sees one graph.
- *ADR-0001.* The note was not in ADR-0001 as published, and the current, shortened
  CLAUDE.md has no strike-through. The fragility was recorded in
  `.scratch/supernode/issues/02-engine-flattens-by-resolution.md`. ADR-0001 now ends with a
  dated *Identity note* that quotes that record and states the retirement, pointing at
  ADR-0004.
- *Full regression.* `scripts/build_windows.ps1` (Debug, all targets): 8/8 CTest entries
  passed, including the real-window `graph_editor_app` check and kernel purity.
  `kernel_tests` ran 44 cases: the 41 it had, less the early demo, plus four new ones. The
  milestone, grouped, nested and block-size-invariance renders are among them, unchanged and
  byte-identical.
- *Purity.* `scripts/check_kernel_purity.sh` reports `kernel purity: clean`.
- *CLAUDE.md.* It has no kernel status line now and no frozen-ports or authoring-API prose
  beyond "graph-scoped NodeId/PortId, RESTRICT on wired primitive deletion", which is still
  accurate. Added a **Roadmap** bullet with a pointer to `docs/roadmap.md` and the latest
  closed claim, byte-identical in `AGENTS.md`. The frozen-ports leftovers ticket 02 passed on
  are fixed: `CONTEXT.md`'s executed-graph entry and the roadmap's present-tense "freezes".
  `docs/project-foundations.md`'s RESTRICT sentence now states D7.
- *Roadmap and map ticket 03.* The claim's row is added to Proven and carries its limit:
  state across a recompile is ticket 10's. The claim's block moved from Next claim to
  Proven, as the payload claim's did, with a *Closed* note on its two sharp edges ("or its
  DSP state" stops at recompile; outer wires are the workspace's). Next claim opens with a
  dated re-cut line and now holds only the channel-layout claim. Map ticket 03 has a dated
  comment, and map ticket 10 is untouched.

**2026-11-20 — close-out review (D8).** `/code-review` on PR #4 at 05983f9, against
`fab2c89` (main), with the Standards and Spec axes run as separate agents. Fixes landed in
the follow-up commit.

- *Standards: no hard violations.* `CLAUDE.md` and `AGENTS.md` are identical; indent,
  columns, naming, test-name sentences, seams and avoided vocabulary all pass. Judgement
  calls taken: the parallel `ins`/`sums`/`outs` vectors became one `Lane` (data clump), and
  `buildDoubler()` now reuses `addLane()` (duplication). Port ids named `in` beside the
  file's inner-graph `in` became `wiredIn`/`wiredOut` and `audioIn`/`audioOut`.
  `dropLane()` takes the inner `Graph&` instead of mutating through a `const` reference.
  The demo's copied "prime: the note onset…" comment was dropped, since 2003 is the render
  length, not the onset. "Patch" became "change" in the roadmap, because "patch" is an
  avoided graph word. Not taken: the same inherited comment at two older call sites.
- *Spec: nothing missing.* The demo can tell id from position: it misroutes rather than
  throws under a positional face. Fixed:
  - ADR-0001's note said the outer wire "is reconciled", in the present tense; it now says
    this is not built and the compile rejects the wire.
  - The Proven row claimed "DSP state" without its limit; it now carries it.
  - The closed claim still sat under Next claim; it has moved to Proven.
  - This record said `derive()` runs in the `Engine` constructor; it does not.
  - The edit-survival test checked `boundaryFor` for two of the four surviving ports; it now
    checks all four.
  - The falsification change was not kept; it is now beside this ticket.
  - The record named only the sample comparison as failing; the face-id checks failed too.
- *ADR-0004 read-through against the code.* Every structural statement holds:
  - `Connection` names four ids, and one heap `IdSource` per graph mints node and port ids.
  - A node holds an `IdSource*`, never a `Graph*`.
  - `frozen_` is gone.
  - `addNode<T>` constructs the node in the graph.
  - `PortSpec` carries its id.
  - Deletion is RESTRICT, within one graph per the 2026-11-15 note.
  - The face is addressed by id, and the engine compacts ids at compile.
  - The purity grep is clean.
  - The 2026-09-13 explicit delete exists as `Workspace`'s unwire-then-remove.

  Two wording gaps were found. (1) The 2026-11-15 note described the workspace
  reconciliation in the present tense; it now says the operation is not built yet. (2) The
  ADR says an id costs "one extra array read once per port per block", but the compaction
  is a binary search over the node's sorted port ids per accessor call (`PortLookup::find`).
  It is still per block and not per sample, so the reasoning stands. That gap is recorded
  here and the ADR is left as written.
