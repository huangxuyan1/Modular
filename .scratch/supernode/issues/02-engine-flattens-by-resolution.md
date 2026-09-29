# 02 — The engine flattens supernodes by resolution

**What to build:** graphs containing supernodes actually render. The engine's compile
enumerates primitive nodes recursively through supernode shells and resolves any
connection touching a boundary node straight through to its real source/destination — no
flat graph materialized, no clone contract; supernodes and boundary nodes never execute.
Includes the enabling prefactor: the engine walks a resolved node set instead of assuming
one flat graph indexed by NodeId (behavior unchanged for flat graphs; existing 11 tests
must stay green throughout). Spec: `.scratch/supernode/spec.md`. Decisions: ADR-0001.

**Blocked by:** 01 — Supernode and boundary nodes exist, with derived faces.

**Status:** ready-for-agent

- [x] Prefactor lands first with all existing tests green and no behavior change
- [x] A supernode containing a single gain node renders bit-identically to the bare gain
      node wired directly
- [x] A value connection through a boundary delivers the value; an unexposed/unconnected
      value input inside reads zero
- [x] An event connection through a boundary preserves offsets sample-accurately (pick a
      non-boundary-aligned frame)
- [x] One exposed input fans out to two inner consumers (fan-out through boundaries)
- [x] Unexposed inner ports read their domain defaults (silence / empty / zero)
- [x] Process paths stay assert-only, no allocation added to the block loop
- [x] Kernel-purity grep still clean; existing block-size-invariance test still passes

## Comments

**Implemented 2026-07-30.** New `kernel/src/Flatten.h` + `Flatten.cpp` (`flatten()`,
`ResolvedNode`, `SourcePort`); `Engine` now compiles from the resolved set; `Supernode`
gained `boundaryIds()`. 5 new tests, suite 16 → 21 green.

- **The prefactor is the whole shape change.** `Engine::Impl` indexed everything by
  `NodeId`; it now holds `std::vector<ResolvedNode>` and indexes by position, because the
  executed graph is drawn from a *hierarchy* of graphs and a NodeId only means something
  relative to one of them. Topo-sort edges now come from resolved sources rather than
  authored connections — a connection landing on a boundary node is not a dependency
  between two things that run. Verified no behavior change by rebuilding `render_cli` at
  376a286 and diffing the milestone WAV: byte-identical (sha 6df323c5).
- **Resolution only ever chases sources.** Each primitive input asks "what feeds me",
  walking through boundary nodes and supernode shells until it reaches something that
  runs. Destinations are never resolved — fan-out means a destination walk would have to
  branch, and cardinality already guarantees a source walk cannot.
- **`Flatten.h` lives in `kernel/src/`, not `include/modular/`**, deviating from the
  documented header layout on purpose: the executed graph is the engine's business and no
  node or application names it.
- **`Supernode::boundaryIds()` is the resolution API ticket 01 wrote and then removed for
  having no consumer.** It now has one, and `derive()` was rewritten to read from it, so
  outer port order has exactly one definition — the risk 01 flagged is closed.
- **Unasked-for, kept:** a cycle guard in `resolveOutput`. A loop of pass-through
  supernodes containing no primitive would make resolution recurse forever — a compiler
  hang, where every other cycle here throws. Reuses the existing cycle message.

Not covered, per scope: **nesting is untested until ticket 03**, and the parent-climbing
path (`Frame::parent`/`superId`) is what nesting exercises hardest — traced by inspection
only. That is the main risk ticket 03 carries.

Latent fragility for whenever deletion arrives: a connection stores an outer port *index*,
resolved through `boundaryIds()`. Stable only because boundary nodes cannot be deleted or
reordered — both out of scope, and the spec already notes stale-exposure semantics start
mattering with deletion.
