# MIDI files and Windows audio output

Status: MIDI playback and Windows output integrated by graph-editor ticket 02.

The original code was written provisionally on 2026-09-12. Ticket 02 was explicitly
invoked on 2026-09-14 and integrated it with the agreed playback contracts. See
[the app guide](graph-editor.md) for the GUI and current verification. Media policy
remains in these reusable nodes; the kernel only gains generic release/error hooks.

## Try the complete graph

Build and test using the command in [windows.md](windows.md), then run:

```powershell
.\build\windows\Debug\midi_cli.exe tests/fixtures/one-note.mid demo.wav --play
```

Use a fresh output name; this CLI refuses to overwrite an existing file. Remove
`--play` for offline rendering. Both paths produce a 48 kHz mono PCM16 WAV. Windows
file arguments accept Unicode paths. The CLI buffers its capture in memory and
limits input duration to ten minutes; that limit belongs to this example app.

Its graph is:

```text
TriggerAt(0) -> MidiFile -> VoiceAllocator(8) -> eight SineSynths -> Mix
                                                                  |
ConstFloat(0.15) -----------------------------------------------> Gain
                                                                  |
                                                    +-------------+-----+
                                                    |                   |
                                                 Capture           AudioOutput
                                                    |              (--play only)
                                                  WAV
```

WAV saving in this CLI still happens after processing. It is not the proposed
Start/Stop-controlled recording node. The file-end event is dispatched by rendering
one extra frame after the MIDI duration, so a one-second file produces 48,001 frames.

The existing instrument plays up to eight sine voices. It ignores instrument/program
changes, sustain and other controllers, pitch bend, and percussion kits. The existing
voice allocator also identifies notes by pitch without separating MIDI channels.
The importer preserves channel messages; these limitations belong to this instrument.

## MidiFileNode

Include `modular/nodes/MidiFileNode.h` and link `modular_midi`.

| Surface | Behavior |
| --- | --- |
| Construction | Empty/unready, a filesystem path, or an already parsed `MidiFile` |
| `import(path)` | Edit-only replacement; failed import preserves previous content |
| `ready()` / `importError()` | Readiness and latest import failure |
| `trigger()` input | `event<trigger>` starts/restarts the file from the beginning |
| `stop()` input | Releases held notes and cancels future events; wins same-frame Start |
| `midi()` output | `event<midi>` emits channel messages at sample offsets |
| `prepare()` | Converts absolute file times to frames at the Engine sample rate |
| `durationFrames()` | Duration including end-of-track silence, available after prepare |
| `file()` | Parsed events, duration in seconds, and skipped SysEx/escape count |

File reading and parsing happen during construction or Edit import, with no file access in
`process()`. Loading alone does not play: a graph connection must deliver a trigger.
Events are rounded from absolute times, keeping results independent of processing
block size. Simultaneous events retain track order, then event order within a track.

Supported: Standard MIDI File formats 0 and 1 with PPQ timing, tempo changes,
track-local running status, and all MIDI 1.0 channel messages. Without a tempo event,
the tempo is 500,000 microseconds per quarter note. Format 1 tempo events must be in
the first track. Unknown chunks and metadata are skipped by their declared lengths.

Format 2 and SMPTE division are rejected with explicit errors. SysEx and escape
events are skipped and counted: the current `MidiMsg` payload only carries channel
messages. Text, signatures, and markers are not projected as graph events. Malformed
chunks, statuses, tempo events, missing track endings, and invalid frame conversions
throw before processing. Timing follows the file; there is no external tempo input.

The node shares `MidiBlockNode`'s Start/Stop contract. Restart releases held notes
and replaces pending events from that instant, preserving earlier events in the
block. Completion releases remaining notes. File completion uses the EOT duration;
bare MIDI blocks complete at their last entry. Seek and loop remain deferred.

The file rules follow the [Standard MIDI Files specification](https://midi.org/standard-midi-files-specification).

## AudioOutputNode

Include `modular/nodes/AudioOutputNode.h` and link `modular_windows_audio` on Windows.
It has one `audio<1>` or `audio<2>` input (`in()`), selected at construction, and no
output ports. It plays every received block, including silence on a disconnected
input. Device output is independent of MIDI parsing and WAV capture.

`prepare()` opens the Windows default output device and preallocates eight buffers.
It accepts integer rates from 1,000 to 384,000 Hz, subject to device support, and
maximum block sizes between one frame and one second. Device-open and preparation
failures throw. Processing interleaves the audio into PCM16, clamps finite samples
to [-1, 1], replaces non-finite samples with silence, and submits the buffers.

The adapter uses the Windows SDK's buffered WinMM `waveOut` API to keep this first
Windows implementation contained and dependency-free. Microsoft classifies it as a
legacy API and recommends WASAPI for modern Windows audio; a later backend can
replace the private implementation without changing graph ports. See
[waveOutOpen](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutopen)
and [Microsoft's waveform-audio guidance](https://learn.microsoft.com/en-us/windows/win32/multimedia/processing-the-mm-wom-done-message).

Run the Engine on an application worker thread: this sink waits when its queue fills.
It is not a hard-realtime callback implementation. At 48 kHz with 512-frame blocks,
the eight buffers hold about 85 ms; actual speaker latency includes Windows/device
buffering and is not measured or compensated. Device selection, hot-plug recovery,
input recording, ASIO, and multiple-device synchronization are not implemented.

Runtime write failures and bounded wait timeouts are stored rather than thrown by
`process()`. The host must check `failed()`; `errorMessage()` supplies details outside
processing. Call `drain()` on the processing thread after the final block to finish
queued playback. Engine teardown calls `release()`, which resets playback and
closes the device while retaining the authored node. Destruction also releases it. Preparing,
processing, draining, and destruction must not run concurrently; failure reporting
can be read from another thread.

Prepared storage remains stable until Windows returns its headers, following
[waveOutPrepareHeader](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutprepareheader)
and [waveOutReset](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutreset).

## Historical provisional verification (2026-09-12)

Debug and Release pass 58 doctest cases (15,904 assertions), the CLI smoke test, and
kernel purity. The MIDI tests cover formats 0/1, tempo changes, ordering, running status, metadata
and SysEx handling, malformed and truncated data, actual file loading, exact trigger
timing at 44.1/48 kHz and multiple block sizes, and retriggers inside a single block.
The CLI test covers Unicode file arguments, WAV output, and refusing to overwrite
existing files. CTest does not require speakers. The Windows device smoke check is the CLI command
above: it exercises buffer reuse, a short final block, drain, and WAV output.

On the development PC, the mono device check returned success and produced a
48,001-frame WAV. Playback and offline captures matched byte for byte, as did
Debug and Release, with audio in the first half-second and silence afterward.
This confirms API completion, not a listening assessment. Stereo,
device removal, and other sound hardware still need manual coverage.
