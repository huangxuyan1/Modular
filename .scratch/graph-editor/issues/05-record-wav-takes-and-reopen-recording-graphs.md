# 05: Record WAV takes and reopen recording graphs

**What to build:** The user adds a WAV recorder to the playable graph, connects
audio plus their own Start/Stop signals, and records separate takes. Failures and
normal exit have explicit node behavior. Saving and reopening the complete graph
preserves recorder configuration and owned MIDI content for another session.

**Blocked by:** 04: Reopen MIDI graphs with their content intact.

**Status:** done

- [x] A recorder node appears with real audio, Start and Stop inputs and suitable
  configuration/status controls. The user chooses and wires it. An accepted
  Start begins collecting incoming audio; Stop completes the take and saves
  it as WAV. The node owns recording and writing behavior, not GUI callbacks.
- [x] Start while recording and Stop while idle are ignored. Capture includes
  the Start sample and excludes the Stop sample. Stop wins when both signals
  arrive at one frame. These policies belong to this recorder, not the kernel.
- [x] Each completed take is a separate numbered WAV. New takes and retries
  preserve earlier completed outputs and unrelated files. Expose the node's
  output location/naming configuration appropriately.
- [x] MIDI completion releases MIDI-held notes but does not finish recording.
  The recorder continues until its own wired Stop. The GUI does not inject
  signals, add a hidden transport or require a MIDI Finished output.
- [x] After a WAV write failure, retain the completed take in memory and expose
  Retry/Discard. Refuse a new take until resolved. Retry can save the retained
  take; Discard releases it. Errors are visible without freezing the GUI.
- [x] Normal disposal aborts unfinished capture, releases retained/failed-take
  memory and owned handles, and removes the node's own incomplete temporary
  files. Preserve completed recordings and unrelated files. Closing or returning
  to Edit does not silently finish a take or require a wired Stop first.
- [x] Save/Open supports the entire playback-and-recording graph, including the
  recorder's authored settings, stable identities and layout. Node-owned MIDI
  content still survives without its import source.
- [x] Recorder capture buffers and retained failed takes are transient under
  this node's policy and are not restored by graph Save/Open. Reopening is idle
  in Edit. This does not exclude persistent audio blocks owned by other nodes,
  whose shared persistence contract was verified in 04.
- [x] New/Open/Close with unsaved authored work keeps the Save/Discard/Cancel
  behavior from 03, including failed/cancelled Save and no autosave. A dirty
  graph can be explicitly saved and then closed during capture; a clean graph
  can close directly. In both cases node cleanup is independent of the
  authored-work decision and does not auto-complete the recording.
- [x] Deterministic real-graph tests drive trigger ports at non-block-aligned
  frames and compare WAV PCM/frame counts, duplicate-trigger behavior,
  simultaneous inputs and recorder independence from MIDI EOF across block
  sizes. Test actual emitted artifacts rather than internal write-call counts.
- [x] Exercise write failure, Retry, Discard, refusal of a new take while
  unresolved, repeated numbered takes and normal teardown during capture.
  Use isolated temporary files and controllable external failure boundaries
  where needed. Verify incomplete owned artifacts are cleaned while completed
  recordings and unrelated files survive.
- [x] Verify the full public author/save/load/run path and a focused actual-app
  smoke path: reopen the graph, Start playback/recording through wired controls,
  Stop and inspect the WAV, repeat a take, and close during active capture.
  Include save/load after transient recorder states to prove they do not resume.
- [x] Native Debug and Release suites, kernel purity and existing graph/supernode
  invariants pass. Separately verify Windows speaker output, GUI responsiveness
  and normal close; automated render/recorder tests require no audio hardware.

**Context and scope:** This completes the recording portion of the Windows flat
graph editor spec, principally stories 24-28 and recording-related 29, 34 and
37-41. Ticket 04 transitively supplies the authoring app, working manual MIDI
controls, Windows output and content-preserving persistence. It is a blocker
because this slice delivers a fully reopenable recording graph.

CaptureSinkNode currently collects all processed audio, while the CLIs write WAV
outside the graph after fixed runs. Neither already satisfies the recorder
contract. Adapt useful code while preserving earlier uncommitted work and
existing public behavior; do necessary prefactoring first within this slice.

The user delegated node-specific policy choices recorded above. No additional
node-policy interview is needed. Material choices that would change shared
Modular behavior or GUI scope are outside that delegation. Undo/Redo, hot
editing, supernode import/nested persistence, richer synthesis, autosave and
crash recovery remain deferred.

## Comments

### Implementation complete (2026-09-20)

`WavRecorderNode` owns capture, 16-bit PCM writing, collision-safe numbered output,
write-failure retention, Retry/Discard and cleanup. Its three real inputs follow
the timing and duplicate-trigger policies above. The palette exposes output
directory, prefix, channels, recording state and recovery controls. The shared
recorder codec preserves authored settings and identities while excluding all
capture/recovery state. MIDI content remains self-contained across reopening.

Final native Debug and Release each pass all seven CTest entries: 41 kernel,
16 node, 21 authoring and 15 MIDI-file cases, actual app, CLI smoke and purity.
Tests inspect WAV headers, PCM and preserved files across block sizes, partial
write failure, recovery, transient-state saves and normal teardown. The actual
app exercises recording after reopen, repeated takes, directory repair, and
cancelled/failed Save decisions during capture before successful Save-and-Close.

The separate Windows hardware path passes: peak 0.499969 after wired Start and
zero after wired Stop, repeated Run and fresh-process reopening, responsive GUI
and normal close during capture. This measures the device stream, not a human
listening test. Independent Standards and Spec reviews have no remaining findings.
See [the complete review and evidence](../05-review.md).

Capture stays in memory and WAV writing runs on the processing worker; writing
can delay subsequent graph blocks while the GUI remains responsive. The output
directory must exist. See [recording usage](../../../docs/graph-editor.md) and
[recorder persistence](../../../docs/graph-persistence.md).

Earlier uncommitted work is preserved; nothing was committed. All five tickets
in this feature are implemented, so there is no next `/implement` command here.

### User-reported recording repair (2026-09-21)

Quoted Windows output paths now save correctly, failed writes show readable short
errors, and recorder help is two sentences. Real-graph and actual-app regressions
cover the reported symptoms. All seven Debug and Release CTest entries pass,
including 17 node cases; independent Standards and Spec reviews have no findings.
The updated Release app is at `build/windows-recorder-fix/Release/graph_editor.exe`
because the user's running app locks the normal Release executable. See the
[repair evidence](../05-review.md#user-reported-recording-repair-2026-09-21).
