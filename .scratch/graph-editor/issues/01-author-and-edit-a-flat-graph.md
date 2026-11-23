# 01: Author and edit a flat graph

**What to build:** A Windows app where the user creates a flat authored graph by
placing primitive nodes, configuring them, moving them, and connecting their real
typed ports. A deliberate delete operation removes a selected node and its wires.
This is a usable editing slice backed by shared Modular behavior.

**Blocked by:** None (can start immediately).

**Status:** done

**Implementation:** complete (2026-09-14; uncommitted)

- [x] The Windows app opens one graph session with a canvas and a small initial
  palette of existing primitive nodes. Use ConstFloatNode, GainNode and MixNode as
  the minimum useful initial set; document any additional existing primitives
  exposed in this slice so ticket 03 can cover the same set.
- [x] The user can drag instances onto the canvas, select and move them, and see
  each node's actual input/output names, directions and types. Presentation
  follows NodeId and PortId, not insertion positions or a second topology model.
- [x] Appropriate creation and Edit-mode settings are exposed for the initial
  nodes, including the constant value and relevant channel/arity configuration.
  Supported edits use node/shared authoring interfaces, preserve unchanged
  identities, and report incompatible changes without corrupting the graph.
  Creation settings and runtime inputs are described honestly.
- [x] Connecting and disconnecting ports changes the real authored graph through
  shared Modular operations. Exact type compatibility, one wire per input and
  free fan-out remain the existing graph rules. Surface errors from the relevant
  Modular operation; do not introduce an independent GUI type/cycle validator.
- [x] Explicit delete-with-connections is a reusable Modular operation invoked
  by the GUI. It removes the selected node's attached wires and then the node;
  unrelated nodes and wires survive. Primitive removal still rejects connected
  nodes/ports under RESTRICT. IDs of deleted objects are not reused.
- [x] Node positions and other authored presentation data have a shared
  association with the authored graph, ready for later persistence. Kernel
  processing remains independent of coordinates and the UI dependency.
- [x] The slice is demonstrable by placing a constant, gain and mixer, editing
  settings, wiring compatible ports, seeing an invalid wire rejected, moving
  nodes, disconnecting and deleting a wired node. It can be verified without
  playback or persistence from later tickets.
- [x] Public Modular integration tests use real Graph and node implementations
  to check edits, validation, identity stability and explicit deletion. A small
  actual-app check verifies the palette/canvas gestures and reported errors.
  Assert observable state rather than private fields or internal call counts.
- [x] Native Debug and Release checks pass, including existing graph/supernode
  regressions and kernel purity. Document how to build, launch and verify the
  app using the project's Windows build workflow.

**Context and scope:** This implements the authoring portion of the Windows flat
graph editor spec, principally stories 1-10 and the single-session foundation.
Choose the GUI dependency within the app; the kernel stays framework-independent
and media-neutral. Introduce only the shared editing facilities this behavior
needs. Do necessary small prefactoring first within the slice and keep the
existing tests green; no separate scaffolding or broad command-system project is
required. The existing uncommitted identity work is prior art to preserve.

Execution and musical controls arrive in 02; Save/Open and unsaved-work dialogs
arrive in 03. This slice may remain Edit-only. Hot editing, Undo/Redo, supernode
canvas authoring/import, multiple graph sessions and the wider roadmap remain
deferred. The current audio port representation still uses subtype for channel
count; its agreed future separation is not an automatic prerequisite.

## Implementation notes (2026-09-14)

Implementation was explicitly invoked for this ticket. The Windows SDK app is
`graph_editor.exe`; build, launch, gestures and contracts are documented in
[docs/graph-editor.md](../../../docs/graph-editor.md). The exposed palette is
exactly ConstFloatNode, GainNode and MixNode; ticket 03 must cover that same set.

Shared `Workspace` owns Graph and positions associated by NodeId, and provides
`deleteNodeWithConnections`. Node-owned setters edit constants, channels and mixer
input count. Existing ports retain identity; removed trailing inputs must be
unwired; growing mints fresh IDs. Type changes are validated against affected
connections before publication. Graph connect/disconnect remain authoritative.

Delegated node policies: constants must be finite; channel count and mixer arity
must be positive. The app bounds editable counts at 64 channels and 32 inputs.
These are creation/Edit settings, not new runtime controls. Existing audio type
representation and compile-time cycle validation are preserved.

Native Debug and Release checks passed all seven CTest entries: 40 kernel cases,
7 node cases, 5 authoring cases, 11 MIDI-file cases, actual-app gestures, CLI smoke
and kernel purity. The real-app rendering capture was inspected and a clipped
constant port label corrected. No playback or persistence is used by the demo.
Independent Standards/Spec review is complete: zero remaining findings on either
axis. The Standards reviewer confirmed its one minor duplication was resolved;
focused Debug/Release app checks passed after that refactor. See
[the review record](../01-review.md). No commit was made.

Review baseline: HEAD `4163dc85b8ef742ae93829af969301dbfd6a4b1b` plus the actual
starting working tree captured in `%TEMP%/modular-ticket01-baseline-20260913`.
This preserves the distinction between ticket 01 changes and earlier uncommitted
identity/node-I/O work; a HEAD-only diff is not the ticket's review boundary.
