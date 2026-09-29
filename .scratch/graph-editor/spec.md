# Windows flat graph editor with audio and shared graph persistence

Status: ready-for-agent
Date: 2026-09-13

## Problem Statement

Modular can already run connected primitive nodes and supernodes, but authoring a
useful graph still requires C++ or fixed CLI examples. A returning user cannot
place nodes, connect them, operate their supported controls, hear the graph, or
save an authored graph for a later session through a simple app.

The existing render examples write WAV files from application code after a fixed
run. That does not provide the requested node-controlled workflow: a user places
Start and Stop nodes, connects them to MIDI playback and recording, and controls
those behaviors through the graph.

The next useful milestone is a Windows graph editor that produces audio and
preserves authored work. Completing speculative roadmap items first would delay
that feedback.

## Solution

Build a Windows-first GUI app that projects Modular's authored graph. A palette
and canvas let the user place primitive nodes, move them, inspect typed ports,
connect or disconnect them, configure their supported settings, and delete them.

Use separate Edit and Run modes initially. Run enables processing; musical Start
and Stop remain explicit nodes and wires. Include a contained MIDI-file node,
Windows audio-output node, and WAV recorder so a user can build a graph that
loads MIDI, synthesizes audio, plays it through speakers, and records selected
takes.

Provide shared Modular operations for editing and graph persistence. Save and
Open preserve graph structure, identities, settings, stored node content, and
layout. Reopening restores an idle authored graph, without resuming generated
signals or in-flight processing. The GUI initially holds one graph and prompts
Save / Discard / Cancel before abandoning unsaved work. It never autosaves.

Keep this milestone flat-first. Preserve the existing graph and supernode model
so an ordinary saved graph can be reused as a supernode in a later feature.

## User Stories

1. As a Windows user, I want to launch a graph editor, so that I can build and run a graph without writing C++.
2. As a graph author, I want to drag primitive nodes from a palette onto a canvas, so that I can choose the graph's structure myself.
3. As a graph author, I want to move nodes on the canvas, so that I can make my graph readable.
4. As a graph author, I want visible port directions and types, so that I can understand what each connection carries.
5. As a graph author, I want to connect compatible ports, so that my wiring determines how signals flow.
6. As a graph author, I want invalid connections to explain the graph rule they violate, so that I can correct the graph.
7. As a graph author, I want to remove a connection, so that I can change signal routing.
8. As a graph author, I want explicit deletion of a selected node and its attached connections, so that I can remove it in one deliberate operation.
9. As a Modular client author, I want that deletion behavior exposed by shared Modular facilities, so that clients apply the same editing contract.
10. As a graph author, I want controls for each node's actual supported settings, so that I can configure it without assuming capabilities it lacks.
11. As a graph author, I want node readiness and errors visible in the app, so that I can understand why a node cannot operate.
12. As a graph author, I want Edit mode for structural and configuration changes, so that the first editor respects the current execution lifecycle.
13. As a graph author, I want Run mode to process my graph, so that its connected nodes can produce signals.
14. As a graph author, I want to rearrange the visual layout during Run, so that I can inspect a running graph without changing its topology.
15. As a graph author, I want to place and wire my own Start and Stop nodes, so that I control which nodes receive those signals.
16. As a graph author, I want the placed Start and Stop buttons to work during Run, so that I can operate the graph through supported node inputs.
17. As a MIDI user, I want a node that loads a supported MIDI file, so that I can use an existing sequence as stored node content.
18. As a MIDI user, I want Start to restart the current MIDI node from the beginning and release prior held notes, so that retriggering does not leave abandoned notes sounding.
19. As a MIDI user, I want Stop to release held notes and cancel future MIDI events, so that the current playback ends.
20. As a MIDI user, I want the MIDI node to release remaining notes and become idle at completion, so that a finished sequence does not leave notes held.
21. As a graph author, I want the app to describe the current synthesizer and allocator limits, so that I understand the sound my MIDI graph can produce.
22. As a graph author, I want to wire audio into a Windows output node, so that I can hear the graph through the default device.
23. As a graph author, I want processing and device waits to leave the GUI responsive, so that I can still operate node controls and exit.
24. As a recording user, I want a wired Start signal to begin WAV capture and a wired Stop signal to finish and save it, so that recording follows my graph.
25. As a recording user, I want the recorder to remain independent of MIDI completion, so that my chosen wiring determines the take's end.
26. As a recording user, I want each completed take in a separate numbered WAV, so that a later take preserves earlier recordings.
27. As a recording user, I want repeated Start while recording and Stop while idle to be harmless, so that duplicate signals do not disrupt a take.
28. As a recording user, I want a failed completed take available for Retry or Discard, so that I can recover from a write error before starting another take.
29. As a graph author, I want to save graph structure, settings and layout, so that I can continue the same authored work later.
30. As a graph author, I want stored MIDI and audio blocks saved with their nodes, so that reopening does not lose content the graph owns.
31. As a graph author, I want saved node-owned content restored without requiring its original import file, so that moving that file does not break an otherwise self-contained graph.
32. As a Modular client author, I want graph Save and Open available outside the GUI, so that other clients can persist the same authored graph.
33. As a Modular client author, I want node and port identities and ID allocation history preserved, so that loading cannot retarget connections or reuse deleted identities.
34. As a graph author, I want Open to restore an idle graph without generated events, sounding notes or live samples, so that saving does not become a running-session checkpoint.
35. As a graph author, I want a node to explain and support repairing any genuine unresolved external dependency, so that I can make it ready again.
36. As a graph author, I want one open graph at a time initially, so that the first editor has a simple session model.
37. As a graph author, I want Save / Discard / Cancel before New, Open or Close abandons unsaved work, so that I choose what happens to my edits.
38. As a graph author, I want Cancel or a failed or cancelled Save to retain the current graph, so that a failed transition does not lose my work.
39. As a graph author, I want saving to happen only when I explicitly choose it, so that the app never autosaves over my intended saved state.
40. As a graph author, I want an invalid or unsupported graph load to report the problem without silently dropping graph content, so that I can trust the result.
41. As an app user, I want normal shutdown to release node resources and remove incomplete temporary recordings, so that exiting does not require manually stopping every node.
42. As a future supernode author, I want ordinary saved graphs to remain suitable for later reuse inside another graph, so that flat-first delivery does not impose a second supernode persistence model.

## Implementation Decisions

- **Ownership:** The GUI is an application above Modular. The kernel remains
  framework-independent and media-neutral. Graph editing, persistence and generic
  execution lifecycle mechanisms are reusable Modular behavior. MIDI playback,
  recording and device I/O belong to node implementations; presentation and
  unsaved-work prompts belong to the app.
- **Shared facilities:** Extend the existing public Graph and Engine behavior and
  introduce only the shared editing/persistence facilities needed for this
  feature, consistent with the provisional Workspace direction. The GUI is a
  client of those facilities. A complete command, constraint, annotation or
  plugin system is not an automatic prerequisite.
- **Graph authority:** Nodes, ports and connections in the authored graph remain
  authoritative. Views derive from them. Persist layout as authored metadata
  through shared persistence without making coordinates part of kernel processing.
  Use stable IDs to associate presentation with graph objects.
- **Connection rules:** Reflect current public Modular validation, including
  compatible port types, one connection per input, free fan-out and the current
  cycle restriction. Do not create a second GUI validator with different rules.
  Current audio port typing uses channel count in the subtype field; the separate
  opaque audio subtype/channel design is agreed but not yet implemented. This
  feature must not advertise it as available or automatically require that
  broader migration.
- **Deletion:** Keep primitive removal RESTRICT: a still-connected node or port
  cannot be silently removed. Expose an explicit shared delete-with-connections
  operation that removes the relevant wires and selected node. The GUI invokes
  that operation. Media-specific cascades remain application policy.
- **Edit/Run lifecycle:** Structural changes and authored node configuration edits
  are available in Edit. Compile and process in Run. With the current borrowing
  implementation, release the Engine before editing the graph and construct a
  new Engine for the next run; pausing processing alone is insufficient. Visual
  movement may remain available in either mode. This initial restriction does
  not change the longer-term editable authored-graph design.
- **Run versus musical controls:** Run starts graph processing, not a hidden
  musical transport. The user places Start and Stop nodes and connects their
  trigger outputs to the intended inputs. Manual controls enter the node's
  supported runtime boundary safely while the processing worker runs. Runtime
  node controls must not mutate topology or constructor-only configuration.
  Leaving Run or shutting down releases execution resources; it does not inject
  a musical Stop into the graph or automatically finish a recorder take.
- **Honest node presentation:** Show the primitive nodes supported by this
  milestone with their actual ports, configuration and applicable runtime
  controls. Add the missing manual trigger, MIDI Stop and signal-controlled
  recording behavior in the node library. Do not simulate it solely in GUI
  callbacks. Constructor-only settings can be edited through the supported
  authored-configuration path in Edit; they are not advertised as live inputs.
- **MIDI-file node:** Integrate the contained existing parser direction for
  Standard MIDI File formats 0 and 1 with PPQ timing and tempo changes. Keep
  deterministic track ordering and sample-frame scheduling. Report unsupported
  format 2 and SMPTE division. Be explicit that SysEx is skipped and that
  downstream nodes may implement only a subset of the loaded channel messages.
  No piano roll, MIDI event editor or General MIDI instrument is required.
- **Current MIDI playback contract:** Start releases notes still held by prior
  playback, cancels that playback and restarts the stored sequence at the
  beginning. Stop releases held notes and cancels remaining events. Completion
  releases remaining notes and leaves the node idle. Stop wins if Start and Stop
  arrive at the same frame. These rules belong to the current MIDI nodes, not
  the kernel or every future node. No Finished output is required.
- **Synthesis:** Use existing synthesis and explicit graph wiring. The current
  sine synth is mono and monophonic, with fixed amplitude and hard note on/off;
  it does not implement velocity dynamics, envelopes, controllers, pitch bend,
  program changes or percussion. Polyphony is allocator-to-multiple-synths-to-mixer
  wiring. The current allocator identifies notes by note number, not MIDI
  channel, and does not forward general non-note messages. Present these limits
  instead of expanding the instrument scope.
- **Windows audio output:** Include mono/stereo default-device playback as a
  node-library capability. The provisional Windows SDK adapter is prior art;
  its backend is an implementation choice within that capability. Audio reaches
  it through audio ports, without a new universal transport gate. Run graph
  processing off the GUI thread because the existing bounded device queue can
  wait. Surface device failures and release device resources on normal teardown.
- **WAV recorder:** An accepted Start begins collecting incoming audio; Stop
  completes the take and saves it to a separate numbered WAV without overwriting
  earlier completed takes. Start while recording and Stop while idle are
  ignored. Capture is Start-inclusive and Stop-exclusive; Stop wins at a shared
  frame. MIDI completion does not stop the recorder. These are contracts of this
  recorder, not general graph timing or recording laws.
- **Recorder errors and disposal:** Retain a completed take in memory after a
  write failure and expose Retry/Discard. Refuse another take until that failure
  is resolved. On normal disposal, abort an unfinished take, release retained
  memory and owned handles, and remove the node's own incomplete temporary
  files. Preserve completed recordings and unrelated files. Neither an unfinished
  capture nor a retained failed take is a persisted audio block under this node's
  policy. This is not crash recovery, and exit does not require a wired Stop.
- **Saved content:** Save authored nodes, port identities, connections, settings,
  layout metadata and stored node content. Persist a node-owned MIDI or audio
  block itself, rather than substituting only its original filename. Exclude
  generated events, active notes, current audio buffers, device queues and other
  in-flight processing. Loading creates an idle graph in Edit; it does not resume
  a prior playback or recording.
- **Reconstruction contract:** Shared persistence must reconstruct supported node
  kinds, their authored configuration, declared ports and stored content without
  interpreting media in the kernel. Provide node-level persistence support where
  it is missing; the GUI must not copy private node internals. Preserve NodeId,
  PortId and the graph's IdSource high-water mark, including gaps left by
  deletions, as required by the identity ruling. Replaying surviving constructors
  with freshly minted IDs is not equivalent.
- **Format and failures:** Define an identifiable, versioned graph representation
  for the supported content. Exact encoding, serialization library and factory
  API are implementation choices within the reconstruction contract. Reject
  corrupt or unsupported content with a useful error rather than silently
  flattening, omitting nodes or changing wiring. A failed Open retains the
  current authored graph; a failed Save does not mark it clean.
- **External dependencies:** Restore owned content independently of its import
  source. A node that genuinely depends on something external reports unresolved
  readiness and provides a repair path; the GUI reflects that state. A node
  remains not ready until its required dependency is repaired. The kernel does
  not acquire MIDI-file or device-selection policy.
- **Supernode compatibility:** A supernode's implementation is another graph;
  save that graph using ordinary graph persistence for future reuse. The first
  GUI authors primitive graphs. Parent graphs containing supernodes, importing
  saved graphs as supernodes and nested packaging are deferred. Preserve existing
  by-value supernode ownership and runtime behavior. Do not introduce a second
  supernode save mechanism or flatten authored structure to make saving easier.
  Unsupported nested content must be explicit, not silently lost.
- **Session and dirty state:** The GUI opens one graph at a time. Authored
  topology, settings, stored-content and layout changes make the graph unsaved;
  generated signals and live node activity do not. Before New, Open or Close
  abandons unsaved work, offer Save / Discard / Cancel. Save proceeds only after
  explicit choice; Discard continues without saving; Cancel cancels the
  transition. Failed or cancelled Save retains the graph and cancels the
  transition. Never autosave. Recording cleanup is separate from this prompt.
- **History priority:** Undo/Redo belongs to shared Modular behavior, with any
  GUI controls projecting that capability. It is lower priority and does not
  block the initial authoring/audio/persistence milestone. Do not add GUI-only
  history or require the entire old command roadmap before this feature.

## Testing Decisions

The user accepted these testing boundaries on 2026-09-13:

- **Primary boundary:** Exercise public Modular operations used by clients to
  author, save, load and run graphs, with real Graph, Engine and node
  implementations. Prefer the existing public graph integration style; add
  shared entry points only as needed for the feature. Check externally observable
  graph state, events, rendered samples, errors and output artifacts rather than
  private fields or internal call sequences.
- **Round-trip behavior:** Author a graph, save it, load it through the shared
  facility and compare topology, settings, IDs, layout and stored content.
  Include deleted-ID gaps and subsequently create new objects to prove IDs are
  not reused. Test reconstruction of ports retained privately by node types
  through their public behavior, not private-member inspection.
- **Content versus execution:** Prove stored MIDI content survives loss of the
  original import file. Exercise the generic persistence contract with a node
  that owns audio block content without requiring a new production audio-block
  player. Prove generated events, active notes, device buffers and unfinished
  recorder state are not resumed after Open.
- **Determinism:** Under the same authored content, explicit inputs, sample rate
  and frame horizon, compare a fresh graph's render with a freshly loaded graph.
  Preserve block-size invariance using odd offsets and multiple block sizes.
  Rebuild graphs for independent runs because nodes are stateful.
- **Wired node behavior:** Drive actual trigger ports at known frames. Cover
  retrigger, Stop, MIDI completion, held-note release, same-frame Stop precedence,
  recorder sample boundaries and recorder independence from MIDI EOF. Check
  emitted MIDI and WAV PCM samples/frame counts, not only successful execution.
- **Failures and cleanup:** Cover unsupported/corrupt graph input, invalid
  connections, unresolved genuine dependencies, write failure with Retry/Discard,
  duplicate triggers, and normal disposal during capture. Assert observable
  readiness, retained or discarded takes, preserved completed files and cleanup
  of owned temporary artifacts. Substitute controllable external file/device
  boundaries only where necessary; do not mock Graph/Engine internals.
- **Existing prior art:** Extend the real-graph doctest tests for timing,
  connection rules, deletion RESTRICT, identity stability and sample equality;
  the node tests for event logs, mixing and allocation; and MIDI byte fixtures
  for parser and scheduling behavior. Reuse the CLI tests' temporary output,
  Unicode-path and existing-file-preservation approach.
- **GUI boundary:** Add a small set of actual-app checks for dragging nodes,
  connection and deletion gestures, supported controls, Edit/Run restrictions,
  one open graph and the full Save/Discard/Cancel transitions, including failed
  or cancelled Save and no autosave. Keep shared graph and node semantics in
  Modular integration coverage instead of duplicating them throughout GUI tests.
- **Windows verification:** Automate deterministic rendering without requiring a
  physical output device. Separately verify real Windows speaker playback,
  responsiveness and normal close while processing. Retain the existing
  supernode equivalence regressions and kernel-purity check. Run the complete
  native Debug and Release CTest suites after implementation.

## Out of Scope

- Hot structural/configuration editing, seamless Engine replacement, preserved
  runtime state across recompile, and running-session checkpoints.
- Supernode canvas authoring, saved-graph import as a supernode, nested-parent
  serialization/packaging, live source references and propagation semantics.
- Multiple graph tabs or simultaneous graph sessions.
- A GUI-owned musical transport, automatic insertion or wiring of Start/Stop,
  or automatic recorder completion at MIDI EOF or app exit.
- Piano-roll or waveform editing, a new audio-block player, arrangement/track
  views, richer synthesizers, General MIDI playback and plugin hosting.
- Expanded audio-device routing/backends, non-Windows GUI delivery and a
  professional low-latency audio guarantee for the initial buffered adapter.
- Autosave, crash recovery and permanent storage of unfinished recorder captures.
- Completing Undo/Redo as a prerequisite, or implementing the broader command,
  constraint, annotation, subtype-registry, latency-compensation or cycle-breaking
  roadmaps beyond a concrete need in this milestone.

## Further Notes

- This spec synthesizes the completed grilling decisions and explicitly delegated
  node policies. It is not evidence that the GUI, persistence, manual controls or
  signal-controlled recorder already exist.
- Existing MIDI-file and Windows-output additions are provisional, uncommitted
  prior art. Their tests establish current behavior, not acceptance of every
  implementation choice. Preserve earlier uncommitted identity work and adapt
  useful existing code only when implementation is invoked.
- Previously recorded Windows checks passed 40 kernel, 7 node and 11 MIDI-file
  test cases, plus CLI smoke and kernel purity. Those results do not verify this
  new feature. No application test run is claimed for writing this specification.
- Specific GUI toolkit, persistent encoding and internal interface choices should
  be resolved within these contracts during ticket planning or implementation.
  They must not silently add product scope or move shared behavior into the GUI.
- Implementation tickets and coding are separate user-invoked phases. Publishing
  this specification does not invoke either phase.
