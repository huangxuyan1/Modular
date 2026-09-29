# 03 — The sprint demo: grouping is bit-identical

**What to build:** the falsifiable claim, tested end to end. Assemble the midi-clip
supernode in test code from existing milestone nodes (user-space wiring — not a new
library node) and prove that grouping changes nothing: the milestone-1 graph with its
midi-clip section inside a supernode renders byte-for-byte equal to the flat build.
Nesting and block-size invariance are exercised on the same comparison. Closes sprint 2.
Spec: `.scratch/supernode/spec.md`. Prior art: the existing block-size-invariance test's
full-vector comparison shape.

**Blocked by:** 02 — The engine flattens supernodes by resolution.

**Status:** ready-for-agent

- [x] Milestone-1 graph with its midi-clip section as a supernode renders bit-identically
      to the flat build (full sample-vector equality, graphs built fresh per render)
- [x] A supernode nested inside a supernode still renders bit-identically to flat
- [x] Block-size invariance holds on the supernode build at two coprime block sizes
- [x] Kernel-purity grep still clean across the finished sprint
- [x] CLAUDE.md status updated to close sprint 2 (commit only when asked)

## Comments

**Implemented 2026-07-30.** Three tests in `tests/test_kernel.cpp` (suite 21 → 24 green),
plus `scripts/check_kernel_purity.sh` wired into ctest. No kernel change was needed: the
nesting path ticket 02 flagged as traced-by-inspection-only worked as written.

- **One builder, three groupings.** `buildMilestoneGraph` took a `Grouping` parameter
  (`Flat` / `Clip` / `Instrument`) instead of gaining a parallel copy per variant, so the
  clip content and the wiring have exactly one definition. Two builds that "look the same"
  would make the bit-identity claim about the test's own duplication rather than about
  grouping.
- **`Instrument` is what tests nesting hardest.** It puts the clip supernode *and* the
  synth inside a second supernode, so the trigger crosses two boundaries with nothing
  executable in between — the parent-climbing path (`Frame::parent`/`superId`) that 02
  could only inspect. Block-size invariance is asserted on this build, not the flat one:
  boundaries are the plausible place for a block to leak into timing. That test also
  compares grouped against *flat* at 331 (review catch): grouped-vs-grouped alone would
  pass a boundary that shifted timing equally at both block sizes.
- **The tests were falsified before being believed.** They passed on first run — flattening
  already worked — so the clip inside the supernode was temporarily perturbed: both
  bit-identity cases failed, the block-size case (grouped-vs-grouped) stayed green, exactly
  as it should. Reverted, all green.
- **Kernel purity became a ctest test, not a manual grep.** It is a rule about the source,
  not observable behaviour, so it is `scripts/check_kernel_purity.sh` rather than a doctest
  case. It strips comments before searching: several kernel comments name MIDI or tempo
  precisely to state that the kernel does not know them, and a check that flagged those
  would train people to ignore it. Comment-stripping is an awk scanner rather than a sed
  line filter (review catch — a line-based strip missed *multi-line* block comments, which
  is exactly where a long "the kernel does not know MIDI" note would live). Verified three
  ways: clean, exempt on a planted multi-line comment, failing on a planted identifier.

- **The test file's helpers moved up rather than multiplying.** `renderMilestone` now backs
  the pre-existing block-size test too (it had been a hand-rolled copy), and the repeated
  "is this render actually sounding?" loop became `isSounding`. Every graph builder lives
  in the one leading namespace, so the forward declaration is gone.

Carried forward unchanged: the latent fragility 02 recorded (outer port *index* in a
connection is stable only while boundary nodes cannot be deleted or reordered).
