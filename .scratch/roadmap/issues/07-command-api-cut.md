# How is the command/edit API cut?

Type: grilling
Status: open
Blocked by: 02, 03

## Question

The DAW is a mapping from user gestures to engine operations: drag a MIDI file in, and the
app calls the engine to add a midi-block node connected to the track supernode. That
presupposes a layer that doesn't exist. Today `Graph` exposes `addNode` and connection
edits directly, with no atomicity, no undo, and no story for editing a graph an `Engine` is
currently running. Ids are no longer missing: map ticket 03 has them minted per graph at
authoring time ([ADR-0004](../../../docs/adr/0004-ports-and-nodes-have-graph-scoped-ids.md)),
and Save/Open restores every saved id ([graph persistence](../../../docs/graph-persistence.md)).

The work is entangled, so the decision is mostly about where to cut:

- **Stable node ids** pull in serialization, which pulls in the deferred
  `value<T>`/`event<T>` parameterized-vs-dynamic ruling, since type ids must survive a
  round trip. Does the API need persistence-grade ids on day one, or session-scoped ones?
  *Since answered:* ids are persistence-grade; they survive Save/Open.
- **Atomicity.** "Add a node and connect it" must not half-apply. Is that a transaction
  concept, or is it avoided by making the whole edit-then-recompile step the unit?
- **Undo/redo.** If the graph is the single source of truth and apps issue commands, undo
  is a property of the command layer, not of the graph. Confirm, and decide whether undo is
  in this cut at all.
- **The offline/live cut.** Recommended shape is offline editing first (edit, recompile,
  play), live editing later (edit while audio runs). Live editing drags in the flagged
  hard/open problem — recompilation mid-playback without dropout — plus thread safety
  between the audio callback and the editing thread, which is a kernel-level ownership
  question the project has never addressed.
- Does the API surface graph edits, or higher-level operations? Higher-level risks the
  engine learning what a track is.

Ticket 03's ruling matters here: if ports can grow after graph-add, "reconfigure a node" is
an API operation; if not, it's delete-and-recreate, which needs deletion to exist first.
