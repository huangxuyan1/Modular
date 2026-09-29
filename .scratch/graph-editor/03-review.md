# Ticket 03 implementation review

Date: 2026-09-15
Ticket: [Save and reopen an authored graph](issues/03-save-and-reopen-an-authored-graph.md)
Spec: [Windows flat graph editor](spec.md), limited to ticket 03.
Disposition: complete, uncommitted. Tickets 04-05 await invocation.

## Review boundary

Starting and final HEAD: `4163dc85b8ef742ae93829af969301dbfd6a4b1b`.
Commit list `git log 4163dc85b8ef742ae93829af969301dbfd6a4b1b..HEAD --oneline` is empty.

Before implementation, the tracked/untracked working source was copied into
`build/ticket03-baseline/`. Independent Standards and Spec agents reviewed the
actual ticket delta, including newly added files, in `build/ticket03-review.diff`.
The diff was generated separately for kernel, nodes, workspace, apps, tests, docs
and CMakeLists.txt using:

```powershell
git -c core.autocrlf=false diff --no-index -- build/ticket03-baseline/<item> <item>
```

This includes the current uncommitted implementation while excluding the earlier
identity, node-I/O and tickets 01-02 changes. Review did not use a HEAD-only diff.
The final refreshed artifact also includes the paired instruction-file updates.
Completion records report the review; they do not claim review of unrelated work.

## Standards

Initial independent report: **No Standards findings.** The delta preserves
kernel/media separation, restores IDs during construction as ADR-0004 requires,
retains allocation history, and keeps persistence failures on authoring paths.
Tests exercise observable round-trip behavior and failure preservation.
No actionable baseline code smells were found.

The independent incremental recheck found **no new Standards violations or
actionable code smells**. It confirmed that capacity checks preserve the existing
configuration contract and that consumed identities remain retired and unsaved.

Final Standards result: **0 remaining findings**.

## Spec

Initial independent report: **One P2 finding — failed edits can change a loaded
graph without marking it unsaved.** With `NEXT 4294967295`, growing a two-input
mixer to four could allocate the third port and then fail. A simultaneous channel
change also left port types inconsistent with the node's channel setting. The
app marked dirty only after successful completion. Failed constant-node creation
could similarly consume a port identity before adoption failed, changing history
while the app remained clean.

Resolution: variable-arity growth now checks available identity capacity before
mutation. The app exception boundary detects changed allocation history and marks
it unsaved. Public integration and actual-app regressions first reproduced the
failures, then passed with the fixes. The same introduced exhaustion risk in the
existing voice allocator received a capacity guard and public regression.

Independent recheck: **finding resolved; no remaining findings.** It confirmed
unchanged failed growth, final-ID allocation, Cancel retention and explicit Save
of consumed identity history. No other missing requirements, scope creep or
incorrect behavior was found within ticket 03. MIDI and stored-audio coverage
remains deferred to ticket 04.

Final Spec result: **0 remaining findings**.

## Final validation

Both commands passed after all source changes:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1 -Configuration Release
```

Each passed all seven CTest entries: 41 kernel, 11 node, 13 authoring and 15
MIDI-file cases, actual-app gestures/persistence, CLI smoke and kernel purity.
The client-area rendering capture was inspected. File tests use isolated temporary
artifacts and Windows Unicode paths; app tests exercise native dialogs and a real
Windows sharing violation rather than substituting Graph or Engine internals.

The shared sample check drives restored private port references through actual
constant/gain/mixer processing. File rejection covers corrupt references, duplicate
and out-of-range IDs, malformed numbers/content, unsupported kinds/versions and
trailing data. App coverage includes all Save/Discard/Cancel transitions,
cancelled/failed Save, invalid Open, no autosave, layout dirty state, idle Open
from Run and normal Save/Discard close during processing.

`git diff --check` passes. AGENTS.md and CLAUDE.md remain byte-identical and below
32 KiB (32,758 bytes each). The final snapshot audit found 49 baseline files
byte-identical, 19 changed paths belonging to this ticket, and no missing baseline
files. See [the API/format guide](../../docs/graph-persistence.md).
