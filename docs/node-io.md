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

## SoundFontNode

Status: soundfont-node ticket 01, the tracer, ticket 02, loops and the piano's tone,
and ticket 06, persistence. Ticket 03 adds the full voice policy, 04 the rest of the
voice and the ignored list, and 05 file modulators and exclusive class. The rules
marked *interim* below change then. See `.scratch/soundfont-node/spec.md` for the end
state.

Include `modular/nodes/SoundFontNode.h` and link `modular_soundfont`. That library
holds the SoundFont 2 reader (`SoundFont`) and the node, and links only the
header-only node library. The node is one polyphonic primitive with its own voices.

**Words.** A *soundfont* is a SoundFont 2 file. A *preset* is what a player selects,
numbered by a *bank* and a program. A preset's *zones* point to instruments, and an
instrument's zones point to samples. Each zone has key and velocity ranges and
*generators*, the settings a sample plays with. *Modulators* adjust generators from
the note or controllers; the node applies the specification's default modulators
and does not read a file's own yet. A *voice* is one sounding
sample. The *voice limit* sizes the node's fixed pool of voices. A note-off starts a
voice's *release*.

| Surface | Behavior |
| --- | --- |
| Construction | Empty and not ready, or holding an already parsed `SoundFont` |
| `load(bytes)` / `load(path)` | Edit-only; parses and checks the whole file |
| `ready()` | Whether the node holds a soundfont |
| `soundFont()` | The loaded file, whose `bytes()` are the node's content; throws while empty |
| `midiIn()` input | `midi`, `event<midi>`: notes on every channel |
| `out()` output | `out`, `audio<2>`: the voices' sum, dry |
| `setPreset(bank, program)` | Edit-only; throws for a preset the file lacks |
| `bank()` / `program()` | The selected preset; zero while empty |
| `setVoiceLimit(n)` / `voiceLimit()` | 1 to 65535, default 256; throws otherwise |
| `prepare()` | Derives rate-dependent values and resets the pool |

A load reads the file into the node, which keeps it whole as its content. A failed
load throws and keeps the previous content. A successful load selects the lowest
bank, then the lowest program in it, and keeps the voice limit. A path is read once:
the node renders after the file is deleted. `prepare()` never reads the soundfont
again, so a fresh `Engine` on the same graph renders from a clean start. It throws
`soundfont: load a soundfont first` while empty, and when the voice limit is below
the most voices one note of the preset can start.

**Saving.** A graph saves the node with its codec, `modular.soundfont/1`, from
`soundFontCodecs()` in `modular_primitive_persistence`, which a client passes
beside `primitiveCodecs()`. It keeps the file's bytes whole, the preset and the
voice limit, so the graph reopens and plays after the file is deleted. An empty node
reopens empty. The Windows editor passes only `primitiveCodecs()`, so its Open
refuses a graph holding the node. See
[graph-persistence.md](graph-persistence.md#owned-soundfont-representation).

**Errors.** Authoring errors throw `std::invalid_argument` with a short
`soundfont: ...` message. A load refuses:

- a malformed RIFF structure, a truncated chunk, a missing or duplicate list, and a
  missing or wrong-size `ifil` version. Bytes after the RIFF chunk are ignored;
- a hydra list of the wrong size, including one with records no index reaches;
- an index or a sample pointing outside its list or the sample data;
- a file with no preset;
- `ifil` major version 3, or a sample with the compressed bit:
  `soundfont: compressed samples (sf3) are not supported`. Other versions than 2
  are refused too;
- *interim (ticket 04 skips and lists them):* a zone whose sample is in ROM or
  empty. A ROM sample no zone plays does not stop a load. A zone whose sample has a
  sample rate of 0 is refused too, a guard of this ticket's own for ticket 04 to
  settle beside them.

The reader reads 16-bit sample data. An `sm24` chunk is skipped, so a 24-bit file
plays at 16 bits.

**Zones.** The first zone at either level without a terminal generator is that
level's global zone. A note plays every instrument zone whose key and velocity
ranges, at both levels, contain it. Preset-level values add to instrument-level
ones, and instrument-only generators at the preset level are ignored. Each final
value is clamped to the specification's range. Generators after a zone's terminal
generator are ignored, and a repeated generator keeps its last value.

**The voice honours so far:**

- pitch from the root key (`overridingRootKey`, else the header's original pitch,
  where 255 or an illegal value means 60) and the sample's rate against the graph's:
  `2^((key - root) / 12) * sample rate / graph rate` points per frame;
- linear interpolation, with the sample silent past its last point;
- the points played: the sample header's start, end and loop points, each plus the
  zone's address offsets, the fine offset plus 32768 times the coarse one. The start
  and end stay within the header's sample;
- loops, by `sampleModes`. 0 and 2, and any value but 1 and 3, play to the sample's
  end. 1 loops for the voice's whole life, its release included, as on the
  default piano. 3 loops while the note is held, then plays on through the loop's end
  to the sample's end. While a voice loops, interpolation follows the loop's last
  point with its first, so no point past the loop is read. Loops shorter than 32
  points, or closer than 8 points to an end of the sample, play as given. A loop that
  is empty or lies outside the points the zone plays plays unlooped;
- `initialAttenuation` plus the default modulators' attenuation, in centibels, the
  total clamped to 0 to 1440 cB, so no voice is louder than its sample;
- equal-power `pan`: a centred zone is 3 dB down on each side, and +-500 is fully
  one-sided. A stereo pair is two voices;
- the volume envelope: delay is silent and holds the sample back, the attack rises
  linearly in amplitude, hold is flat, and decay and release fall at a constant rate
  in decibels, a stage's time being a full 100 dB fall. Sustain is an attenuation in
  centibels. A release starts from the current level, so it ends sooner the lower it
  starts;
- the filter: the specification's two-pole low-pass, before the volume envelope as
  in its synthesis model. At Q 0 cB it is a Butterworth response, 3 dB down at the
  cutoff. Its cutoff in cents is `initialFilterFc`, plus the velocity-to-cutoff
  default modulator, plus `modEnvToFilterFc` times the modulation envelope. The
  filter follows it frame by frame in whole cents, and keeps it below 0.45 of the
  graph's rate. `initialFilterQ` is not read yet;
- the modulation envelope: the volume envelope's stage timing, with its own
  generators. Its level rises linearly from 0 to 1 in the attack, holds at 1, and its
  decay and release move linearly, a stage's time being a full change between 1 and
  0. Sustain is a decrease in 0.1 % steps. A release starts from the current level.

**Default modulators.** The node follows no controller, so the specification's
default modulators read every controller at General MIDI's power-on value:

- velocity to attenuation, 960 cB on a concave curve: a gain of
  `40 log10(velocity / 127)` dB, so -3.3 dB at 105, -7.0 at 85 and -11.9 at 64;
- velocity to cutoff, -2400 cents times `(128 - velocity) / 128`: linear at every
  velocity, with no velocity switch, so a softer note is never brighter;
- the controller defaults: CC7 at 100 attenuates every voice by a constant 4.2 dB on
  the same concave curve. CC11 at 127, CC10 at 64, the pitch wheel centred and every
  other source at 0, the sustain pedal included, add nothing. Controllers in the MIDI
  stream change nothing.

A voice ends once its volume envelope has fallen 96 dB below its peak after its
attack, held or released, or when, with no loop left, it passes its sample's end. A
voice on a mode 1 loop ends only by its envelope. After the last voice the output is
exact zeros, and its slot is free from the frame after its last sound. Tuning
(`modEnvToPitch` included), key scaling, `initialFilterQ` and the LFOs come in ticket
04; the file's own modulators in ticket 05.

**Notes.** Every channel, channel 10 included, plays the one preset. Velocity is the
note-on's byte, and velocity 0 is a note-off. Program changes, controllers, pitch
bend and note-off velocity do nothing. A key is a channel and a key number. A key
struck again starts a new note beside the old one. A note-off releases every voice of
its key's oldest held note; with none held, it does nothing.

**Interim rules**, replaced by the tickets named:

- *Pool full (03):* a note-on that finds too few free voices for all its zones is
  not started.
- *Unplayable zones (04):* refused at load, as listed under Errors.
- *Silent ignoring (04):* generators and modulators the node does not read are
  ignored silently, effects sends included. There is no ignored list yet, so a loop
  that plays unlooped is not listed.

`process()` allocates nothing. Voices render in slot order between events, so the
output is bit-identical at every block size. Every transcendental function whose
result reaches the output goes through one internal header,
`nodes/src/SoundFontMaths.h`, which wraps the standard library for now. Map ticket
08 decided that a browser render must match a native one bit for bit, so the soundfont
spec's ticket 08 replaces it there with portable routines.

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
