# Ticket 04 implementation review

Date: 2026-09-19 (implementation and validation continued from September 15).
Ticket: [Reopen MIDI graphs with their content intact](issues/04-reopen-midi-graphs-with-owned-content.md)
Spec: [Windows flat graph editor](spec.md), limited to ticket 04.
Disposition: complete, uncommitted. Ticket 05 awaits invocation.

## Review boundary

Starting and final HEAD: `4163dc85b8ef742ae93829af969301dbfd6a4b1b`.
Commit list `git log 4163dc85b8ef742ae93829af969301dbfd6a4b1b..HEAD --oneline` is empty.

The starting tracked and untracked source was copied into a private temporary
snapshot (`modular-ticket04-baseline-20260915`) before implementation. Independent Standards and Spec agents reviewed the ticket delta
in `build/ticket04-review.diff`, then checked the live incremental changes.
The final artifact includes the actual uncommitted source, tests, docs and paired
instruction-file updates. It was generated per changed non-ignored file using:

```powershell
git -c core.autocrlf=false diff --no-index -- <baseline>/<path> <path>
```

This isolates ticket 04 from prior identity and tickets 01-03 work. A HEAD-only
diff would omit newly created files and conflate earlier uncommitted changes.
Completion records report the review rather than expanding its implementation
scope. The unrelated ignored `apps/.DS_Store` file is excluded.

## Standards

Initial independent review identified two minor documented convention issues:
uppercase MIDI in authoring error messages and a test constant named `digits`.
It also identified a possible Duplicated Code smell in canonical channel-message
validation. The error messages are lowercase, the constant is `kDigits`, and
both reconstruction paths use `MidiMsg::isValidChannelMessage()`.

Final incremental review found one further minor constant naming issue in the
hardware fixture. It is now `kHeldDelta`; the reviewer confirmed resolution.
No other actionable findings remained. The reviewer confirmed that chooser
synchronization waits for native dialog destruction and reports timeout, source
locations identify calling assertions, and hardware timing changes remain in tests.
AGENTS.md and CLAUDE.md are identical.

Final Standards result: **0 remaining findings**.

## Spec

Initial independent review found no missing or partial requirements, scope creep,
or incorrect implementation within ticket 04.

The final incremental review also found no issues. Chooser synchronization retains
outcome assertions. Hardware waits retain responsiveness, successful execution and
normal-exit checks. Early tone detection retains its amplitude threshold, and the
extended copied fixture ensures Stop is checked before natural completion.
Fresh-process reopening still verifies source independence, idle playback, Start
and Stop. No behavioral assertions were weakened.

Final Spec result: **0 remaining findings**.

## Final validation

Both standard native commands passed on September 19 after the final source changes:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1 -Configuration Release
```

Each passed all seven CTest entries: 41 kernel, 11 node, 20 authoring and 15
MIDI-file cases, actual-app checks, CLI smoke and kernel purity. Existing
supernode equivalence and block-size invariance remain covered.

Shared tests use real graphs and fresh Engines to compare emitted MIDI and audio
at block sizes 17, 127 and 512. Coverage includes source-file removal, sample-rate
preparation, tempo changes, simultaneous/channel message order, duration, skipped
SysEx count, surviving identity gaps and later minting, malformed-content
rejection, import repair, idle reopen and saves during active/pending control
execution. A test-only PCM node proves opaque owned-audio persistence without a
production audio-block player. The shared format and kernel/workspace source are
unchanged by ticket 04.

Actual-app coverage includes native file dialogs, cancelled Save, a real Windows
sharing-violation Save failure, malformed owned-content Open retaining the active
graph, dirty state, source removal, Save during Run and fresh-process reopening.
A MIDI block created by another client is displayed and saved. An intermittent
native-dialog timing failure was fixed by waiting for file selection to finish
before interacting with the owner. Final Debug and Release app checks passed.
The playback canvas capture was visually inspected.

### Separate Windows device check

The actual-device path passed on September 16:

```powershell
& ./build/windows/Debug/graph_editor_ui_tests.exe ./build/windows/Debug/graph_editor.exe ./build/windows/graph-editor-audio-Debug.bmp --audio
```

Default-device peaks were 0.499969 after wired Start and 1e-12 after Stop.
A second Run reached 0.499969; after fresh-process reopening without the import
source, Start again reached 0.499969 and Stop returned to 1e-12. The test also
checks silence before Start and normal close during processing. These are
endpoint-stream measurements, not acoustic or human-listening evidence.

Cold device activation took about 2.5 seconds on this machine. Hardware-only
checks therefore allow ten seconds for activation/teardown and five seconds for
tone detection while checking responsiveness. The copied fixture holds its note
for over seventeen seconds so natural completion cannot satisfy Stop. Production
audio code, device settings, volume and mute were unchanged. Temporary diagnostic
probes were removed. The later changes only synchronize native file dialogs and
rename a test constant; the device path was not rerun after those changes.

`git diff --check` passes. AGENTS.md and CLAUDE.md are byte-identical and below
32 KiB (32,753 bytes each). The final source/doc snapshot audit found 51 baseline
files byte-identical, 16 changed paths belonging to this ticket and no missing
baseline files. Completion notes are separate from that source/doc count.

Summary: **Standards 0; Spec 0.** No remaining issue within either axis.
