# Archived premature spec: Windows flat graph editor

Status: withdrawn

Archived during the workflow review at the user's request to correct the process.
The user had not invoked `/to-spec`. This is historical assistant-authored material,
not a spec to implement, a source of accepted decisions, or a ticket parent. Resume
grilling from [planning.md](../planning.md). The original draft body follows.

This draft records confirmed requirements and questions for the next grilling round.
It is not ready for implementation tickets. The existing MIDI-file and audio-output
code was written prematurely and remains provisional; its choices do not settle this
spec. The agreed workflow is grilling -> agreed spec -> tickets -> implementation.

## Problem

The next useful milestone is an application where the user can place nodes, wire
them, and hear and record the result. The existing roadmap contains questions whose
answers are not all prerequisites for that outcome.

## Confirmed requirements

- Prioritize Windows.
- The editor is an application above the kernel. Its graph view projects the
  authored graph; graph behavior belongs to the kernel and node library.
- Begin with flat graphs. Supernodes remain part of Modular, but are omitted from
  the first editor. Ordinary node/port identity and connection rules still apply.
- Let the user drag nodes onto a canvas and create connections between their ports.
- The user places Start and Stop nodes and connects their signals to the MIDI and
  recording nodes. Clicking a node's button acts through those actual connections.
- The recorder begins capturing incoming audio on Start and saves a WAV on Stop.
- Expose controls that correspond to actual node capabilities.
- Include speaker output and .mid loading if each is a contained node-library
  addition. These are independently usable outside the GUI.
- Preserve the earlier uncommitted identity work.

## Acceptance scenario to refine

1. Open the app and place Start, Stop, MIDI-file, synth, speaker-output, and recorder
   nodes. Additional existing nodes can provide gain, mixing, and voice allocation.
2. Select a MIDI file and configure the recorder's destination through node controls.
3. Wire Start and Stop to the appropriate inputs on MIDI and recorder nodes; wire
   MIDI to the synth and synth audio to both speaker output and recorder.
4. Click the placed Start node. The connected MIDI and recording nodes respond,
   producing audible audio and capturing it.
5. Click the placed Stop node. The connected nodes respond and a WAV is saved.
6. A disconnected control affects no other node. Invalid connections report the
   Graph's error and leave the existing graph intact.

The execution lifecycle, exact stop/restart rules, subsequent recordings, and project
persistence below must be settled before this becomes the acceptance test.

## Current factual constraints

- Engine currently borrows node objects from Graph. Its documented contract requires
  that Graph outlive Engine and remain unedited while Engine exists. The intended
  editable authored graph/compiled snapshot model is not fully implemented yet.
- TriggerAt is a scheduled source, not a clickable trigger source.
- The existing MIDI block has a trigger input and no Stop input. CaptureSink records
  continuously into memory and has neither recording controls nor WAV saving.
- The existing synth is a mono sine instrument. Polyphony can be composed from the
  existing voice allocator, several synths, and a mixer. It does not interpret
  General MIDI instrument programs or sustain controllers.
- The provisional Windows output provides buffered playback. It waits for device
  buffers; the application must process on a worker thread if it uses this adapter.
- No application project save/load currently exists.

See `docs/node-io.md` for provisional implementation details. Existing tests are
evidence about that code, not proof that this still-unagreed milestone is complete.

## Current decision frontier

These recommendations are proposals, not confirmed requirements.

### Q1: Editing and execution

How should wiring edits interact with a running graph?

Recommendation: explicit Edit and Run modes for the first app. Run enables graph
processing; the placed Start node still controls MIDI/recording through its wires.
Adding/removing nodes and connections happens in Edit mode; moving node cards and
using their buttons remains possible in Run mode. Seamless live recompilation stays
outside this first milestone. Leaving Run while recording needs a follow-up rule.

### Q2: MIDI stop and restart

What happens when Stop arrives during a held note, or Start arrives during playback?

Recommendation: Stop cancels future events and emits note-offs for active notes from
that node. Each Start begins from the file's beginning, releasing old held notes
before restarting if necessary. There is no resume position in this first version.

### Q3: Repeated recording takes

What should Start -> Stop -> Start -> Stop produce?

Recommendation: two WAV files, numbered under a configured destination. Start while
already recording leaves the current capture intact; Stop while idle does nothing.
The recorded interval includes Start's frame and excludes Stop's frame. Saving and
error/retry behavior needs a follow-up once the take policy is agreed.

### Q4: MIDI file completion

What happens to recording when the MIDI file reaches its end?

Recommendation: the MIDI node releases remaining held notes and becomes idle; the
recorder continues until its own Stop input receives a signal. There is no implicit
cross-node stop caused by file completion. A finished-event output can be considered
if automatic stopping is required for this milestone.

### Q5: Project persistence

Should the first version save and reopen the user's graph?

Recommendation: include a small application project file containing node settings,
wires, canvas positions, and MIDI-file references, so wiring work survives closing
the app. Keep its scope to the node types exposed by this editor. Missing-file and
compatibility behavior depends on whether this is included.

## Later decisions

After this round: shutdown during recording, file-save errors, the supported palette
and controls, and any persistence details required by Q5. UI framework selection and
backend implementation are engineering work against the agreed behavior.

## Relationship to older work

Identity ticket 01 is committed; 02 is implemented in the working tree, with some of
03 also present. Remaining supernode boundary deletion work is relevant when exposing
supernode authoring. The GUI milestone does not automatically adopt every open roadmap
ticket. Each prerequisite must be justified by an acceptance criterion above.
