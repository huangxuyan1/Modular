# Ticket 05 implementation review

Date: 2026-09-20.
Ticket: [Record WAV takes and reopen recording graphs](issues/05-record-wav-takes-and-reopen-recording-graphs.md)
Spec: [Windows flat graph editor](spec.md), limited to ticket 05.
Disposition: complete, uncommitted. All five graph-editor tickets are implemented.

## Review boundary

Starting and final HEAD: `4163dc85b8ef742ae93829af969301dbfd6a4b1b`.
No commits were made. The starting tracked and untracked files were copied to a
private temporary snapshot (`modular-ticket05-baseline-20260920`) before edits.
Independent Standards and Spec agents reviewed a nonempty comparison of the
ticket's before/after trees using:

```powershell
git -c core.autocrlf=false diff --no-index -- <private-review-snapshot>/before <private-review-snapshot>/after
```

The comparison includes newly added files and isolates this ticket from the
substantial earlier uncommitted work. Final local evidence is
`build/ticket05-review.diff`, including source, tests, docs and paired instruction
updates. The kernel and workspace source remain unchanged by this ticket.

## Standards

The initial review found one P3 heuristic: recorder controls depended on the
palette display name. `PrimitivePresentation::hasRecording` now identifies that
capability independently of the label. A separate read-only follow-up confirmed
resolution and found no new issue in the corrected inspector label or controls.

The final test-helper follow-up found no issues in native filename editing or
UTF-8 timeout diagnostics. It confirmed that the comment explains the reason
for driving the edit control's normal change notification.

Final Standards result: **0 remaining findings**.

## Spec

The initial independent review found no missing requirements, incorrect behavior
or unrequested scope. It checked sample boundaries and ties, MIDI independence,
numbered-file preservation, write recovery and cleanup, transient persistence,
and the GUI lifecycle. Final incremental review also found no issues in the
presentation capability, timing synchronization, chooser interaction or docs.

Final Spec result: **0 remaining findings**.

## Validation

The native Debug and Release commands are:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1 -Configuration Release
```

Both final commands passed all seven CTest entries: 41 kernel, 16 node, 21
authoring and 15 MIDI-file cases, actual-app checks, CLI smoke and kernel purity.
The existing graph/supernode equivalence and block-size invariants remain covered.

The node tests read emitted WAV headers and PCM, including distinct stereo
channels starting at frame 101 and stopping at 509, at block sizes 7, 64 and
331. They check duplicate triggers, simultaneous Stop precedence, independent
MIDI completion, repeated takes, existing files/directories, missing-directory
failure, repeated Retry, Discard, and normal teardown. An injected external WAV
writer writes a partial temporary file then throws, proving cleanup and retained
audio recovery without mocking graph execution.

Shared author/save/load/run tests preserve recorder settings, port IDs, NodeIds,
retired identity gaps and layout alongside source-independent owned MIDI. Saving
both active capture and failed-take states yields the same authored representation
as idle. Reopened graphs do not resume capture and can record new audio.

The actual app places/configures/wires the recorder, saves and reopens it, records
separate takes, and closes during capture. It covers Retry after directory repair,
Discard, unchanged dirty state for runtime controls, Save during failure, cancelled
New/Open/Close during capture, sharing-violation Save failures retaining capture,
cancelled Save As, and successful Save-and-Close aborting capture. Completed and
unrelated outputs survive. The normal app test requires no audio device.

Two test-harness issues were found and corrected without changing product policy:

- An automated Start followed immediately by Stop could land in one block,
  where Stop correctly wins. The test now observes Recording before sending Stop.
- Save As sometimes retained the native chooser's preselected filename after a
  caption update, opening an overwrite prompt instead of saving the new path.
  The helper edits the actual filename control through `EM_SETSEL` and
  `EM_REPLACESEL`, which delivers the normal change notification. It still waits
  for native-dialog completion and asserts the saved artifact and visible result.
  Temporary dialog-enumeration probes were removed; concise Unicode-safe timeout
  diagnostics remain.

The separate hardware command passed:

```powershell
& ./build/windows/Debug/graph_editor_ui_tests.exe ./build/windows/Debug/graph_editor.exe ./build/windows/graph-editor-audio-Debug.bmp --audio
```

Default-device peaks were **0.499969 after Start** and **0 after Stop**, both in
the original and reopened process. Second Run also reached 0.499969. Device
preparation took 84, 32 and 75 ms. The same run verified GUI responsiveness,
numbered WAV artifacts and normal close during capture. This measures the Windows
device stream, not acoustic sound pressure or a human listening test. Earlier
attempts stopped at the chooser before playback and were not counted as passes.

The actual-app capture was visually inspected. The recorder has visible real
ports, configuration, status and runtime controls; a crowded Channels label was
shortened to keep the filename field clear.

## Node policy and limits

Capture is held in memory; WAV writing runs on the processing worker and can
delay subsequent blocks while the GUI remains responsive. This is not a
continuous disk-streaming or low-latency guarantee. Completed output is 16-bit
PCM, with RIFF size validation and NaN-to-silence conversion. Temporary output
is exclusively owned and published without replacing existing paths. Normal
release aborts capture and discards retained failures; graph saving stores only
the recorder's authored settings. See [app usage](../../docs/graph-editor.md)
and [persistence](../../docs/graph-persistence.md).

## User-reported recording repair (2026-09-21)

The user's screenshot showed an output directory pasted with literal surrounding
quotes. Windows treated those quotes as path characters, so no WAV was published.
The filesystem exception used the Windows ANSI code page; displaying it as UTF-8
produced unreadable text. The inspector also appended excessive implementation
detail to every recorder status.

A real-graph regression reproduced the failed write with a quoted Windows path
containing spaces and Unicode. Construction and configuration now normalize that
pasted form, including settings restored from existing graphs. The test reads two
numbered WAV artifacts and checks their sample counts. A separate red/green check
reproduced the unreadable missing-directory status; failures now use concise text
derived from structured error codes, retaining Retry/Discard behavior. Recorder
help is two short sentences, omitted while displaying a failed-write status.

The actual-app tests apply quoted paths, check the normalized field, record after
reopening, and assert the exact readable error without appended help. All seven
CTest entries pass in Debug and Release: 41 kernel, 17 node, 21 authoring and 15
MIDI-file cases, actual app, CLI smoke and purity. Debug used the documented build
script. The running app locked the normal Release executable, so a fresh Release
build and full CTest run used `build/windows-recorder-fix/`; the user's process,
graph and retained take were left untouched. Tests used isolated temporary output
folders rather than the user's Downloads. Hardware playback was not repeated for
this path/status repair. The updated actual-app capture was visually inspected.

Independent read-only Standards and Spec reviews of `build/recorder-fix-review.diff`
both report **0 findings**. The comparison is against the pre-repair dirty tree
snapshot at `%TEMP%/modular-recorder-fix-baseline-20260921`, preserving earlier
uncommitted work. No commit was made.
