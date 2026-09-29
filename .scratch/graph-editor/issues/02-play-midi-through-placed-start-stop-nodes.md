# 02: Play MIDI through placed Start/Stop nodes

**What to build:** The user loads MIDI into a node, wires it through synthesis to
Windows speakers, enters Run, and operates Start/Stop nodes they placed themselves.
The app stays responsive and shows the real nodes' capabilities and failures.

**Blocked by:** 01: Author and edit a flat graph.

**Status:** ready-for-agent

**Implementation:** complete (2026-09-14; uncommitted)

- [x] Extend the palette and controls with a MIDI-file node, placed manual Start
  and Stop nodes, SineSynthNode and AudioOutputNode, plus the existing allocator
  and mixer needed for explicit polyphonic wiring. Imported sequence content
  belongs to the MIDI node. No MIDI event editor is required.
- [x] A user-built MIDI-file -> sine synth -> audio output graph is audible on
  Windows' default speakers or headphones. Start/Stop trigger wires go to the
  MIDI node. AudioOutputNode consumes audio, with mono/stereo support as declared
  by its ports. There is no hidden graph or automatically inserted musical wiring.
- [x] Reuse/adapt the existing provisional MIDI parser and Windows output node
  where suitable. AudioOutputNode already provides buffered default-device
  playback; this ticket integrates that capability with the app. Its existing
  code and historical playback check do not establish completion of this slice.
- [x] MIDI import supports formats 0/1, PPQ timing, tempo changes and deterministic
  track ordering. Unsupported format 2/SMPTE input and failed imports produce
  useful node errors and an appropriate file-selection/repair path. Disclose
  skipped SysEx and downstream MIDI-message limitations.
- [x] Edit permits structural/configuration changes. Run compiles and processes
  the real graph; a compile or readiness failure is visible. With the current
  borrowed-node implementation, stop processing and release the Engine before
  edits, then construct a fresh Engine for the next run. Pausing alone does not
  authorize graph mutation.
- [x] Visual movement remains available during Run. Structural/configuration
  edits are unavailable then, while supported manual node buttons work through
  a safe runtime-input boundary. A button causes the node to emit a trigger into
  its output; the GUI does not call MIDI playback logic directly.
- [x] Run enables processing only. The MIDI node begins on its wired Start,
  releases previously held notes and restarts from the beginning on retrigger,
  and releases notes/cancels future events on Stop. Completion releases remaining
  notes and leaves it idle. Stop wins when both inputs receive a signal at the
  same frame. These contracts apply to the current MIDI nodes only.
- [x] Process on a worker so buffered device waits do not block GUI interaction.
  Device/processing failures are reported. Returning to Edit and normal close
  release execution/device resources without hangs or a hidden musical Stop;
  repeated Edit/Run use is supported without promising seamless state retention.
- [x] Controls and descriptions disclose the mono, monophonic, fixed-amplitude
  hard-on/off sine synth and its lack of velocity dynamics, envelopes, controllers,
  pitch bend, program changes and percussion. Polyphony comes from actual
  allocator-to-synths-to-mixer wiring; the allocator's note-number identity and
  non-note-message limitations are explicit.
- [x] Real-graph tests verify no unwired/manual-start playback, retrigger, Stop,
  completion, held-note cleanup and same-frame precedence through observable MIDI
  and rendered audio. Use non-block-aligned frames and multiple block sizes.
  Retain parser fixtures and block-size-invariance coverage.
- [x] Focused actual-app checks cover wiring, node buttons, mode restrictions,
  visible failures and responsiveness. Separately verify real Windows speaker
  playback and normal close during processing; automated rendering requires no
  audio device. Native Debug/Release suites and kernel purity pass.

**Context and scope:** This implements the playback portion of the Windows flat
graph editor spec, principally stories 10-23, 35 and playback resource cleanup.
Start/Stop and device behavior belong to the node library; generic lifecycle
mechanisms are shared Modular behavior; the app projects them. Run is not a
global musical transport. No recorder or Finished output is required here.

This ticket can start without 03. Persistence of these node kinds/content is
delivered by 04 after both 02 and 03. Preserve prior uncommitted work and existing
public behavior when prefactoring. Hot editing, richer instruments, professional
low-latency guarantees and additional device-routing/backends remain deferred.

## Implementation notes (2026-09-14)

Implementation was explicitly invoked for ticket 02. Playback implementation and
independent Standards/Spec review are complete, with zero remaining findings on
both axes. Nothing was committed. Tickets 03-05 remain uninvoked. See the
[review record](../02-review.md) for the findings, fixes and final evidence.

The nine palette entries are ConstFloatNode, GainNode, MixNode, Start, Stop,
MidiFileNode, SineSynthNode, VoiceAllocatorNode and AudioOutputNode. The app builds
only the user's placed/wired graph. `GraphExecution` provides shared worker
execution and publishes Edit eligibility only after Engine teardown. Generic
`Node::release()` and between-block errors keep teardown and failures reusable;
no media policy was added to the kernel. Windows device teardown runs on the
worker, including normal close and failed preparation.

Delegated node policies: manual presses coalesce until the next block; each new
Run drops pending presses and resets MIDI/synth/allocator execution state. Failed
MIDI replacement retains previous owned content; an empty node remains unready.
Held-note identity is channel plus note number with one release per identity.
Files complete at their EOT time; bare MidiBlockNode content completes at its
last entry (empty content immediately). Four kernel fixtures that formerly
relied on an abandoned note now give it an explicit distant note-off, preserving
their original sounding render horizons and graph-invariant assertions.
Allocator voices are editable from 1–32 under trailing-port RESTRICT; output
supports mono/stereo. The app processes at 48 kHz in 512-frame blocks. Descriptions
disclose the synth/allocator limitations and scroll to retain the full text.

Verification: full native Debug and Release CTest suites passed all seven entries:
41 kernel, 10 node, 7 authoring and 15 MIDI-file cases, actual-app checks, CLI smoke,
and kernel purity. TDD red/green cycles covered retrigger cleanup, Stop/completion
and exact rendered silence at block sizes 1/7/127/512, manual wiring, idle reruns,
failed-import repair, resource release (including failed prepare), worker lifecycle
and failure reporting, and the actual app. Voice configuration also has a public
identity/RESTRICT regression. Rendered samples are bit-identical across blocks.

The separate actual-app `--audio` check passed in Debug and Release: a quiet,
unmuted Windows default output was silent before Start, peaked around 0.5 after
the placed/wired Start, and returned below 0.001 after Stop. A second Run played
again and normal close during processing succeeded. The endpoint meter verifies
the default-device output stream, not acoustic sound pressure or human listening.
No device is required by the ordinary CTest suite. Rendering captures were inspected.
See [the app guide](../../../docs/graph-editor.md) for use and repeatable commands.

Review baseline: HEAD `4163dc85b8ef742ae93829af969301dbfd6a4b1b` plus the actual
starting tracked/untracked working tree copied to
`%TEMP%/modular-ticket02-baseline-20260914`. Review compares that snapshot with
the current files, preserving earlier uncommitted identity/node-I/O/ticket-01 work.

Next, in a fresh session:
`/implement .scratch/graph-editor/issues/03-save-and-reopen-an-authored-graph.md`
