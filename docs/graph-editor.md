# Windows graph editor

Tickets 01–05 provide one graph session with Edit/Run, Windows playback, wired
WAV recording and shared Save/Open with owned MIDI content. Undo/Redo remains deferred.

## Build and launch

From the repository root in PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1
& .\build\windows\Debug\graph_editor.exe
```

For Release, append `-Configuration Release` to the build command and launch
`build/windows/Release/graph_editor.exe`. The app uses Windows SDK controls and
GDI drawing. No additional GUI dependency or package installation is required.
See [windows.md](windows.md) for toolchain and editor setup.

## Use the editor

1. Click a palette entry to choose its creation settings. Drag that entry onto
   the canvas to place an instance. A drop outside the canvas places nothing.
2. Select a node to inspect its settings. Change the fields and choose **Apply
   settings**. Settings for palette entries apply to subsequent placements.
3. Click an output circle, then an input circle to connect them. Dragging between
   the circles also works. Input-first wiring is supported. Esc cancels a wire.
4. Right-click an input circle to disconnect its wire. Graph errors appear in
   the bottom status line. Connections require exact types and one wire per input;
   outputs can feed several inputs. Cycle rejection remains at Engine compilation,
   where the current Modular implementation enforces it; the app does
   not introduce a second cycle validator.
5. Drag a node's header or body to move it. Wires follow its actual port identities.
   Middle-drag or the mouse wheel pans the canvas, including large mixers.
6. Select a node and press Delete, or choose **Delete node and wires**. This
   explicitly removes its attached wires. Other nodes and wires survive.

The initial GUI accepts 1–64 channels and 1–32 mixer inputs to bound interactive
placement. Those are editor field limits, not new kernel limits. Port names and
types are read from the actual node declarations; audio types currently show the
channel count as `audio<N>`. There is no separate audio subtype field yet.

## Save and reopen

Use **File → Save** (Ctrl+S), **Save As**, **Open** (Ctrl+O) or **New** (Ctrl+N).
A `*` in the window title marks unsaved authored changes. Before New, Open or
Exit abandons those changes, choose **Save**, **Discard** or **Cancel**. Cancel,
a cancelled file chooser or a failed Save retains the graph and cancels the
transition. There is no autosave. Errors appear in the bottom status line.

Save preserves the supported primitives, their settings, owned MIDI, actual ports and
connections, identities, deleted-ID gaps and layout. Open restores one idle graph
in Edit. Save also works during Run; generated signals are excluded. Open/New/Exit
release an active execution worker before replacing or destroying the graph.

Start/Stop, MIDI blocks/files, synth, allocator and output nodes all support
Save/Open. A saved MIDI graph works after its original import source is moved
or removed. Prepared schedules, held notes, pending button presses, synth state
and device queues are excluded. Unconfigured MIDI nodes reopen with Load MIDI
available; output readiness is checked on Run. Unsupported kinds and versions,
malformed files and nested supernodes fail
explicitly. The editor has no presentation for a soundfont node, so a graph
holding one fails with `open: unsupported node kind modular.soundfont/1`.
Invalid Open retains the current graph. A saved inner graph uses the
ordinary format; importing it as a supernode is deferred.

See [graph-persistence.md](graph-persistence.md) for the shared client API,
versioned format, reconstruction contract and node-content extension.

## Settings and shared contracts

| Primitive | Creation / Edit settings | Actual signal interface |
| --- | --- | --- |
| ConstFloatNode | Finite constant value | `value` output, `value<float>` |
| GainNode | Channel count | `in` / `out`, `audio<N>`; `gain` input, `value<float>` |
| MixNode | Channel count and input count | `in0`… inputs and `out`, `audio<N>` |
| Start / Stop | Authored label on ManualTriggerNode | Manual button in Run; `trigger` output |
| MidiFileNode | Browse or enter path, then Load MIDI | `start` / `stop` triggers; `midi` output |
| MidiBlockNode (opened from another client) | Stored message count; no event editor | `start` / `stop` triggers; `midi` output |
| SineSynthNode | None | `midi` input, mono `out` |
| VoiceAllocatorNode | 1–32 voices | `midi` input, `voice0`… MIDI outputs |
| AudioOutputNode | 1 or 2 channels | Mono/stereo `in`, Windows default device |

The constant setter is authored configuration, not a thread-safe runtime input.
Gain comes from a wire; an unconnected gain input reads zero. The mixer sums its
inputs, with unconnected inputs contributing silence.

`ConstFloatNode::setConstant`, `GainNode::setChannels` and `MixNode::configure`
are node-owned authoring interfaces. Channel changes preserve existing NodeId
and PortId values and reject any change that would invalidate a wire. Mixer
growth appends fresh ports; shrinking removes trailing unwired inputs under
RESTRICT. Invalid settings and incompatible wiring leave the prior configuration
and graph intact. Release an Engine borrowing the graph before calling these APIs.

`Workspace` in `workspace/include/modular/Workspace.h` owns the graph and positions
associated by NodeId. `graph()` exposes the authoritative graph; `position()` and
`setPosition()` expose authored layout independently of execution. Graph moves
retain identity and layout. `deleteNodeWithConnections()` is the reusable explicit
delete operation. Primitive `Graph::removeNode` and `Graph::removePort` retain
RESTRICT. Deleted identities are never reused. No GUI library enters the kernel.

Shared persistence covers the palette and frame-based MIDI blocks authored by
other Modular clients. The presentation adapter lives in
`apps/graph_editor/PrimitivePalette.cpp`.

## Play a MIDI file

1. Place **Start**, **Stop**, **MidiFileNode**, **SineSynthNode**, and
   **AudioOutputNode**. Leave the output at one channel for the mono synth.
2. Select the MIDI node. **Browse MIDI...** selects and loads a file; alternatively,
   enter a path and press **Load MIDI**. The scrollable help panel shows readiness,
   message count, skipped SysEx count and import errors. Failed replacement keeps
   previous content; a new empty node stays unready until a successful import.
3. Wire Start `trigger` to MIDI `start`, Stop `trigger` to MIDI `stop`, MIDI `midi`
   to synth `midi`, and synth `out` to output `in`. No nodes or wires are inserted
   automatically. Formats 0/1, PPQ timing and tempo changes are supported; format 2
   and SMPTE timing are rejected. SysEx is skipped and disclosed.
4. Choose **Run**. This enables processing without starting the MIDI sequence.
   Select the placed Start node and press its **Start** inspector button. Select
   the placed Stop node and press **Stop** to end its connected MIDI playback.
   Buttons emit triggers at the next block; presses before that block coalesce.
5. **Return to Edit** releases the Engine and output device before edits become
   available. Structural edits and settings are disabled during Run; nodes still
   move and the canvas still pans. Compile/readiness/processing failures appear
   in the status line. Repair an unavailable Windows default device in Windows,
   then try Run again. Close also releases resources while keeping the UI responsive.

Start releases held notes and restarts at the beginning. Stop releases held notes
and cancels future events, winning over Start at a shared frame. MIDI completion
releases remaining notes and leaves the node idle. Held identities are channel
plus note number; repeated note-ons for an already held identity share its release.
The file's end-of-track time includes silent tails. A bare `MidiBlockNode` ends at
its last entry, including immediate completion of a block containing only a
frame-zero note-on. An empty block completes at Start with no output.

The sine synth is mono and monophonic, uses fixed amplitude and hard on/off, and
has no velocity dynamics, envelopes, controllers, pitch bend, program changes or
percussion. For polyphony, wire MIDI to a VoiceAllocatorNode, each allocator
output to a separate synth, and their audio outputs to a MixNode. The allocator
identifies notes by note number, ignores channel identity and drops non-note
messages; it chooses the lowest free voice, then steals the oldest. Its voice
count is editable under the same trailing-port RESTRICT rule as the mixer.

Shared `GraphExecution` owns the processing worker; the graph must outlive it.
Only `Idle` or `Failed` permits authored edits: both are published after Engine
destruction. `stop()` is nonblocking; the worker finishes bounded device waits
before releasing resources. `Node::release()` pairs with every attempted prepare,
including failures. Node error messages are read between blocks on the worker.
Teardown emits no musical Stop. Each new Run resets MIDI, synth, allocator and
manual-trigger execution state. Processing uses 48 kHz and 512-frame blocks;
buffered device playback has no low-latency or seamless-resume guarantee.

## Recording takes

Place **WavRecorderNode** and set its output directory, filename prefix and
channel count (1–64) in Edit. The directory must already exist. The default is
the app's working directory and prefix `take`. Relative directories remain
relative to the working directory, rather than the saved graph's location.
Windows paths pasted with surrounding quotes, such as Explorer's **Copy as path**,
are accepted; Apply removes the quotes from the directory field.
Wire audio to `in` and your chosen trigger sources to `start` and `stop`.
The same Start/Stop sources can fan out to MIDI and the recorder, or each node
can have separate controls. MIDI completion does not stop recording.

Start includes its sample; Stop excludes its sample and wins simultaneous ties.
Start during capture and Stop while idle are ignored. A completed take becomes
`prefix-1.wav`, then the next unused numbered path. Existing files, directories
and completed takes are preserved. Output is interleaved 16-bit PCM at the
graph's sample rate (48 kHz in this app); clipping uses full-scale saturation
and NaN becomes silence. The writer rejects sizes beyond standard RIFF limits.

The canvas displays Idle, Recording, Writing WAV or Retry / Discard. Select the
recorder for details. A failed write retains the completed take and blocks new
capture. Repair the external directory/permissions and choose **Retry**, or
choose **Discard** to release the take. These runtime controls do not dirty the
graph. A retry uses the next unused filename and preserves other outputs.

Capture uses memory; WAV writing runs on the processing worker and can delay
graph processing. The GUI remains responsive, but this is not a continuous
disk-streaming recorder or a low-latency recording guarantee. A temporary owned
subdirectory holds a pending WAV until publication; ordinary completion/failure
removes only that temporary output. Windows publication uses a rename that
refuses to replace existing paths.

**Return to Edit**, New/Open after an accepted transition, and Close abort
unfinished capture and release failed-take memory. They do not send Stop or
finish a take. Save/Open preserves recorder settings, IDs and layout together
with the rest of the graph, and reopens idle in Edit. Captures and failed takes
are transient. Save/Discard/Cancel still governs unsaved authored work separately:
Cancel or a failed Save leaves the current running capture intact.

## Verification

The normal build script runs the full CTest suite, including the
`desktop`-labelled app test, which drives the real editor on a private desktop
(see [windows.md](windows.md)). Focused authoring checks:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1 -Target authoring_tests -TestRegex '^authoring_tests$'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1 -Target graph_editor_ui_tests -TestRegex '^graph_editor_app$'
```

The integration tests use real Graph, Engine and node implementations to verify
identity, layout association, deletion, rejected edits, and rendered values and
samples after configuration changes. Existing graph/supernode, node, MIDI-file,
CLI and kernel-purity checks remain in the full suite.

The app test launches the real executable and drives ordinary Windows mouse and
control messages. It reads visible inspector, status and graph-count controls;
there is no test-only graph access. It checks palette placement, settings, valid
and rejected wires, movement, disconnection, deletion and cancellation. The test
runs the editor on a private Win32 desktop of its own, so its window and dialogs
never appear on the user's screen. It shows the window there without activating
it to capture real rendering, then hides it. Captures are
`build/windows/graph-editor-Debug.bmp` and `graph-editor-Release.bmp`. Windows
desktop access is required for this check.

The app test also covers Run/Edit, cycle and readiness failures, MIDI import
repair, manual node buttons, disabled edits, movement in Run, and normal close
during processing. Its ordinary CTest path places no output device. Playback
canvas captures have an additional `.playback.bmp` suffix.

Recorder tests inspect actual WAV headers, channel order and PCM at odd trigger
frames across multiple block sizes. They cover duplicate/tied triggers, MIDI
completion, failed/partial writes, Retry/Discard, preserved existing paths and
teardown. The app check reopens a recording graph, records numbered takes,
repairs a failed destination, and closes during capture. It exercises cancelled
and failed New/Open/Close decisions while capture remains active, then saves
and closes without completing that take.

Run the separate hardware check on a quiet, unmuted default output device:

```powershell
& ./build/windows/Debug/graph_editor_ui_tests.exe ./build/windows/Debug/graph_editor.exe ./build/windows/graph-editor-audio-Debug.bmp --audio
```

This places and wires AudioOutputNode through the actual app, saves the playable
graph, removes its copied import file and reopens the graph. It measures silence
before Start, output after Start and silence after Stop, repeats Run, closes
during playback, then repeats the owned-content check in a fresh app process.
It does not change volume or mute settings. The
[Windows endpoint meter](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nn-endpointvolume-iaudiometerinformation)
measures the stream sent to the default device before endpoint attenuation;
this verifies the device path, not acoustic sound pressure or a human listening test.
The hardware path allows ten seconds for device activation/teardown and five
seconds to detect a tone, while continuing to check GUI responsiveness. Its
copied MIDI fixture holds a note for over seventeen seconds so Stop is verified
before natural completion. Ordinary device-free app checks keep their shorter
deadlines. This accommodates cold-device startup; it is no latency guarantee.

For a manual demonstration, place all three nodes, wire constant `value` to gain
`gain`, wire gain `out` to mixer `in0`, and try constant `value` to gain `in`.
The last wire must report a type mismatch. Edit the constant, grow the mixer,
try changing the wired gain's channels, move the gain, right-click its `gain`
input, then delete the still-wired gain. The constant and mixer must survive.

The app follows Microsoft's documented [mouse capture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture)
and [window rendering](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-print)
boundaries for gestures and visual inspection.
