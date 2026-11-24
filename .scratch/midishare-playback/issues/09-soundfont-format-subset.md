# Which part of the SoundFont format does the node read?

Type: grilling
Status: resolved
Blocked by: 07, 10

## Question

The soundfont node reads a soundfont at authoring time, as `MidiFile` reads MIDI: file I/O and
parsing never happen in `process()`. The SoundFont 2 format is large (presets, instruments,
zones, generators, modulators), and a node that reads all of it is a large piece of work. A
node that reads too little plays the chosen piano wrongly.

Using the default soundfont that
[Which soundfonts ship, in what format, under what licence and at what size?](07-which-samples-ship.md)
recommends, decide:

- Which generators the node honours: key and velocity ranges, root key and tuning, sample modes
  and loops, the volume envelope, attenuation, pan, the filter. Which may it ignore without an
  audible difference on that piano, and which must a reusable node honour anyway, since a DAW
  will load other soundfonts later?
- Default modulators (velocity to attenuation, velocity to filter): honoured, approximated, or
  ignored?
- What the node does with a soundfont that uses something it does not read: refuse it at
  authoring time with an error, or play it as best it can. Authoring errors throw, by the
  kernel's convention.
- SoundFont 3 (Ogg Vorbis samples): read, or refused? Reading it puts a decoder in the node
  library.
- What the node reads velocity from, given that abcjs assigns loudness and the site's own notes
  carry none.

The answer fixes the node's authoring-time interface and the size of its spec.

## Answer

**The node honours every generator that shapes one voice's sound, and ignores only what has no
meaning inside it: effects sends and moving controllers.** It reads SoundFont 2 at authoring
time. It refuses, with an error, a file it cannot play (malformed, or SoundFont 3), and plays
everything else as the spec's own rules say, listing what it ignored. Velocity comes from each
note-on's velocity byte, channel 10 plays the preset like every other channel, and each left
and right zone is its own mono voice. Nothing changes under `kernel/` or `workspace/`, the page
contract stands as written, and no ADR is needed.

Every decision below was decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.
Like every planning record, it is provisional until its planning PR merges.

### Facts this rests on

The piano's facts come from
[Measure the FluidR3_GM piano before it becomes the default](10-measure-the-fluidr3-piano.md).
The rest come from a census of the whole `FluidR3_GM.sf2` (189 presets) by a throwaway
standard-library reader,
[`PROTOTYPE_census.py`](../prototypes/09-format-census/PROTOTYPE_census.py)
(`python -I PROTOTYPE_census.py <file.sf2>`), which reads the hydra and never the sample data.
FluidR3's other presets stand in for "other soundfonts": they are the stretch goal's first
candidates.

- **The piano** (bank 0, program 0) uses key and velocity ranges, `overridingRootKey`,
  `sampleModes` 1, pan ±500, the volume envelope's attack, hold, decay, sustain and release, a
  preset-level release per velocity zone, `initialAttenuation`, `reverbEffectsSend`,
  `initialFilterFc`, `modEnvToFilterFc` and `releaseModEnv`. It sets no modulator, LFO, tuning
  generator or `exclusiveClass`.
- **The rest of bank 0** also uses both LFOs (`freqModLFO` in 106 zones, `modLfoToVolume` in
  52, `vibLfoToPitch` in 5), the modulation envelope to cutoff (59 zones) and to pitch (7),
  `initialFilterQ` (84), `coarseTune`, `fineTune`, `scaleTuning` and the loop offsets. The drum
  kits in bank 128 add `exclusiveClass` (30 zones, classes 1, 4, 5 and 6) and real velocity
  layers (30 of 31 kits; no bank-0 preset has one).
- **File modulators.** There is no preset-level modulator. 17 of bank 0's 128 presets, and 622
  drum-kit instrument zones, carry one instrument modulator: note-on velocity to
  `initialFilterFc` with amount 0 and a negative velocity switch as its amount source (`0x0102`,
  amount source `0x0D02`). An amount-0 modulator does nothing except switch off a default
  modulator of the same identity, so FluidR3's authoring tool gave the default
  velocity-to-cutoff modulator that amount source. None of the eight pianos (0:0 to 0:7) carries
  it. Brass Section has two more velocity modulators.
- **Values outside the spec's ranges** occur: `attackVolEnv` −32768 and `initialFilterFc`
  14400.
- **Loops.** Only `sampleModes` 0 and 1 occur (814 and 1,827 zones). No loop lies outside its
  sample, but 7 loops are shorter than the spec's 32 points and 346 zones have fewer than the
  spec's 8 points between the loop and an end of the sample.
- **Samples.** 485 left, 485 right and 448 mono; none linked, in ROM or compressed; 21 sample
  rates from 11,025 to 48,000 Hz; `ifil` 2.01 and no `sm24`. No `wSampleLink` is usable
  (ticket 10). In 9 instruments (29 pairs) the left and right zones of a range are detuned
  against each other on purpose, for example Sitar by ±1 cent and Sweep Pad by ±5 cents.

### D1. Generators: every one that shapes a voice is honoured

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

| Group | Generators | Default piano | The node |
|---|---|---|---|
| Zone selection | `instrument` 41, `keyRange` 43, `velRange` 44, `sampleID` 53 | 20 key ranges with two sides each; 9 velocity zones | Honoured at both levels, wherever they sit in a zone |
| Root and tuning | `overridingRootKey` 58, `coarseTune` 51, `fineTune` 52, `scaleTuning` 56, `keynum` 46; the header's pitch, correction and rate | Root key needed: every header says 60. The rest unset | Honoured |
| Sample addressing and loops | offsets 0–4, 12, 45 and 50; `sampleModes` 54 | Mode 1 throughout; loop offsets 0 | Honoured (D7) |
| Volume envelope | 33–40, key scaling included | Attack, hold, decay, sustain and release | Honoured |
| Level and placement | `initialAttenuation` 48, `pan` 17, `velocity` 47 | 140 cB; ±500 | Honoured |
| Filter | `initialFilterFc` 8, `initialFilterQ` 9 | 12623 cents; Q unset | Honoured: the spec's two-pole resonant low-pass |
| Modulation envelope | 25–32, `modEnvToPitch` 7, `modEnvToFilterFc` 11 | −1000 cents to the cutoff, constant after the first 2 ms: its only slow stage is a 100 s release | Honoured |
| LFOs | 5, 6, 10, 13, 21–24 | Unset | Honoured |
| Exclusive class | 57 | Unset | Honoured (D8) |
| Effects sends | `chorusEffectsSend` 15, `reverbEffectsSend` 16 | Reverb 10 % | Ignored and reported: the node's output is dry, and reverb is wiring after it ([ticket 05](05-soundfont-node-shape-and-samples.md), D1) |
| Unused, reserved, unknown | 14, 18–20, 42, 49, 55, 59, and 61 up | None | Ignored, as the spec requires |

- Preset-level values add to instrument-level ones. Instrument-only generators at the preset
  level are ignored. A generator's final value is clamped to the spec's range for it.
- **Why all of them.** What the piano does not need adds little: the LFOs are two oscillators
  per voice, the modulation envelope is the volume envelope's code with other destinations, key
  scaling and tuning are one formula each, and exclusive class is one pass over the voices.
  FluidR3's other presets use each of them, and so will the files a DAW loads. A node that skips
  one plays such a file wrongly with no error, which D3 exists to prevent. What stays ignored
  has no meaning inside this node: it has no effects unit, and it ignores controllers
  ([ticket 04](04-page-to-modular-contract.md), decision 13.5).
- **Ignorable on the default piano without an audible difference:** the tuning generators, key
  scaling, `initialFilterQ`, the LFOs, exclusive class, the address offsets other than the
  loop's, sample modes other than 1, and the modulation envelope's shape (a constant −1000 cents
  would do). They are honoured anyway, for the reasons above. The spec may still cut the work
  into a piano-first ticket and a second one.
- **Nothing is approximated** in the format's sense. The interpolation order and the filter's
  exact form are implementation choices, bounded by D10's tolerances.

### D2. Default modulators: honoured, velocity to cutoff without a switch

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

| Default modulator | The node |
|---|---|
| Velocity to attenuation, 960 cB, concave | Honoured. The spec's concave curve comes to 40·log10(v/127) dB: −3.3 dB at velocity 105, −7.0 at 85, −11.9 at 64 and −16.9 at 48 |
| Velocity to filter cutoff, −2400 cents | Honoured at every velocity: linear, with no velocity switch |
| CC7 and CC11 to attenuation, CC10 to pan, CC1 and channel pressure to vibrato, CC91 and CC93 to the sends, the pitch wheel to pitch | Held at General MIDI's power-on values, because the node ignores controllers: CC7 100, CC10 64, CC11 127, the wheel centred, every other source 0, the sustain pedal (CC64) included. In effect a constant 4.2 dB from CC7, the value abcjs's files also send, and nothing else |

**The sustain pedal is not followed**, nor any other controller. Ticket 04's decision 13.5
ignores controllers; abcjs's files send the pedal only as "off" at each track's start (ticket
04's facts); and abcjs's own synth ignores it, so today's sound has no pedal either. A DAW may
want it later. It would be an addition, following §9.2 of the 2.04 spec: a note the pedal
keeps stays in the key-on state, which
[How does a released voice keep sounding until its release ends?](06-release-survives-voice-allocation.md)
(D3) already ranks as held.

**The velocity-to-cutoff choice.** Three readings exist. The default piano's cutoff, in kHz,
derived from its 11623 cents as ticket 10 did, not rendered:

| Velocity | 127 | 105 | 85 | 64 | 63 | 48 | 40 |
|---|---|---|---|---|---|---|---|
| Linear at every velocity (chosen) | 6.7 | 5.2 | 4.2 | 3.4 | 3.3 | 2.8 | 2.6 |
| A positive switch: from 64 up only (FluidSynth) | 6.7 | 5.2 | 4.2 | 3.4 | 6.7 | 6.7 | 6.7 |
| A negative switch: below 64 only | 6.7 | 6.7 | 6.7 | 6.7 | 3.3 | 2.8 | 2.6 |

- **Softer is never brighter.** Both switched readings jump an octave in cutoff between
  velocity 63 and 64, and abcjs's files straddle that line: its chord track plays bass at 64
  and chord notes at 48, its `!p!` bars play at 60 and 50, and a crescendo crosses it. Under
  FluidSynth's reading the soft accompaniment would sound brighter than the melody.
- **It equals FluidSynth from velocity 64 up,** where abcjs puts the melody (beats at 85, 95
  and 105) and the note preview (105). Today's site plays FluidSynth's renders made at velocity
  85, a 4.2 kHz cutoff, and this reproduces it.
- **It is what the 2.04 text says, as the samples research read it.** Whether an earlier text
  gave the modulator a switch is for the spec to confirm (below); the choice does not rest on
  it.
- **A file can still turn it off.** A file modulator replaces a default one when their source,
  destination and transform match, **whatever their amount sources**. This leniency is
  deliberate: the amount source is the one field on which the texts and FluidSynth disagree,
  and FluidR3's switch-offs (Facts) carry yet another. Matched strictly, they would match no
  reading and switch nothing off. Preset-level modulators add to instrument-level ones, as the
  spec says.

**File modulators are honoured, evaluated once per voice at its note-on.** With controllers
held, every source (velocity, key number, a constant or a held controller) is constant for the
life of a note, so each modulator is a fixed offset to a generator. Nothing is evaluated per
sample, and `process()` allocates nothing for it. A modulator whose source is another
modulator's output (a 2.04 link) is ignored and reported; FluidR3 has none.

### D3. A file using something the node does not read: refuse only what cannot play

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

| The file has | The node |
|---|---|
| A malformed RIFF structure, a truncated chunk, a hydra list of the wrong size, an index or a sample pointing outside its list or the sample data, or no preset | Throws at load: `soundfont: <what went wrong>` |
| An `ifil` major version other than 2, or compressed samples | Throws at load (D4) |
| No preset at the bank and program the setting names | Throws at the setting (ticket 05, D3) |
| A zone whose sample is in ROM (type bit `0x8000`) or empty | Skips the zone and reports it |
| Effects sends, link modulators | Ignores and reports them |
| Unknown, unused or reserved generators; instrument-only generators at the preset level; modulators the spec calls illegal | Ignores them, as the spec says |
| Generator values outside the spec's ranges | Clamps them |
| Loops shorter than 32 points, or closer than 8 points to an end of the sample | Plays them as given. While looping, interpolation takes its neighbours across the loop's seam, so the points beyond the loop are never read |
| A loop outside its sample, or empty | Plays the zone unlooped and reports it |
| An unusable `wSampleLink` | Never reads it (D6) |

- **Refuse what cannot play; play the rest.** An authoring error is an error in the file, and
  the kernel's convention makes those throw. A feature this node does not implement is not an
  error in the file, and the spec itself tells a reader to ignore what it does not know.
  Refusing over a send would refuse nearly every General MIDI bank: FluidR3 sets a reverb send
  on 225 bank-0 zones.
- **A load checks the whole file**, every preset, because the file is kept whole and the preset
  can change later (ticket 05, D5). Choosing a preset then fails only when it is absent.
- **A report, not an error.** A load leaves a list of what the node ignores in the file, in the
  errors' short form (`soundfont: reverb and chorus sends are ignored`), readable after the
  load, as `MidiFile` reports its skipped SysEx count. The editor may show it and the web glue
  may drop it. It is built at load, so `process()` never sees it.

### D4. SoundFont 3: refused

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **Refused at load with `soundfont: compressed samples (sf3) are not supported`**, when `ifil`
  has major version 3 or a sample header sets the compressed bit (`0x10`).
- **Why.** Reading it puts a Vorbis decoder in `nodes/`: a third-party dependency, a larger
  module, and a decoder whose floating-point output
  [How is a browser render checked against a native one?](08-browser-render-checked-against-native.md)
  would also have to cover. Nothing needs it: the default piano ships as a 7.45 MiB SoundFont 2
  extract, under the host's 25 MiB limit (ticket 10), and the samples research chose
  SoundFont 2 for this reason ([ticket 07](07-which-samples-ship.md)).
- **It stays open at no cost.** The node keeps the file's bytes whole (ticket 05, D5), so a
  later version can read SoundFont 3 with no save-format change.

### D5. Velocity: each note-on's own velocity byte

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **The node reads velocity from each note-on on its `midi` input** (`MidiMsg::data2`), as the
  MIDI bytes carry it. It has no velocity input, setting or default.
- **The site's notes get theirs from abcjs.** They reach Modular only inside the Download .mid
  bytes, where abcjs writes the loudness it assigns: beats at 105, 95 and 85, accents at 127,
  dynamics such as `!p!` at 60 and 50, grace notes at two thirds of their main note, and the
  chord track at 64 and 48. The note preview plays at 105 (ticket 04, decisions 8 and 13.3).
- **What velocity does:** it chooses zones by `velRange` (on the piano, a release of 0.4 to
  1.2 s), drives the two velocity default modulators (D2) and any file modulator with a
  velocity source, and is replaced within a zone by the `velocity` generator when that is set.
- A note-on with velocity 0 is a note-off, by MIDI's rule (`MidiMsg::isNoteOff`). Note-off
  velocity is ignored: no SoundFont 2 source reads it.

This is what ticket 04's decision 13.3 needs: velocity is honoured.

### D6. Stereo: each zone is its own mono voice

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **Every instrument zone a note matches plays as its own mono voice**, with its own generators,
  placed by its own `pan` into the node's `audio<2>` output. The node never reads `wSampleLink`
  or the left, right and linked sample types.
- **On the default piano** a note plays two voices, the left sample panned −500 and the right
  panned +500, so the recording's stereo image is kept exactly. The two sides of a range share
  every pitch input, so their voices advance in step, sample for sample.
- **Why not the spec's linked pairs.** No link in FluidR3 is usable (ticket 10), so a reader
  that needed them would refuse the file or fall back to this anyway. And 9 of its instruments
  detune their two sides against each other on purpose (Facts); locking a pair to one side's
  pitch, as the spec describes for linked samples, would erase that.
- **Pan law:** equal power between the extremes, so a centred mono zone is 3 dB down on each
  side, and ±500 is fully one-sided.
- This is the unit ticket 06's pool counts: one voice per sounding sample, 256 by default
  (its D2). A note on this piano takes two, and the pool takes both together (its D3).

### D7. Loops, and when a voice ends

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **`sampleModes` 0 and 2:** no loop; the voice plays to the sample's end.
- **1, loop continuously:** the loop repeats for the voice's whole life, **its release
  included**, as every zone of the default piano asks.
- **3:** the loop repeats while the note is held. After the note-off, the voice plays on through
  the loop's end to the sample's end.
- **Loop, start and end points** are the header's plus the zone's offset generators: the fine
  offset plus 32,768 times the coarse one.
- **A voice ends** as ticket 06 decided (its D1): when, after its attack, its volume envelope
  has fallen 96 dB below its peak, or when playback with no loop left passes its sample's end.
  On the default piano, whose zones all loop in mode 1, only the envelope ends a voice: about
  0.4 to 1.2 s after the note-off, or within a held note's hold and decay, at most about 50 s.
- **The envelope's shape**, which ticket 06 left here, follows the spec. Delay is silent and
  hold is flat. The attack rises linearly in amplitude. Decay and release fall at a constant
  rate in decibels, and a stage's time is the time for a full 100 dB change, so a release that
  starts below full level ends sooner. Sustain is an attenuation in centibels; the piano's
  1000 cB passes the 96 dB floor, so its held notes end by themselves.
- **The gains on top of the envelope**, also left here: `initialAttenuation` plus the velocity
  and held-controller modulators, clamped to the spec's 0–1440 cB, so no voice is ever louder
  than its sample; the modulation LFO's volume term; equal-power pan (D6); and the filter.
  None of them decides when a voice ends, which ticket 06 bases on the envelope alone.

### D8. Exclusive class

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **Honoured.** When a note starts a zone with a non-zero `exclusiveClass`, every other sounding
  voice of that class ends. The voices that note itself starts are spared, since a pair's two
  sides share a class.
- **Its scope is the whole node, on every channel.** The spec makes a class's scope its preset,
  and the node plays one preset on every channel (ticket 05, D2).
- **It ends with ticket 06's fade:** from the new note's sample, the old voice fades linearly to
  zero over 6 ms, as a voice taken for the pool does (ticket 06, D5), and its slot is free at
  once. One way to end a voice early, not two. SFZ's exclusive groups (`off_by`) end a voice
  with this same fast fade by default. FluidSynth instead gives the old voice a release of
  −2000 timecents (about 0.3 s for 100 dB, as ticket 06 read its source), which lets a choked
  open hi-hat ring on under the closed one. The default piano has no class, so this serves only drum kits, which no comparison
  covers.

### D9. Channel 10: played like every other channel

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **The node plays notes on channel 10 with its one preset, like any other channel.** It is
  omni (ticket 05, D2) and is not a General MIDI player: it has no drum channel, just as it
  follows no program change. A plugin instrument behaves the same way.
- **For midishare,** a clip with `%%MIDI drum` plays its drum hits as piano notes at the drum
  note numbers, as a clip with `%%MIDI program 40` plays a piano (ticket 04, decision 13.5).
  Decision 13.3 stays as written, every note on every channel, so the page contract does not
  change. midishare's census found no stored clip with `%%MIDI drum` (ticket 04, the midishare
  session's answers). Whether its engine file sends such a clip to the abcjs fallback while
  the fallback lasts is page policy (Open, below).
- **Drums later are composition:** a second soundfont node with a kit preset from bank 128, fed
  only channel 10 by a channel filter. No such filter node exists, and nothing here needs one.

### D10. What the default piano needs, and how close "sounds right" must be

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

**What the default piano needs** (ticket 10's measurement), each audible if missing:

1. The hydra: its one preset, the global zones at both levels, the 9 velocity zones and the
   20 key ranges with two sides each.
2. Preset values added to instrument values: the 140 cB attenuation, and each velocity zone's
   release.
3. `overridingRootKey`, since every header says 60, and the 32 kHz sample rate against the
   graph's rate.
4. Looping in mode 1, through the release.
5. The volume envelope: an 8 ms attack, hold (1 ms to 6.4 s) and decay (3.7 to 45 s) per zone
   down to silence, and a release of 0.4 to 1.2 s.
6. Pan ±500, as two voices per note.
7. The filter: 12623 cents plus the modulation envelope's −1000, about 6.7 kHz before velocity.
8. Both velocity default modulators.

**Two references, for two checks.**

- **The spec's equations, as amended here, are normative.** The node's tests check each feature
  on a small soundfont the test writes itself, so no licensed file enters `tests/`. Starting
  tolerances, which the spec may tune: pitch within ±1 cent of the derived pitch; each
  envelope stage within one frame of its derived time; velocity attenuation within 0.1 dB of
  D2's curve; the filter's gain at its derived cutoff within 1 dB of the design; a voice ending
  on the frame its envelope has fallen 96 dB (ticket 06, D1); and output bit-identical across
  block sizes, as every node's must be.
- **FluidSynth is the comparison renderer** for the default piano. It made the renders the site
  plays today and is the most widely used SoundFont 2 player. It renders the same MIDI bytes
  and the same extract at the same sample rate, with reverb and chorus off. "Sounds right" then
  means, for every note of a test clip at velocities 64 to 127: pitch within ±1 cent, onset
  within FluidSynth's 64-frame control block, and a per-note RMS envelope within ±1 dB in 10 ms
  windows while the note is within 60 dB of its peak, after one global gain offset is removed.
  The offset absorbs FluidSynth's master gain and the attenuation scaling noted below, which on
  this piano change only the overall level, since its attenuation is one constant. Below
  velocity 64 the two differ by design (D2); compare there only if FluidSynth's default
  modulator can be replaced with D2's.
- **Not ticket 08's reference.**
  [How is a browser render checked against a native one?](08-browser-render-checked-against-native.md)
  compares a Modular render with a Modular render, so its reference is the native build.
  FluidSynth uses different maths and is never sample-equal, so it fits this node's fidelity
  check, not that one.
- **Last, the ear.** Whether the new piano is close enough to today's sound for the abcjs
  fallback to go is the map's Sound parity item, and the user's call. It should expect two
  differences: the node's output is dry, while FluidSynth renders with reverb by default
  (whether today's renders do is unconfirmed); and D2's velocity curve may spread loudness
  wider than abcjs's synth, whose law was not checked here.

### D11. The node's authoring-time face, and the spec's size

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **The face, adding to ticket 05's:** loading bytes, which throws as D3 and D4 say; the preset
  and voice-limit settings; and a read-only list of what the loaded file uses that the node
  ignores. No velocity, channel or format setting.
- **The spec's size:** one SoundFont 2 reader (RIFF, the hydra, validation, zone merging); one
  voice (interpolated sample playback with loops, two envelopes, two LFOs, a filter and pan);
  modulators evaluated once at note-on; and exclusive class. Nothing else varies in time.
- **Layers:** nothing changes under `kernel/` or `workspace/`, no media word enters them, and
  the page contract stands as written: D5 meets decision 13.3, and D9 answers 13.5 within it.
- **No ADR and no glossary change.** Every choice here is one node's policy, which a later
  version of the node can reverse. Its terms belong in `docs/node-io.md` when the node is built
  (ticket 05, D6).

### Facts the spec must confirm

This session read nothing beyond this repository and the soundfont. These rest on recollection
of sources it did not re-read:

- **The spec texts on velocity to cutoff.** The research read 2.04 as giving the modulator no
  amount source. FluidSynth's comment on its own default says the 2.01 text gives it a velocity
  switch in the negative direction, the form FluidR3's switch-offs carry. D2 holds either way.
- **FluidSynth scales a file's `initialAttenuation` by 0.4**, for compatibility with the EMU10K1
  hardware many soundfonts were made on. The node follows the spec's centibels. On the default
  piano this changes only the overall level. If a stretch-goal preset whose zones differ in
  attenuation sounds unbalanced against FluidSynth, revisit it under the same delegation.

### Open for the planning hub

1. **Drum clips (D9).** Does midishare's Modular engine file send a `%%MIDI drum` clip to the
   abcjs fallback while the fallback lasts? Recommendation: yes, as page policy, like its
   writer-defect clips. Afterwards such a clip plays as piano notes.
2. **FluidSynth for the comparison (D10).** Installing it is a download for the user to approve
   when the spec reaches that check. Recommendation: approve then, an official FluidSynth 2.x
   release, kept outside both repositories.
3. **Sound parity (D10).** With the default piano and the node's reading both decided, the map's
   Sound parity item may be sharp enough to become a ticket.

### Planning hub decisions (2026-11-24)

Decided by the planning hub under the judgment the user delegated for this map ("All, on your
best judgment").

1. **Drum clips are page policy.** Whether midishare's engine file sends a `%%MIDI drum` clip
   to the abcjs fallback is midishare's choice. The planning hub passes the recommendation on
   ("yes, like its writer-defect clips"). Nothing on Modular's side changes.
2. **The FluidSynth comparison waits for the user's approval.** The node's own tests rest on
   the spec's equations, so building the node does not wait for FluidSynth. The comparison
   against FluidSynth is an acceptance step of the soundfont node's spec. It needs one
   download, an official FluidSynth 2.x release kept outside both repositories, which the user
   approves when that step is reached.
3. **Sound parity is not a map ticket.** Whether the new piano is close enough to today's for
   the abcjs fallback to go is the user's call, by ear, on the live site, after the FluidSynth
   comparison. The spec carries it as the last acceptance item. The map's "Not yet specified"
   item says so.
4. **The spec slices the piano first.** D11's size is accepted, because the node is meant to
   serve other soundfonts later. The tickets put what the default piano needs (D10's list) in
   the first vertical slice, so midishare's path does not wait on the LFOs, filter Q, tuning or
   exclusive class, which follow in later tickets.
