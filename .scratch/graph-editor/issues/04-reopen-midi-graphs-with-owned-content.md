# 04: Reopen MIDI graphs with their content intact

**What to build:** The user saves the playable MIDI graph, moves or removes the
original imported MIDI file, and can still reopen, run and hear the same authored
graph. The saved graph contains the node-owned sequence, not a playback checkpoint.

**Blocked by:** 02: Play MIDI through placed Start/Stop nodes; 03: Save and reopen
an authored graph.

**Status:** ready-for-agent

- [x] Shared Save/Open and the GUI support every node kind needed for the playable
  graph from 02, including manual trigger nodes, MIDI block/file nodes, synth,
  allocator, mixer, gain/value configuration and Windows audio output settings.
  The authored graph, identities and layout survive together.
- [x] MIDI blocks and MIDI-file content are restored as node-owned authored
  content with their timing/message meaning preserved. Saving only the original
  import path is insufficient. A graph reopens and plays after that source file
  has been removed or relocated.
- [x] Node-level reconstruction keeps authored content distinct from runtime
  preparation and scheduling. Reopening starts idle in Edit: held notes, emitted
  events, current samples, device queues and manual controls awaiting execution
  are not resumed.
- [x] Under identical explicit input signals, preparation settings and frame
  horizon, a freshly reopened graph emits the same MIDI and rendered audio as
  the original authored content. Different supported block sizes retain the
  existing output-invariance contract.
- [x] The generic node-content contract also preserves an audio block owned by
  a test node, using real graph Save/Open. The shared mechanism treats its
  representation as node-declared content without MIDI/audio knowledge in the
  kernel. This proves the distinction without adding a production audio-block
  player or waveform editor.
- [x] Restored owned content does not unnecessarily require its import file.
  Actual required external dependencies are reported by the responsible node
  with readiness/error and a suitable repair control. The GUI projects that
  information; it does not invent a universal file policy. Failed imports and
  unavailable playback devices remain actionable.
- [x] Invalid node content and unsupported graph data are reported through the
  shared load contract without silently dropping content or replacing the active
  graph with a partial reconstruction. Failed/cancelled Save and dirty-state
  behavior continue to satisfy 03 for these newly supported nodes.
- [x] Tests use real public author/save/load/run operations, tiny MIDI fixtures
  and observable events/samples. Include source-file removal, stored audio
  content, ID history and a save during/after runtime activity whose reload
  demonstrates authored content survives and transient activity does not.
  Independent comparison runs use fresh graphs.
- [x] An actual-app smoke path builds and saves a playable graph, closes it,
  removes its import source, reopens it idle and uses the placed Start/Stop
  controls to play again. Deterministic rendering stays automated; speaker
  playback is checked separately. Native Debug/Release suites, existing
  supernode equivalence and kernel purity pass.

**Context and scope:** This joins the playback and persistence branches of the
Windows flat graph editor spec, principally stories 29-35, 40 and future graph
reuse compatibility. Both blockers are necessary: 02 defines the actual playable
nodes and runtime inputs; 03 supplies shared reconstruction and GUI Save/Open.

The existing MidiFileNode keeps parsed content, not a guaranteed original file
or byte buffer. Persist its authored meaning through node-owned support rather
than relying on an unavailable path or serializing prepared runtime output.
Refactor only what this round trip requires. Recorder persistence arrives with
05. Supernode import/nested packaging, hot editing, runtime checkpoints and a
broader plugin/type-registry project remain deferred.

## Comments

### Implementation complete (2026-09-19)

Implemented and verified across the continued September 15-19 session. Nothing
was committed; earlier uncommitted work remains preserved. Independent Standards
and Spec reviews have zero remaining findings. See [the review record](../04-review.md).

Primitive persistence now covers every playback node, including authored MIDI
block entries, parsed MIDI-file meaning, manual control settings, allocator arity
and Windows output channels. Existing identities, allocation history, wiring and
layout survive together. The shared `MODULAR_GRAPH 1` container and opaque
`NodeContent` contract are unchanged. The kernel and workspace source were not
changed by this ticket.

MIDI-file timestamps, simultaneous message order, channel bytes, skipped-SysEx
count and duration survive independently of the original import path. Preparation
recomputes frames at the requested sample rate. Reopened nodes start idle; pending
manual presses, held notes, synth phases, cursors and device queues are excluded.
Empty MIDI files remain repairable through Load MIDI, and failed imports retain
prior owned content. Output devices open only on Run. Frame-based MIDI blocks
created by another client can be opened, displayed, wired, moved and saved without
adding a MIDI event editor or another palette item.

Real Save/Open/Engine tests cover source removal, malformed content, ID gaps and
later minting, tempo/message meaning, active and pending-control saves, fresh
MIDI/audio comparisons at block sizes 17, 127 and 512, and owned PCM through a
test-only node codec. The actual app exercises failed/cancelled Save, failed Open,
dirty state, source removal and fresh-process reopening with placed Start/Stop.
Native chooser synchronization waits for selection to finish before later gestures.

Final native Debug and Release each pass all seven CTest entries: 41 kernel,
11 node, 20 authoring and 15 MIDI-file cases, actual-app checks, CLI smoke and
kernel purity. Existing supernode equivalence remains covered. The separate
Windows device check passed on September 16, including fresh-process reopening:
the default-device peak reached 0.499969 after Start and fell to 1e-12 after Stop.
This measures the device stream, not acoustic sound pressure or human listening.
See the review for the hardware timing and validation details.

Representation and node policies are documented in
[graph-persistence.md](../../../docs/graph-persistence.md) and
[graph-editor.md](../../../docs/graph-editor.md).

Next, in a fresh session:
`/implement .scratch/graph-editor/issues/05-record-wav-takes-and-reopen-recording-graphs.md`
