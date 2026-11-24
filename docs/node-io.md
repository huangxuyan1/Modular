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
| `import(path)` / `import(bytes)` | Edit-only replacement from a file or its bytes; a failed import throws, records its message and preserves previous content |
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
ticket 03, the voice policy, ticket 04, the rest of the voice and the ignored list,
ticket 05, the file's own modulators and exclusive class, ticket 06, persistence,
and ticket 08, portable maths. See `.scratch/soundfont-node/spec.md` for the end
state.

Include `modular/nodes/SoundFontNode.h` and link `modular_soundfont`. That library
holds the SoundFont 2 reader (`SoundFont`) and the node, and links only the
header-only node library. The node is one polyphonic primitive with its own voices.

**Words.** A *soundfont* is a SoundFont 2 file. A *preset* is what a player selects,
numbered by a *bank* and a program. A preset's *zones* point to instruments, and an
instrument's zones point to samples. Each zone has key and velocity ranges and
*generators*, the settings a sample plays with. *Modulators* adjust generators from
the note or controllers: the specification's default ones, and the file's own. A
*voice* is one sounding sample. The *voice limit* sizes the node's fixed pool of
voices. A note-off starts a voice's *release*. An *exclusive class* is a group of
zones, such as a kit's hi-hats, of which one sounds at a time.

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
| `ignored()` | What the loaded file uses that the node ignores or skips; empty while empty |
| `prepare()` | Resets the pool for the sample rate; each note-on derives its voices at that rate |

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
  are refused too.

The reader reads 16-bit sample data. An `sm24` chunk is skipped, so a 24-bit file
plays at 16 bits.

**What a load skips, ignores and lists.** A file the node only partly reads still
loads. The load leaves a read-only list, `ignored()`, of what the file uses that the
node ignores or skips, each kind once, in the errors' short form and in this order:

| Entry | When |
| --- | --- |
| `soundfont: reverb and chorus sends are ignored` | A zone at either level sets `chorusEffectsSend` or `reverbEffectsSend` above 0, or has a legal modulator with an amount to either. The output is dry; reverb is wiring after the node |
| `soundfont: link modulators are ignored` | A modulator's source is another modulator's output (the link source, 127), or its output feeds another modulator (a destination with bit 15 set) |
| `soundfont: zones playing a sample in rom are skipped` | An instrument zone's sample has the ROM bit (`0x8000`) |
| `soundfont: zones playing an empty sample are skipped` | An instrument zone's sample starts where it ends |
| `soundfont: zones playing a sample with no sample rate are skipped` | An instrument zone's sample has a sample rate of 0 |
| `soundfont: loops outside their sample play unlooped` | A zone that would loop (`sampleModes` 1 or 3) has a loop with a point outside the points it plays |
| `soundfont: empty loops play unlooped` | A zone that would loop has a loop whose end is not after its start |

- A skipped zone is left out of every preset that plays it, and the rest of the file
  plays. A ROM sample that no zone plays is neither refused nor listed.
- A loop is checked against the points the zone plays, after its address offsets. A
  zone that does not loop plays its sample to its end, so its loop is not listed.
- The list covers the whole file, every preset and instrument, not only the selected
  preset. A failed load keeps the previous content and its list. `process()` never
  reads it.
- Ignored as the specification says, and not listed: unknown, unused and reserved
  generators (14, 18 to 20, 42, 49, 55, 59, 60 and from 61 up), instrument-only
  generators at the preset level, and modulators the specification calls illegal
  (**Modulators**, below).

**Zones.** The first zone at either level without a terminal generator is that
level's global zone. A note plays every instrument zone whose key and velocity
ranges, at both levels, contain it. Preset-level values add to instrument-level
ones, and instrument-only generators at the preset level are ignored. Each final
value is clamped to the specification's range. Generators after a zone's terminal
generator are ignored, and a repeated generator keeps its last value.

**Generators.** The node honours every generator that shapes one voice's sound
(map ticket 09, D1):

| Group | Generators | The node |
| --- | --- | --- |
| Zone selection | `instrument` 41, `keyRange` 43, `velRange` 44, `sampleID` 53 | Honoured at both levels |
| Sample addressing and loops | offsets 0 to 4, 12, 45 and 50; `sampleModes` 54 | Honoured |
| Pitch | `overridingRootKey` 58, `coarseTune` 51, `fineTune` 52, `scaleTuning` 56, `keynum` 46; the header's pitch, correction and rate | Honoured |
| Volume envelope | 33 to 38, and their key scaling, `keynumToVolEnvHold` 39 and `keynumToVolEnvDecay` 40 | Honoured |
| Modulation envelope | 25 to 30, `keynumToModEnvHold` 31, `keynumToModEnvDecay` 32; `modEnvToPitch` 7, `modEnvToFilterFc` 11 | Honoured |
| Modulation LFO | `delayModLFO` 21, `freqModLFO` 22, `modLfoToPitch` 5, `modLfoToFilterFc` 10, `modLfoToVolume` 13 | Honoured |
| Vibrato LFO | `delayVibLFO` 23, `freqVibLFO` 24, `vibLfoToPitch` 6 | Honoured |
| Filter | `initialFilterFc` 8, `initialFilterQ` 9 | Honoured |
| Level and placement | `initialAttenuation` 48, `pan` 17, `velocity` 47 | Honoured |
| Exclusive class | `exclusiveClass` 57 | Honoured |
| Effects sends | `chorusEffectsSend` 15, `reverbEffectsSend` 16 | Ignored and listed |
| Unused, reserved and unknown | 14, 18 to 20, 42, 49, 55, 59, 60, and 61 up | Ignored, not listed |

`keynum` and `velocity`, when set, replace the note's key and velocity within the
zone: in its pitch, its key scaling and its modulators. The note's own key and
velocity still choose the zones.

**The voice:**

- pitch from the root key (`overridingRootKey`, else the header's original pitch,
  where 255 or an illegal value means 60) and the sample's rate against the graph's.
  In cents it is `scaleTuning * (key - root) + 100 * coarseTune + fineTune` plus the
  header's pitch correction, plus the modulation envelope's and the LFOs' terms
  below, and the sample is read at `2^(cents / 1200) * sample rate / graph rate`
  points per frame. A modulated pitch is recomputed every frame;
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
  is empty or lies outside the points the zone plays plays unlooped, and is listed;
- `initialAttenuation` plus its modulators' attenuation, in centibels, the total
  clamped to 0 to 1440 cB, so no voice is louder than its sample. The modulation
  LFO's volume term and the filter's gain at DC come on top;
- equal-power `pan`: a centred zone is 3 dB down on each side, and +-500 is fully
  one-sided. A stereo pair is two voices;
- the volume envelope: delay is silent and holds the sample back, the attack rises
  linearly in amplitude, hold is flat, and decay and release fall at a constant rate
  in decibels, a stage's time being a full 100 dB fall. Sustain is an attenuation in
  centibels. A release starts from the current level, so it ends sooner the lower it
  starts;
- key scaling, for both envelopes: `keynumTo...Hold` and `keynumTo...Decay` take
  their timecents off the hold or the decay for every key above 60 and add them for
  every key below, so 100 halves the stage an octave up. The result stays within the
  stage's own range, -12000 to 5000 for a hold and -12000 to 8000 for a decay;
- the filter: the specification's two-pole low-pass, before the volume envelope as
  in its synthesis model. At `initialFilterQ` 0 cB it is a Butterworth response,
  3 dB down at the cutoff, as FluidSynth reads the specification's "not resonant".
  Each centibel of `initialFilterQ` raises the gain at the cutoff a tenth of a
  decibel above that: its Q is `10^(dB / 20) / sqrt(2)`, FluidSynth's, so the
  response has FluidSynth's shape at every Q. 100 cB puts the gain at the cutoff
  10 dB above the Butterworth response's, 7 dB above DC where the specification's
  text says 10. The gain at DC falls by half the decibels, as the specification
  says, so it is unity at 0 cB; FluidSynth's is 1.5 dB higher at every Q. Its
  cutoff in cents is `initialFilterFc`, plus its modulators, the velocity-to-cutoff
  default among them, plus `modEnvToFilterFc` times the modulation envelope, plus
  `modLfoToFilterFc` times the modulation LFO. The filter follows it frame by frame
  in whole cents, and keeps it below 0.45 of the graph's rate;
- the modulation envelope: the volume envelope's stage timing, with its own
  generators. Its level rises linearly from 0 to 1 in the attack, holds at 1, and its
  decay and release move linearly, a stage's time being a full change between 1 and
  0. Sustain is a decrease in 0.1 % steps. A release starts from the current level.
  It moves the cutoff by `modEnvToFilterFc` and the pitch by `modEnvToPitch` cents at
  its full level;
- the two LFOs, each the specification's triangle: silent through its delay from the
  note-on, then rising from 0 to 1, falling to -1 and rising back to 0 every period,
  advanced every frame. Their frequencies are absolute cents, in which 0 is 8.176 Hz.
  The modulation LFO moves the pitch by `modLfoToPitch` cents, the cutoff by
  `modLfoToFilterFc` cents and the volume by `modLfoToVolume` centibels at its peak:
  a positive depth raises each as the LFO rises. The vibrato LFO moves only the
  pitch, by `vibLfoToPitch` cents. Neither decides when a voice ends.

**Modulators.** A voice's modulators are evaluated once, at its note-on (map ticket
09, D2). The node follows no controller, so every source is constant for a note's
life, and each modulator is a fixed offset to its destination generator: nothing is
evaluated per frame. A note-on adds each modulator's output to the zone's generator
and derives the voice from the sums.

- *Sources:* the note's velocity and key number (the zone's, where `velocity` or
  `keynum` replaces them), no controller, which is the constant 1, and controllers
  held at General MIDI's power-on value: CC7 at 100, CC10 at 64, CC11 at 127, the
  pitch wheel centred, its sensitivity at 2 semitones, and every other one at 0,
  the sustain pedal included. Controllers in the MIDI stream change nothing.
- *Curves:* linear, concave, convex or switch, each unipolar (0 to 1) or bipolar
  (-1 to 1), rising or falling. Concave and convex are the specification's 96 dB
  curves: concave is `-20/96 log10((1 - x)^2)` and convex `1 + 20/96 log10(x^2)`,
  each reaching 1 at its end and capped there. A linear or switch source is
  normalized by its range (128 values, or the pitch wheel's 16384), as FluidSynth
  does, so a centred controller is exactly half and the switch is on from 64. A
  unipolar concave or convex curve runs over a 7-bit source's values 0 to 127, so
  127 gives 1. A bipolar one runs from the centre out each way, by the distance
  from the centre over half the range, as FluidSynth's does: a centred controller
  gives exactly 0 and 0 gives -1, but 127 falls short of 1 (0.75 on the concave
  curve).
- *Output:* the amount times the source and the amount source, each on its curve,
  then the transform: linear, or 2, the absolute value.
- *Levels:* a zone takes its global zone's modulators, each superseded by an
  identical one of its own (the same source, destination, amount source and
  transform); of identical modulators in one zone the last is kept. Preset-level
  modulators add to instrument-level ones.
- *Default modulators* (below) run through the same evaluation. An instrument-level
  modulator replaces a default one when their source, destination and transform
  match, **whatever their amount sources**. So FluidR3's amount-0 modulators, note-on
  velocity to `initialFilterFc` with a velocity switch as their amount source, switch
  the velocity-to-cutoff default off. A preset-level modulator never replaces one.
- *Ranges:* each sum is kept within its generator's range, as the reader keeps a
  generator, and rounded to a whole number for a generator that counts whole things
  (address offsets, `sampleModes`, `exclusiveClass` and `overridingRootKey`). The
  cutoff is not clamped: its modulators sum with its envelope and LFO terms, and the
  filter keeps the total below 0.45 of the graph's rate.
- *Address offsets:* a note-on places the voice's points from its sample header and
  its offsets, modulated or not. A modulated loop outside the points played plays
  unlooped, unlisted, since the list is built at load.
- *Ignored, unlisted:* a modulator the specification calls illegal: a source or
  amount source that is a MIDI controller it forbids (0, 6, 32 to 63, 98 to 101 and
  120 to 127), a general controller it does not list, or a curve type past switch;
  the link as an amount source; a transform other than 0 and 2. Also one that moves
  nothing in a voice: to an unused, reserved or unknown generator, to one that
  chooses zones or names what they play (`instrument`, `keyRange`, `velRange`,
  `sampleID`), to `keynum` or `velocity`, which modulators read, and, at the preset
  level, to one valid only in an instrument. A link is ignored and listed, and so is
  a modulator with an amount to an effects send.

**Default modulators.** The specification's defaults, read at the held controllers:

- velocity to attenuation, 960 cB on a concave curve: a gain of
  `40 log10(velocity / 127)` dB, so -3.3 dB at 105, -7.0 at 85 and -11.9 at 64;
- velocity to cutoff, -2400 cents times `(128 - velocity) / 128`: linear at every
  velocity, with no velocity switch, so a softer note is never brighter;
- the controller defaults: CC7 at 100 attenuates every voice by a constant 4.2 dB on
  the same concave curve. CC11 to attenuation, CC10 to pan, and CC1 and channel
  pressure to the vibrato LFO add nothing at their power-on values. The defaults to
  the effects sends, which the node does not have, and the pitch wheel's, whose
  destination is the pitch itself and which adds nothing with the wheel centred, are
  left out.

A voice ends once its volume envelope has fallen 96 dB below its peak after its
attack, held or released, or when, with no loop left, it passes its sample's end. A
voice on a mode 1 loop ends only by its envelope. After the last voice the output is
exact zeros, and its slot is free from the frame after its last sound.

**Notes.** Every channel, channel 10 included, plays the one preset. Velocity is the
note-on's byte, and velocity 0 is a note-off. Program changes, controllers, pitch
bend and note-off velocity do nothing, and neither does a message with a data byte of
128 or more, which is not MIDI. A key is a channel and a key number. A key
struck again starts a new note beside the old one, so two parts in unison, on one
channel or two, both sound.

A note-off ends its key's oldest held note: it releases every voice of that note
still sounding. The node counts each key's held notes whatever has become of their
voices. So when that note was taken (below), or its voices ended by themselves while
it was held, its note-off is spent on it and releases no other note. A note-on that
matches no zone is a held note of its key too. A note-off with no held note on its
key does nothing, and the node keeps no credit for a later note-on.

**The pool.** The voice limit counts voices, so a note on a stereo pair takes two. A
note-off frees nothing: a voice is busy until it ends by itself. When a note-on finds
too few free voices, the node takes whole notes, every voice one note-on started,
until the new note has the voices it needs. A stereo pair or a layered note never
loses one side.

- *Which note goes:* released notes first, the one whose note-off came first. Only
  when none is left, held notes, the one whose note-on came first. Every note-on and
  note-off is numbered in stream order, so there are no ties.
- *The fade:* a taken voice ramps linearly to zero over 6 ms, rounded to whole
  frames at the graph's rate, from full on the new note's frame. The new note starts
  at its own frame.
- *Tails:* each slot of the pool carries one fading tail beside its voice, and a tail
  does not count against the limit. A slot taken again within 6 ms stops its older
  tail at once.

**Exclusive class** (map ticket 09, D8). When a note starts a zone whose
`exclusiveClass` is not 0, every other sounding voice of that class ends, held or
released, on any channel: the scope is the whole node. The note's own voices are
spared, so a pair's two sides survive, and a hi-hat struck again ends the one before
it. An ended voice fades as a taken voice does, linearly to zero over 6 ms from the
new note's frame, in its slot's tail, and its slot is free at once: the class ends
its voices before the pool takes any note for the new one.

Taking and ending depend only on the event stream, so the output stays bit-identical
at every block size while the pool turns over, also when a class's end and a take
land on one slot.

**Known limitation: `MidiBlockNode`'s held flag.** `MidiBlockNode`, and
`MidiFileNode` with it, keeps one held flag per channel and key. So at Stop,
retrigger or a file's end it sends one note-off per key, and a second held note on
the same key and channel, such as a unison on one channel, keeps sounding. A
whole-clip render never depends on this, since every note gets its own note-off from
the file: it bites only under a realtime transport. The ticket that first gives the
node one, such as the editor's, counts held notes per channel and key in
`MidiBlockNode`.

`process()` allocates nothing: `prepare()` allocates the voices, their tails, room
for the most voices one note starts and the counts of held notes, 16 channels of 128
keys. Each zone's list of modulators is made when the preset is chosen. Voices and
tails render in slot order between events, so the output is bit-identical at every
block size.

**Portable maths.** The node computes its output without the platform's maths
library. Maths libraries disagree on `exp2`, `pow`, `log10`, `sin` and `cos` in the
last place: MSVC's with Emscripten's, and even MSVC's Debug build with its Release
build, which rendered the default piano differently at 44.1 kHz. Map ticket 08
decided that a browser render matches the native one bit for bit, so every
transcendental value that reaches the output, whether computed at load, in
`prepare()`, at a note-on or per frame, comes from one internal header,
`nodes/src/SoundFontMaths.h`. Its `exp2`, `exp10`, `log10`, `sin` and `cos` are
built only from operations IEEE 754 makes exact or correctly rounded (`+ - * /`,
`floor`, `ldexp`, `frexp` and conversions), and no build fuses them (see
[project-foundations.md](project-foundations.md#implementation-conventions)). So the
same graph, soundfont, notes and sample rate render the same bits in every build.
The routines are within three units in the last place of the true values, far
inside the node's tolerances. A source check, CTest's `soundfont_portable_maths`,
fails when any other soundfont node source calls a maths-library transcendental
(`exp`, `exp2`, `pow`, `log`, `log10`, `sin`, `cos`, `tan` and their kin, in `std::`
or C form); `floor`, `ceil`, `fmod`, `llround`, `isfinite`, `ldexp`, `frexp` and
`sqrt` are exact or correctly rounded, and allowed. Golden tests in `soundfont_tests`
pin the bits: three clips that between them take every maths path the node has (the
second the LFOs, resonance, tuning and key scaling, the third file modulators and
exclusive classes), at 44.1, 48 and 22.05 kHz, as each render's frame count and a
hash of every 4,096 frames (`tests/GoldenRender.h`).
A change that alters them, such as a new maths path, updates their values in the
same commit, so review sees every change to the node's rendered bits. The web render
surface's [golden clips](web.md#golden-clips) pin the node's output through the render
core too, in every build: a change to what the node renders updates their values in
that same commit as well.

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
