# 03: Save and reopen an authored graph

**What to build:** The user explicitly saves a graph made from the initial editing
palette, reopens the same authored work, and chooses what happens to unsaved edits
before replacing or closing it. The same persistence capability is usable by a
non-GUI Modular client.

**Blocked by:** 01: Author and edit a flat graph.

**Status:** done

- [x] Public shared Modular Save/Open operations round-trip the primitive set
  delivered by 01: node kinds, authored configuration, actual ports/connections
  and layout metadata. Demonstrate the same operations through both the app and
  real-graph integration tests without GUI-owned serialization.
- [x] Preserve NodeId, PortId, port identity/order as applicable and the graph's
  IdSource high-water mark, including gaps from deleted nodes and ports.
  Subsequent additions cannot reuse deleted identities or retarget a connection.
  Nodes' internally retained port references work after reconstruction.
- [x] Reconstruct supported nodes through appropriate shared/node interfaces.
  Replaying surviving constructors with fresh IDs or patching only connection
  IDs is insufficient. Respect the graph-scoped identity and primitive RESTRICT
  rulings; the GUI does not reach into private node state.
- [x] Choose/document an identifiable, versioned representation and the smallest
  reconstruction mechanism needed. It must accommodate node-declared authored
  content alongside settings without media interpretation by the kernel.
  Ticket 04 supplies MIDI and stored-audio-content coverage; a settings-only
  design that discards owned blocks is not a valid foundation.
- [x] Open restores an idle authored graph in Edit. Runtime signals, held notes,
  live buffers and other execution state are not graph checkpoints. Structural,
  setting, stored-content and layout edits affect dirty state; transient
  processing does not.
- [x] The app has one open graph. New, Open or Close that would abandon unsaved
  work offers Save / Discard / Cancel. Save runs only on explicit choice;
  Discard continues without saving; Cancel retains the graph and cancels the
  transition. There is no autosave.
- [x] A failed or cancelled Save leaves the graph open and dirty and cancels the
  pending transition. Invalid/unsupported Open reports a useful error and retains
  the current authored graph. Successful Save records the saved authored state.
- [x] Unsupported versions, node kinds or graph content are explicit failures;
  do not silently omit nodes, flatten structure, remint identities or change
  wiring. If 02 is already present, its not-yet-supported node persistence is
  rejected until 04 rather than silently producing a partial file.
- [x] Persistence represents an ordinary authored graph. A graph can be saved
  normally for future supernode reuse; no second supernode format is introduced.
  Nested-parent persistence/import are deferred, so unsupported supernode
  content is reported explicitly. Existing runtime supernodes remain intact.
- [x] A real client-level round trip verifies topology, configuration, layout,
  surviving IDs, deleted-ID gaps and later minting. Verify loaded node behavior
  through public ports/graph operations, not private counter/member inspection.
  Cover corrupt references/content, unsupported kinds and observable failures.
- [x] Focused actual-app checks exercise Save/Open, New/Open/Close decisions,
  failed/cancelled Save and no autosave. Exercise Windows Unicode paths using
  isolated temporary artifacts. Native Debug/Release suites, identity/supernode
  regressions and kernel purity pass.

**Context and scope:** This implements shared persistence and session behavior in
the Windows flat graph editor spec, principally stories 29, 32-34 and 36-42.
The existing code has no persistence or arbitrary identity-restoration contract;
this is real behavior to add, not GUI packaging around an existing serializer.
Perform the required identity/configuration prefactoring first within this slice.

This ticket can start without 02 and demonstrates saving the initial primitive
graph independently of playback. The owned-media extension joins both branches
in 04. Exact encoding and internal APIs are implementation decisions within these
contracts. Undo/Redo, a complete command/constraint/annotation framework, nested
serialization, import, autosave and crash recovery remain deferred.

## Implementation notes (2026-09-15)

Ticket 03 is implemented, verified and independently reviewed. Nothing was
committed; earlier uncommitted work remains preserved. Tickets 04-05 await
invocation. See [the review record](../03-review.md).

Shared stream/file Save/Open lives in `modular_workspace`. A separate
`modular_primitive_persistence` library supplies ConstFloatNode, GainNode and
MixNode codecs. The `MODULAR_GRAPH 1` container retains node/port identity,
declaration order and names, authored settings, layout, wiring and the ID source's
high-water mark. `NodeContent` carries opaque settings and owned bytes; the shared
container does not interpret either. Playback kinds and nested structures fail
explicitly until supported by later tickets.

`Graph::restoreNode` supplies saved port identities during node construction,
validates declarations and keeps subsequent minting above the saved history.
The mixer restores middle-deletion gaps and later input additions without
reminting or patching private references. Missing mandatory primitive ports and
zero-input mixers are explicit unsupported configurations outside the initial
GUI's editing operations. Existing runtime supernodes and RESTRICT remain intact.

The app exposes File/New, Open, Save, Save As and Exit, with Ctrl+N/O/S. A title
asterisk marks unsaved authored changes. Save/Discard/Cancel governs replacement
and exit; invalid Open and failed/cancelled Save retain the graph. Saves replace
the destination only after a complete temporary write/close. Unicode paths work.
Open restores Edit and waits for any active execution worker to release the old
graph. Generated signals do not affect saved state or dirty tracking.

Verification: native Debug and Release each passed all seven CTest entries:
41 kernel, 11 node, 13 authoring and 15 MIDI-file cases, actual-app checks, CLI smoke
and kernel purity. Shared tests cover observable processing after restoration,
surviving IDs, gaps, later minting, malformed content, unsupported nodes, Unicode
files and failed-write preservation. Actual-app checks cover Save/Open/Save As,
layout/settings restoration, all New/Open/Close choices, cancelled Save, a real
Windows sharing-violation Save failure, unsupported/invalid content, no autosave,
Open from Run and Save/Discard while closing a running graph.

TDD red/green evidence: shared round-trip API, file Save/Open, and native Save/Open
were tested before their implementation. Review found a near-exhaustion failure
that partly grew a mixer while leaving the graph clean. Public/app regressions
reproduced it; capacity preflight and failed-construction history tracking fixed
it. The equivalent allocator growth guard also has a red/green public regression.
Independent rechecks leave zero Standards and zero Spec findings.

Implementation details and the representation contract are in
[graph-persistence.md](../../../docs/graph-persistence.md).

Next, in a fresh session:
`/implement .scratch/graph-editor/issues/04-reopen-midi-graphs-with-owned-content.md`
