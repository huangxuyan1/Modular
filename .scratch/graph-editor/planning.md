# Graph editor: reorientation

Status: tickets-published

Completed phase: `/to-tickets`, invoked and approved by the user on 2026-09-13.
Five tickets were published below as `ready-for-agent`. The [spec](spec.md) remains
unchanged. All five were later invoked through `/implement` and are now `done`; see
their implementation notes and review records for delivery and verification.

The premature [spec artifact](archive/premature-spec-20260912.md) is preserved only
as history. The original node-I/O additions were provisional; ticket 02 integrates
them under the agreed spec. The work was uncommitted when this note was written; it
was published in the repository's root commit.

## Interview scope and delegation (2026-09-13)

Label every question **[GUI app]** or **[Modular]** before asking it. The user wants
to decide application behavior and shared Modular behavior. Node-specific choices
are delegated to the assistant: choose, record, and report them afterward instead
of interviewing the user about them. A delegated choice is distinct from a direct
user decision. This delegation authorizes design choices within the current phase;
it does not invoke implementation, specs, or tickets.

## Confirmed goal

On 2026-09-12, the user chose the next near-term outcome:

> Have a simple GUI to drag nodes into the graph and create connections.

The user is returning after a break and wants to avoid spending time on speculative
roadmap questions. Existing open tickets are a record of possible work; whether they
belong before this milestone depends on a concrete need in the editor.

## Confirmed direction (2026-09-12)

- The GUI is an application above Modular; GUI behavior stays outside the kernel.
- Prioritize Windows for this milestone.
- Offer flat graphs of primitive nodes first. Omit supernodes from this first UI;
  preserve the graph model and leave room to expose them later.
- The app must produce audio, rather than stopping at a wiring-only demonstration.
- Start and Stop are nodes the user places and connects. Their buttons emit signals
  along the user's wires to the MIDI node and the WAV-recording node; the GUI must
  not substitute a hidden global transport for those connections.
- The requested recorder begins collecting incoming audio on Start and saves the
  captured audio to a file on Stop.
- Node controls should represent what each node can actually do. Missing behavior
  must be made explicit rather than simulated only in the GUI.
- Audio output and MIDI-file loading should be included if they are contained
  additions to the node library. The assistant prematurely implemented them before
  the spec and tickets; `docs/node-io.md` records the provisional code's capabilities.

## Round 1 decisions (2026-09-13)

The user explicitly accepted all five recommendations, with these clarifications:

- **Q1 accepted for this milestone:** initial Edit/Run modes; hot editing is
  complicated and deferred. This is a scope cut for the first app, not a reversal
  of the intended editable authored-graph model.
- **Q2 accepted for the current MIDI nodes:** Start releases prior held notes and
  restarts from the beginning; Stop releases held notes and cancels remaining events.
- **Q3 accepted for the current WAV recorder:** ignore Start while recording and
  Stop while idle; write each completed take to a separate numbered WAV.
- **Q4 accepted for these nodes:** MIDI completion releases remaining notes and
  leaves the MIDI node idle; the recorder continues until its own wired Stop signal.
  No Finished output is required for this milestone.
- **Q2-Q4 are node-specific contracts.** They do not establish kernel transport,
  universal Start/Stop behavior, or policies imposed by the GUI on other nodes.
- **Q5 accepted, ownership corrected:** include Save/Open, but graph persistence
  must be a reusable Modular capability, not an app-private GUI serializer. The
  initially discussed content included graph structure, node settings, positions and
  MIDI-file references. Round 2 clarifies that node-owned MIDI/audio block content
  itself must be saved, not merely its original file reference. The user's word
  "engine" is treated here as the shared Modular
  graph system; no placement on the C++ `Engine` class has been agreed.

This accepts product decisions within grilling. It does not invoke `/to-spec`,
`/to-tickets`, or implementation.

## Proposed editing surface

A palette of existing node types, a canvas to place and move instances, visible typed
ports, and gestures to connect or remove connections and delete nodes. The existing
`Graph` remains authoritative for nodes, connections, and connection validation.

The editing gestures are a scope proposal, not a completed implementation spec.

## Pre-implementation facts (2026-09-12)

- `TriggerAtNode` fires at a configured stream frame; it has no GUI-triggered input.
- `MidiBlockNode` starts/restarts its stored event sequence on a trigger. It has no
  Stop input. `MidiFileNode` extends it with .mid file loading (formats 0/1, PPQ,
  tempo changes). Both use the same trigger and MIDI ports. There is no event editor.
- `CaptureSinkNode` collects every processed audio block into memory. It has no
  Start/Stop inputs and does not itself save a WAV.
- `apps/render_cli/main.cpp` builds a fixed graph, processes three seconds, and then
  calls `writeWav16` outside the graph.
- `AudioOutputNode` plays mono/stereo audio on the Windows default output device.
  Its bounded queue can wait, so the app must process on a worker thread.
- `apps/midi_cli/main.cpp` proves file loading, graph synthesis, speaker output, and
  offline WAV capture together. Its instrument uses the existing eight sine voices;
  it does not reproduce General MIDI instrument sounds.

Read-only contract check on resumption (2026-09-12):

- `kernel/include/modular/Engine.h:20` and `kernel/src/Flatten.h:38` require the
  authored graph to remain unedited for the Engine's lifetime. Compilation borrows
  the actual node objects. Pausing `process()` alone does not make edits supported;
  the current path is destroy Engine, edit, then construct another Engine.
- `CONTEXT.md` describes the authored graph as editable during execution. That is
  the intended model, ahead of the current implementation. ADR-0004 hands the
  state-preserving re-prepare requirement to roadmap ticket 10. Do not turn an
  initial app editing restriction into a permanent change to that intended model.
- There is no general live-setting or manual-trigger API. Visual movement can be
  independent of execution; a node's constructor configuration is different from
  a supported runtime input. Re-prepare currently resets MIDI playback and reopens
  audio output, so seamless state preservation must not be promised.
- The MIDI nodes have no Stop input, completion output, or cleanup of notes whose
  note-offs are abandoned by a restart. End-of-file duration is queried by the CLI;
  the node does not emit the file's end-of-track event.
- `SineSynthNode` is mono/monophonic, fixed-amplitude, hard on/off, without velocity,
  envelope, controller or program-change interpretation. Polyphony requires actual
  allocator -> multiple synth -> mixer wiring. The allocator tracks note number,
  not MIDI channel. These are capabilities to label honestly, not a mandate to
  implement a richer instrument before the editor.
- `PortType.h` still represents audio channel count through `sub`; ADR-0003's
  separate opaque audio subtype and channel fields are not implemented here.
  Present current port compatibility honestly; do not claim the settled design
  is already available or make its implementation an automatic GUI prerequisite.

The requested interactions therefore need user-space node behavior in addition to
GUI controls. These facts do not justify adding transport or recording concepts to
the kernel.

## Remaining scope questions

Round 1 was answered on 2026-09-13. Preserve the original recommendations and the
actual answers separately; the earlier `sure then` was only a workflow acknowledgement.

| ID | Decision | Settled prerequisite / known constraint | Assistant recommendation | User answer |
| --- | --- | --- | --- | --- |
| Q1 | Editing while processing | Current Engine forbids graph edits during its lifetime; intended editable-snapshot model is ahead of implementation | Initial Edit/Run modes: structural/configuration edits in Edit, visual movement in either; placed buttons still send wired signals during Run | Accepted; hot editing later |
| Q2 | MIDI Stop and repeated Start | User places and wires Start/Stop nodes | Stop releases held notes and cancels future events; every Start restarts from the beginning after releasing prior held notes | Accepted for the current nodes only |
| Q3 | Repeated recording takes | Recorder starts on Start and saves on Stop | Separate numbered WAV per take; Start while recording and Stop while idle do nothing | Accepted for the current recorder only |
| Q4 | MIDI end-of-file and recorder lifetime | MIDI and recording are separate graph nodes | MIDI releases held notes and becomes idle; recorder continues until its own Stop input receives a signal | Accepted as node-specific behavior |
| Q5 | Saving and reopening the graph | User authors a flat graph in the app | Include an app project file for node settings, connections, positions, and MIDI-file references | Save/Open accepted; belongs to reusable Modular functionality, not just GUI |

### Persistence facts checked after Q5

- No graph save/load, persistent node-type key/factory, configuration export/import,
  annotation storage, or `modular_workspace` implementation currently exists.
- Graph/Node expose live topology and port descriptions, but private authored
  configuration and mutable runtime state have no shared persistence contract.
  `MidiFileNode` retains parsed content, not the original path or file bytes.
- `IdSource` only exposes minting; arbitrary saved IDs and its high-water mark
  cannot currently be restored through the public construction API. ADR-0004
  requires these to round-trip. Replaying surviving nodes with fresh IDs is not
  equivalent, because it loses deleted-ID history and private port identities.
- Supernodes own nested graphs with separate ID namespaces. Their outer faces are
  derived from boundary nodes. Flattening for storage would lose authored structure;
  silently dropping supernodes or treating every ID as globally unique is invalid.
- The shared workspace direction is recorded in roadmap decision 02, but persistence
  format/type policy remains open. Open command/constraint/annotation tickets are
  not automatically prerequisite deliverables for this milestone.

### Round 2 decisions and delegation (2026-09-13)

| ID | Decision | Settled prerequisite | Assistant recommendation | User answer |
| --- | --- | --- | --- | --- |
| Q6 | Saved authored configuration versus running checkpoint | Shared graph Save/Open is included | Restore authored graph/configuration/layout, excluding running activity | Save graph/settings AND node-owned MIDI/audio blocks; exclude generated signals, currently playing notes and live samples |
| Q7 | Supernodes in reusable persistence | GUI is flat-first; persistence belongs to Modular; supernodes already exist | Round-trip nested supernodes immediately | Corrected: a supernode uses another graph; save that graph normally for future import/reuse. Saving it alongside a parent graph and import arrangements are explicitly deferred |
| Q8 | A referenced MIDI file is missing | Save/Open includes node content and any genuine external dependencies | Show unresolved node, allow repair, prevent running until ready | Accepted as that node's responsibility, not universal kernel file policy |
| Q9 | Leaving Run/closing with an active take | Initial Edit/Run modes and signal-driven recorder are accepted | Require return-and-Stop or explicit discard | Corrected: GUI may prompt, but may simply exit; the node must release resources and clean unfinished temporary files |
| Q10 | WAV write failure and starting another take | Separate WAV per completed take | Retain failed take; retry/discard; refuse another take until resolved | Delegated to assistant; no further node-specific questions |
| Q11 | Sample boundary and simultaneous Start/Stop | Q2-Q4 are contracts of the current MIDI/recorder nodes | Start-inclusive/Stop-exclusive; Stop wins at the same frame | Delegated to assistant; no further node-specific questions |

The persistence distinction is **stored node content versus generated signal**, not
MIDI/audio versus everything else. A MIDI block belongs to its node and must survive
Save/Open; MIDI events emitted when it receives Start are transient execution output.
An audio block retained as node content likewise survives; currently generated audio
samples, active notes and other in-flight processing are not a running checkpoint.
The shared graph mechanism must preserve node-declared content without interpreting
its media-specific meaning. A filename alone is not a substitute for owned content.

**Supernode scope correction:** use the ordinary graph Save/Open capability to save
a graph that can later be imported into another graph as a supernode. The GUI's
working view of that graph is a graph session. Do not invent a second special
supernode persistence system or require nested-parent serialization now. ADR-0001
and the existing Supernode still describe a by-value inner graph after placement;
the user's save-for-future-import description does not itself request changing that
ownership rule. Live references, propagation from source edits, packaging nested
graphs, and the future import workflow are deferred rather than re-grilled now.

### Node decisions made under delegation (not implemented)

- **Missing content:** a node reports its unresolved external dependency and offers
  a way to repair it; the GUI reflects the node's readiness/error. Restore saved
  node-owned block content directly, without unnecessarily requiring the original
  import file. The shared kernel does not acquire MIDI-file policy.
- **Recorder write failure:** retain the completed failed take in memory, expose
  Retry/Discard, and refuse a new take until resolved. Release this memory on node
  disposal; this is not a promise of crash recovery or graph checkpointing.
- **Recorder shutdown:** abort an unfinished capture, release memory and device/file
  handles owned by the node, and remove its own incomplete temporary files during
  normal teardown. Preserve completed recordings and unrelated files. The GUI does
  not require a wired Stop before it may exit, and does not inject a hidden Stop.
  An unfinished recorder capture is transient under this node's policy; other
  nodes' stored audio blocks remain persistent content.
- **Current MIDI/recorder timing:** Stop wins when both inputs fire at one sample.
  Capture includes the Start sample and excludes the Stop sample. These are node
  contracts, not a new ordering policy imposed on every node by the kernel.

Report material later node choices as decisions, not as questions for the user or
as evidence that implementation already exists. Escalate only if a choice would
change the shared Modular contract, GUI scope, or a decision the user reserved.

### Round 3 decisions (2026-09-13)

| ID | Scope | Decision | Assistant recommendation | User answer |
| --- | --- | --- | --- | --- |
| Q12 | Modular (corrected by user) | Undo/Redo in the initial editor | Include Undo/Redo for authored edits/settings/positions | Shared Modular behavior; lower priority. Do not implement a separate GUI history |
| Q13 | Modular (corrected by user) | Deleting a wired node | Explicitly remove attached wires, then the selected node; keep primitive RESTRICT | Proceed under the prior deletion ruling; the operation belongs to Modular, not a GUI-only implementation |
| Q14 | GUI app | Number of open graph sessions in the first version | One open graph at a time; save/reopen now, multiple graph tabs later | Accepted |

Q12 was misclassified: undo/redo is already assigned to the shared workspace in
roadmap decision 02. The GUI's buttons project that capability. The user places it
below the essential authoring/audio/persistence path; do not make a GUI-only history
as a shortcut or grow the whole old command roadmap into an immediate prerequisite.
Its lower priority does not change its shared ownership.

**Q13 prior answer found:**

- [Roadmap decision 03](../roadmap/issues/03-variable-arity-after-graph-add.md),
  under its Answer, records the user's RESTRICT ruling: a connected port/node
  cannot be removed; the caller unwires first. A force-delete composed from the
  connection query is an optional convenience.
- [ADR-0004](../../docs/adr/0004-ports-and-nodes-have-graph-scoped-ids.md) records
  the distinction between implicit cascade and explicit caller intent.
- The old record used an app-level cascade as its example. The user's current
  correction places the generic delete-with-connections operation in shared
  Modular functionality so every client can invoke the same behavior. Primitive
  RESTRICT stays intact. This is a placement clarification, not silent automatic
  deletion of wires by the primitive graph operation.

The user only requested a follow-up if the prior answer could not be found. It was
found; no further Q13 confirmation is needed. Historical tickets remain unchanged;
the dated ADR clarification and these notes carry the current ownership decision.

### Final GUI decision (2026-09-13)

| ID | Scope | Decision | Settled prerequisite | Assistant recommendation | User answer |
| --- | --- | --- | --- | --- | --- |
| Q15 | GUI app | Closing or replacing a graph with unsaved authored edits | One open graph at a time; Save/Open included | Offer Save / Discard / Cancel for New, Open and Close; a failed Save leaves the graph open | Accepted: prompt the user when the current graph is unsaved; do not save automatically |

This concerns graph edits and stored node content, not a recording-specific exit
restriction. Save runs only after the user chooses it; Discard continues without
saving; Cancel retains the current graph and cancels the pending New/Open/Close.
A failed or cancelled Save likewise retains the current graph. No automatic saving.
Node-owned cleanup still permits shutdown.

### Interview status

The current milestone's decision frontier is empty: Q1-Q15 have answers,
corrections, explicit deferrals, or delegated node decisions recorded above.
No further product question is blocking synthesis. The user subsequently invoked
`/to-spec` on 2026-09-13, authorizing the specification phase. Supernode import and
the broader speculative roadmap remain outside this milestone's decision frontier.

## Spec synthesis: testing boundary check (2026-09-13)

The invoked `to-spec` skill requires checking testing seams with the user before
publishing the spec. The user accepted both proposed boundaries with `sure` after
the `/ask-matt` recommendation restated them and explicitly offered publication
upon confirmation. This answers the pending check within the already invoked
spec phase; it does not invoke a later phase.

- **[Modular] Accepted primary boundary:** public Modular operations clients use
  to edit, save, load and run graphs, with real Graph, Engine and node behavior.
  Check IDs, settings and stored content, deterministic output after Save/Open,
  wired Start/Stop and observable resource cleanup. Extend existing graph
  integration tests; add shared entry points only as the feature needs them.
  Avoid private-field assertions and mocks of internal calls.
- **[GUI app] Accepted additional boundary:** a small set of actual-app checks
  for node dragging, connection/deletion gestures, Edit/Run restrictions, one
  graph and Save/Discard/Cancel without autosave. Keep shared/node behavior in
  Modular tests. Automate rendered output; check Windows speaker playback
  separately through the actual device.

The published spec contains 42 user stories, architectural and node contracts,
these testing decisions and explicit scope limits. It supersedes the withdrawn
premature spec as the feature specification; existing code remains provisional.
No implementation tickets or source changes were made in this phase.

## Ticket planning (2026-09-13)

The user invoked `/to-tickets` in the same session to preserve the reasoning behind
the spec. The five-slice proposal was presented with sizes, deliverables and
blocking edges. The user asked whether speaker output already existed; the answer
identified the provisional Windows AudioOutputNode and confirmed its integration
in ticket 02. The user's subsequent `sure then` accepts this breakdown within the
already invoked ticket phase. It does not invoke implementation.

| Ticket | Blocked by | End-to-end result |
| --- | --- | --- |
| [01: Author and edit a flat graph](issues/01-author-and-edit-a-flat-graph.md) | None | Place, configure, wire and delete real primitive nodes in the Windows app |
| [02: Play MIDI through placed Start/Stop nodes](issues/02-play-midi-through-placed-start-stop-nodes.md) | 01 | Operate an explicitly wired MIDI/synthesis/speaker graph through node buttons |
| [03: Save and reopen an authored graph](issues/03-save-and-reopen-an-authored-graph.md) | 01 | Shared persistence for the initial palette, stable identities and unsaved-work choices |
| [04: Reopen MIDI graphs with their content intact](issues/04-reopen-midi-graphs-with-owned-content.md) | 02, 03 | Restore a playable graph with owned media independent of its import source |
| [05: Record WAV takes and reopen recording graphs](issues/05-record-wav-takes-and-reopen-recording-graphs.md) | 04 | Signal-controlled recording, error recovery, cleanup and full graph persistence |

After 01, tickets 02 and 03 are independently available. Ticket 04 joins those
branches; 05 builds the complete reopenable recording workflow on 04. Each ticket
contains its own acceptance criteria, scope and verification requirements for a
fresh implementation session. No ticket is implemented or claimed by publication,
and `ready-for-agent` does not bypass an unfinished blocker.

The existing Graph API supplies most structural editing. The MIDI parser and
Windows output are provisional prior art. Save/Open needs real reconstruction of
node and port identities, settings, layout and stored content. No standalone
prefactoring ticket is justified by this inspection: perform necessary small
prefactoring first within the relevant behavior slice while preserving existing
tests. Broader roadmap completion remains outside this milestone.

## Existing work to preserve

Identity ticket 01 is committed. Ticket 02 is implemented and tested in the working
tree; part of ticket 03 arrived with it. The remaining boundary-deletion rule matters
if the editor exposes edits inside supernodes. The source changes and Windows/Codex
setup remain uncommitted.

References: `.scratch/identity/issues/02-port-identity-and-port-mutation.md`,
`.scratch/identity/issues/03-supernode-face-by-id.md`, and `.scratch/roadmap/map.md`.

## Ticket 01 implementation (2026-09-14)

The user invoked `/implement` for ticket 01. The Windows SDK app now provides
the Edit-only authoring slice with exactly ConstFloatNode, GainNode and MixNode.
Shared Workspace layout/deletion and node-owned settings back the canvas.
Both native Debug and Release suites pass all seven CTest entries, including
the real-app gesture check and the existing regressions. Independent Standards and
Spec reviews are complete with no remaining findings; see [the record](01-review.md).
Build/launch and delegated node policies are recorded in
[the editor guide](../../docs/graph-editor.md).

Completion of ticket 01 makes 02 and 03 eligible; neither is automatically
invoked. This work remains uncommitted alongside the preserved earlier work.

## Ticket 03 implementation (2026-09-15)

The user invoked `/implement` for ticket 03 after ticket 02 was complete.
Shared primitive Save/Open, identity reconstruction and the app's unsaved-work
choices are implemented. Both native configurations pass all seven CTest entries;
independent Standards/Spec reviews have no remaining findings. See
[the ticket](issues/03-save-and-reopen-an-authored-graph.md) and
[review evidence](03-review.md). Ticket 04 is eligible for a fresh-session
invocation; ticket 05 remains blocked by 04. Nothing was committed.

## Ticket 04 implementation (2026-09-19)

The user invoked `/implement` for ticket 04 and continued after usage interruptions.
Playback nodes now round-trip their settings and owned MIDI without the import
source. Freshly reopened graphs are idle and produce identical MIDI/audio under
the same explicit inputs. A test-only owned PCM block also survives shared
Save/Open. Both native configurations pass all seven CTest entries, the separate
Windows device Start/Stop check passed, and independent Standards/Spec reviews
have zero remaining findings. See [the ticket](issues/04-reopen-midi-graphs-with-owned-content.md)
and [review evidence](04-review.md). Earlier uncommitted work is preserved;
nothing was committed. Ticket 05 is now eligible for invocation in a fresh session:
`/implement .scratch/graph-editor/issues/05-record-wav-takes-and-reopen-recording-graphs.md`.

### Ticket 05 completed (2026-09-20)

Wired WAV recording, numbered takes, Retry/Discard, ordinary cleanup and recorder
Save/Open are implemented. Both native configurations pass all seven CTest entries;
the separate Windows output check passes through fresh-process reopening and close
during capture. Independent Standards and Spec reviews have no remaining findings;
see [the review record](05-review.md). Earlier uncommitted work is preserved, and
nothing was committed. All five tickets in this feature are implemented; there is
no next `/implement` command in this ticket graph.
