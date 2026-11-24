# Spec: The soundfont node — a SoundFont 2 instrument in Modular's node library

Status: ready-for-agent
Date: 2026-11-24

This spec turns the decided parts of the wayfinder map
[midishare makes its sound with Modular](../midishare-playback/map.md) into buildable work for
one of its two Modular-side specs. It synthesizes these resolved tickets, which hold the
evidence and the reasons:

- [Is the soundfont node one primitive or a supernode of voices, and where do its samples live?](../midishare-playback/issues/05-soundfont-node-shape-and-samples.md)
  (shape, ports, settings, samples, persistence);
- [How does a released voice keep sounding until its release ends?](../midishare-playback/issues/06-release-survives-voice-allocation.md)
  (the voice policy);
- [Which soundfonts ship, in what format, under what licence and at what size?](../midishare-playback/issues/07-which-samples-ship.md)
  (SoundFont 2, the licences);
- [Which part of the SoundFont format does the node read?](../midishare-playback/issues/09-soundfont-format-subset.md)
  (the format subset, errors, references and tolerances);
- [Measure the FluidR3_GM piano before it becomes the default](../midishare-playback/issues/10-measure-the-fluidr3-piano.md)
  (the default piano's layout);
- [What does the page hand Modular, and what does it get back?](../midishare-playback/issues/04-page-to-modular-contract.md),
  only where it touches the node: velocity is honoured, the output is stereo, and the node
  parses once, so `prepare()` does not read the soundfont again.

Section numbers below (D1, D2 …) are those tickets' decision numbers. This spec decides nothing
those tickets left open, except the rulings recorded under
[Decided while writing this spec](#decided-while-writing-this-spec).

## Problem Statement

midishare's Play and note preview sound through abcjs's synth, which fetches pre-rendered MP3s
of a piano from a third-party host. The user wants every sample heard on the site to be
computed by Modular instead: a sampled piano played from a soundfont, by a Modular node that
reads a piano soundfont by default.

Modular cannot do this today. Its node library has no sample playback and no soundfont reader.
Its only instrument is a test sine voice. That voice has no envelope and ignores velocity.
`VoiceAllocatorNode` frees a voice the moment its note-off arrives, so a ringing release would
be cut short. It also names notes by pitch alone, so two parts in unison on two channels
collapse into one voice. A piano played through those parts would lose its releases, its
dynamics and its unisons.

The instrument also has to outlive the website. The user asked for "a useful compliment" to
Modular's own route: a soundfont node that a DAW built on Modular can load other soundfonts
into later. So the node must be a reusable part of the node library, follow its contracts and
read the format faithfully, not be midishare-specific code.

## Solution

A **soundfont node**: one polyphonic primitive node in its own library in the node library,
beside the MIDI library. It takes notes on one `event<midi>` input and writes stereo on one
`audio<2>` output. It owns the soundfont file's bytes, as loaded, as its **stored node
content**. It parses them once, at authoring time, when it is built or when a file is loaded
into it, as `MidiFile` reads MIDI. It never parses again in `prepare()` or `process()`.

The node reads SoundFont 2. It honours every generator that shapes one voice's sound, the
default modulators and the file's own modulators, evaluated at note-on, and exclusive class.
It ignores only what has no meaning inside it: effects sends and moving controllers. It
refuses a file it cannot play (malformed, or SoundFont 3) with an error. Everything else plays
as the SoundFont 2.04 specification says, and the node lists what it ignored.

The voices are internal and work as one pool. A note-off starts a voice's release and frees
nothing. A voice ends when its envelope has fallen 96 dB or its sample runs out. When the pool
is full, the node takes whole notes, released before held and oldest first, each with a 6 ms
fade. Struck again, a key gets a new voice beside its old one, and its note-offs end its held
notes oldest first.

The node saves inside the graph through its own codec: the whole file's bytes, plus the preset
and the voice limit. The node library ships no soundfont. The default piano, FluidR3_GM's
"Yamaha Grand Piano", is the client's to load. The node's tests build their own tiny
soundfonts. A comparison against FluidSynth on the default piano is the spec's acceptance step.

**Tickets 01 to 03 complete the website's route.** After them, the default piano plays in full
under the whole voice policy. The rest of the format, the codec and the acceptance comparison
follow, and the web build (spec B, below) needs none of them.

## User Stories

1. As a client author, I want to load a soundfont into a node from bytes in memory, so that a
   web page can hand Modular the file it fetched without Modular needing a filesystem.
2. As a desktop client author, I want to load a soundfont from a file path, so that a native
   tool can load a file without reading it into memory itself.
3. As a client author, I want the soundfont parsed once when it is loaded, so that rendering
   many clips and note previews on one loaded instrument does not pay for parsing again.
4. As a client author, I want a fresh `Engine` built on the same graph to render the same
   samples as the last one, so that one loaded instrument can render clip after clip, each
   from a clean start.
5. As a client author, I want a malformed file refused at load with a short message, so that
   my page can show the user why the instrument did not load.
6. As a client author, I want a SoundFont 3 file refused with a message saying compressed
   samples are not supported, so that I know to convert the file and do not just get silence.
7. As a client author, I want a failed load to leave the node's previous soundfont in place, so
   that a bad file does not destroy a working instrument.
8. As a client author, I want an empty node to refuse to prepare and to ask for a soundfont, so
   that I find the missing load at once instead of rendering silence.
9. As a graph author, I want the node to select the file's lowest bank and program when a file
   loads, so that a single-preset piano file plays with no further setting.
10. As a graph author, I want to choose another preset of the loaded file by bank and program,
    so that one file can serve several instruments.
11. As a graph author, I want naming a preset the file lacks to fail at once, so that a typo
    cannot leave the instrument silent.
12. As a graph author, I want to set the voice limit, so that I can trade memory for
    polyphony.
13. As a graph author, I want a voice limit outside 1 to 65535 rejected, and a limit too small
    for one note of the preset refused at `prepare()`, so that no note ever sounds half-made.
14. As a listener, I want every note in the MIDI bytes to sound on every channel, so that what
    I hear is what Download .mid contains.
15. As a listener, I want each note's velocity to choose its zone and set its loudness and
    brightness, so that the accents and softer chord accompaniment abcjs writes stay audible.
16. As a listener, I want a softer note never to sound brighter than a louder one, so that the
    accompaniment does not cut through the melody.
17. As a listener, I want two parts sounding the same pitch both to sound, so that unisons keep
    their weight.
18. As a listener, I want a slurred repeat of a pitch to keep its second note, so that abcjs's
    deliberately overlapping slurs play right.
19. As a listener, I want a released note to ring through its release even in a quick
    passage, so that the piano does not sound clipped.
20. As a listener, I want every note at its correct pitch, so that a sample recorded at one key
    and played across a range is in tune.
21. As a listener, I want a piano's left and right samples to keep their stereo image, so that
    the piano sounds as it was recorded.
22. As a listener, I want looped samples to sustain and to keep looping through the release,
    so that held notes and releases do not stop dead at the end of a sample.
23. As a listener, I want the piano's filter applied, so that the default piano is no brighter
    than its sound today.
24. As a listener, I want a voice taken for a new note to fade out in 6 ms rather than stop,
    so that a full pool does not click.
25. As a listener, I want the output to be exact zeros once the last voice ends, so that a
    trimmed render ends cleanly.
26. As a client author, I want voices to end by themselves, so that a render-until-silent
    contract can find the end of the release from the output, with no tail API.
27. As a client author, I want the output identical at every block size, so that a render does
    not depend on how the host splits it into blocks.
28. As a client author, I want `process()` to allocate nothing, so that the node can run
    inside a real-time callback later.
29. As a client author, I want program changes, controllers and pitch bend ignored, so that a
    clip sounds as the chosen preset, as the contract says.
30. As a client author, I want notes on channel 10 to play the preset like any other channel,
    so that the node behaves as an omni instrument, as a plugin instrument does.
31. As a DAW author, I want every generator that shapes a voice honoured (the LFOs, tuning,
    key scaling, filter resonance and the modulation envelope's pitch depth among them), so
    that a soundfont other than the default piano plays as its author meant.
32. As a DAW author, I want the file's own modulators honoured, including ones that switch off
    a default modulator, so that a General MIDI bank's presets sound as their author set them.
33. As a DAW author, I want exclusive class honoured across the node, so that a drum kit's
    closed hi-hat chokes the open one.
34. As a graph author, I want a list of what the loaded file uses that the node ignores, such
    as effects sends, so that I know why the output is drier than another player's.
35. As a graph author, I want a zone the node cannot play skipped and listed, rather than the
    whole file refused, so that a General MIDI bank with one odd zone still loads.
36. As a graph author, I want a loop that lies outside its sample played unlooped and listed,
    so that one bad loop point does not make a whole file unusable.
37. As a graph author, I want generator values outside the specification's ranges clamped, so
    that a file written by a lax editor still plays.
38. As a graph author, I want a saved graph to keep the soundfont inside it, so that it reopens
    and plays after the original file is moved or deleted.
39. As a graph author, I want the preset and voice limit restored on reopen, so that a
    reopened graph sounds as it did.
40. As a graph author, I want an empty node to save and reopen empty, so that I can load a
    soundfont into it later.
41. As a graph author, I want a saved graph whose soundfont no longer parses to fail Open with
    an explicit error and leave my current graph alone, so that a corrupt file cannot replace
    my work.
42. As a Windows editor user, I want a graph containing a soundfont node refused at Open with
    the ordinary unsupported-kind error, so that the editor does not fail drawing a node it
    cannot show.
43. As a maintainer, I want the node in its own library, so that a pipeline with no music in it
    links none of it.
44. As a maintainer, I want no change under `kernel/` or `workspace/`, and no media word in
    them, so that the layering Modular exists to keep holds.
45. As a maintainer, I want the node's tests to build their own soundfonts, so that no licensed
    file enters the repository.
46. As a maintainer, I want the default piano compared with FluidSynth, the player that made
    today's sound, so that "sounds right" has numbers behind it.
47. As the user, I want to judge by ear on the live site whether the new piano can replace
    abcjs's synth, so that the fallback goes only when I am satisfied.

## Implementation Decisions

### Modules

- **A new library in the node library**, beside the MIDI library (working name
  `modular_soundfont`). It holds the SoundFont 2 reader and the soundfont node. It links only
  the header-only node library, so a pipeline with no music links none of it (05, D4).
- **The reader** (working name `SoundFont`) parses bytes in memory, as `MidiFile::parse` does,
  with reading a path beside it as a desktop convenience, as `MidiFile::read` is (05, D4). It is
  public, but tests observe it through the node.
- **The node** (working name `SoundFontNode`), one polyphonic primitive (05, D1). A composed
  form, voice nodes sharing samples, is not built. It would need a reference on a wire or
  storage outside the graph, which are kernel and terminal-claim questions.
- **The codec**, kind `modular.soundfont/1`, lives in `modular_primitive_persistence` behind its
  own accessor beside `primitiveCodecs()`, not inside it. See
  [Decided while writing this spec](#decided-while-writing-this-spec).
- **`SineSynthNode`'s comment is reworded** so that "polyphony is a `VoiceAllocatorNode`,
  several of these, and a `MixNode`" stops reading as a rule for every instrument in the
  library (05, D1).
- **Nothing changes under `kernel/` or `workspace/`.** There is no ADR and no `CONTEXT.md`
  change (05, D6; 06, D7; 09, D11). The node-library words soundfont, preset, bank, zone,
  generator, modulator, voice, voice limit and release are documented in `docs/node-io.md`. In
  this effort, "bank" keeps its SoundFont meaning, part of a preset's number. Say "the node's
  samples" or "its content" for the storage.

### The node's face

- **Ports:** one input, `midi` (`event<midi>`), and one output, `out` (`audio<2>`). There are
  no value inputs. Level is a gain node's job (05, D2).
- **Omni:** notes on every channel play the one preset, channel 10 included. Program changes,
  controllers, pitch bend and note-off velocity are ignored (05, D2; 09, D5, D9).
- **Velocity** comes from each note-on's own velocity byte. Within a zone, the `velocity`
  generator replaces it when set. Velocity chooses zones by `velRange` and drives the velocity
  default modulators and any file modulator with a velocity source. A note-on with velocity 0
  is a note-off. The node has no velocity input, setting or default (09, D5).
- **Content:** empty and not ready, or loaded from bytes or a path. A load parses and checks
  the whole file, every preset, and throws as the reader's rules below say. A failed load
  keeps the previous content. A successful load selects the file's lowest bank and program
  (lowest bank first, then the lowest program in it) and keeps the voice limit (05, D3, D4;
  the rulings below).
- **Settings:**
  - **Preset:** a bank and program in the loaded file. Naming one the file lacks throws.
  - **Voice limit:** 1 to 65535, default 256. Any other value throws.
  - Changing either is an authoring edit that takes effect at the next `prepare()` (05, D3;
    06, D2).
- **The ignored list:** a read-only list, built at load, of what the file uses that the node
  ignores, in the errors' short form, for example `soundfont: reverb and chorus sends are
  ignored`. `process()` never sees it. The editor may show it and the web glue may drop it
  (09, D3, D11).
- **`prepare()`** throws when the node is empty, asking for a soundfont first. It also throws
  when the voice limit is below the most voices one note of the selected preset can start. It
  derives what depends on the sample rate (pitch increments, envelope rates, filter
  coefficients, the fade length) and allocates and resets the pool: the voices, their fading
  tails, and the counts of taken notes, one per channel and key. It never reads the soundfont
  again, so a new `Engine` on the same graph reuses the parsed soundfont (05, D4; 06, D2, D6).
- **`process()`** allocates nothing. A note starts at its event's sample offset. Voice state
  advances per sample, and allocation depends only on the event stream, so the output is
  bit-identical at every block size (05, D2; 06, D6).
- **Errors** are authoring errors and throw `std::invalid_argument` with short lowercase
  `soundfont: what went wrong` messages, by the kernel's convention. Under WebAssembly, the
  web glue catches them at its export boundary (map ticket 02's ruling); the node does nothing
  special for that.

### The reader

- **What it reads:** the RIFF form, the `ifil` version, the sample data and the nine hydra
  lists: presets, instruments, samples and their bags, generators and modulators. 16-bit PCM
  only. An `sm24` chunk is skipped, so a 24-bit file plays at 16 bits, as the spec allows.
- **What it refuses**, at load (09, D3, D4):
  - **malformed files:** a malformed RIFF structure, a truncated chunk, a hydra list of the
    wrong size, an index or a sample pointing outside its list or the sample data, or no
    preset. Each throws `soundfont: <what went wrong>`;
  - **SoundFont 3:** `ifil` major version 3, or a sample header with the compressed bit
    (`0x10`). These throw `soundfont: compressed samples (sf3) are not supported`. Any other
    major version than 2 also throws.
- **Zones** (09, D1):
  - A preset zone points to an instrument, and an instrument zone to a sample. The first zone
    at either level with no terminal generator is that level's global zone, and applies as
    the 2.04 specification defines.
  - A note plays every instrument zone whose key and velocity ranges, at both levels, contain
    the note's key and velocity.
  - Preset-level generator values add to instrument-level ones. Instrument-only generators
    at the preset level are ignored. Each generator's final value is clamped to the
    specification's range for it. Values outside those ranges occur in FluidR3, for example
    `attackVolEnv` −32768 and `initialFilterFc` 14400.
- **What it skips, ignores and lists** (09, D3):
  - A zone whose sample is in ROM (type bit `0x8000`), or empty, is skipped and listed.
  - Effects sends (`chorusEffectsSend`, `reverbEffectsSend`) and link modulators are ignored
    and listed.
  - Unknown, unused or reserved generators, instrument-only generators at the preset level,
    and modulators the specification calls illegal are ignored, as the specification says.
    They are not listed.
  - A loop outside its sample, or an empty loop, plays unlooped and is listed.
  - `wSampleLink` and the left, right and linked sample types are never read (09, D6).

### The voice

A voice is one sounding sample: every instrument zone a note matches plays as its own mono
voice, with its own generators, placed by its own pan into the stereo output. A note on the
default piano plays two voices, the left sample panned −500 and the right +500 (09, D6).

- **Pitch:** the root key is `overridingRootKey` when it is set, and otherwise the sample
  header's original pitch, where 255 or an illegal value means 60. The pitch in cents is
  `scaleTuning · (key − root) + 100 · coarseTune + fineTune + the header's correction`, plus
  the modulation envelope's and the LFOs' pitch terms. The `keynum` generator replaces the
  key. The sample is read at `2^(cents/1200) · sample rate / graph rate` points per frame, so
  Modular's one global rate needs no resampling of the source (09, D1; research for ticket 07).
- **Interpolation:** interpolated sample playback. The order is an implementation choice,
  bounded by the pitch tolerance below. While a voice loops, interpolation takes its
  neighbours across the loop's seam, so the points beyond the loop are never read (09, D3).
- **Loops** (09, D7):
  - `sampleModes` 0 and 2 play to the sample's end.
  - 1 loops for the voice's whole life, its release included.
  - 3 loops while the note is held, then plays on through the loop's end to the sample's end.
  - Start, end and loop points are the header's plus the zone's offset generators: the fine
    offset plus 32,768 times the coarse one.
  - Loops shorter than 32 points, or closer than 8 points to an end of the sample, play as
    given.
- **The volume envelope** (09, D7):
  - Delay is silent and hold is flat.
  - The attack rises linearly in amplitude.
  - Decay and release fall at a constant rate in decibels, and a stage's time is the time for
    a full 100 dB change, so a release that starts below full level ends sooner.
  - Sustain is an attenuation in centibels.
  - Times are timecents, with key scaling (`keynumToVolEnvHold`, `keynumToVolEnvDecay`).
- **The modulation envelope** has the same shape, with `keynumToModEnvHold` and
  `keynumToModEnvDecay`, and drives `modEnvToPitch` and `modEnvToFilterFc`.
- **The LFOs:** the modulation LFO (delay, frequency, to pitch, to filter cutoff, to volume)
  and the vibrato LFO (delay, frequency, to pitch), as the specification defines them.
- **The filter:** the specification's two-pole resonant low-pass. Its cutoff is
  `initialFilterFc` plus the modulation envelope's, the modulation LFO's and the modulators'
  terms. Its resonance is `initialFilterQ`. The exact form is an implementation choice,
  bounded by the filter tolerance below.
- **The gains on top of the envelope:**
  - `initialAttenuation` plus the velocity and held-controller modulators, clamped to the
    specification's 0–1440 cB, so no voice is ever louder than its sample;
  - the modulation LFO's volume term;
  - equal-power pan, where a centred mono zone is 3 dB down on each side and ±500 is fully
    one-sided;
  - the filter.

  None of these decides when a voice ends (09, D6, D7).
- **When a voice ends** (06, D1; 09, D7):
  - after its attack, its volume envelope has fallen 96 dB below its peak, whether in release
    or while still held, in decay or sustain;
  - or playback with no loop left passes its sample's end.

  A note-off starts the release and frees nothing. Once the last voice has ended, the node
  writes exact zeros.
- **Modulators** are evaluated once per voice, at its note-on (09, D2):
  - With controllers held, every source is constant for a note's life (velocity, key number, a
    constant or a held controller). Each modulator is then a fixed offset to its destination
    generator, and nothing is evaluated per sample.
  - Preset-level modulators add to instrument-level ones.
  - A file modulator replaces a default one when their source, destination and transform
    match, **whatever their amount sources**. This leniency is deliberate. It is how
    FluidR3's amount-0 modulators, which carry yet another amount source, switch off the
    default velocity-to-cutoff modulator.
  - A modulator whose source is another modulator's output (a 2.04 link) is ignored and
    listed.
- **Default modulators** (09, D2):
  - velocity to attenuation, 960 cB concave, a gain of 40·log10(v/127) dB: −3.3 dB at
    velocity 105, −7.0 at 85, −11.9 at 64;
  - velocity to filter cutoff, −2400 cents, linear at every velocity, with no velocity
    switch;
  - the controller defaults, held at General MIDI's power-on values: CC7 100, CC10 64, CC11
    127, the pitch wheel centred, every other source 0, the sustain pedal (CC64) included. In
    effect a constant 4.2 dB of attenuation from CC7, and nothing else.

  The sustain pedal is not followed.
- **Exclusive class** (09, D8):
  - When a note starts a zone with a non-zero `exclusiveClass`, every other sounding voice of
    that class ends. The voices the note itself starts are spared.
  - The scope is the whole node, on every channel.
  - An ended voice fades linearly to zero over 6 ms from the new note's sample, as a voice
    taken for the pool does, and its slot is free at once.

### The voice policy

All of this is ticket 06's decision.

- **The pool counts voices**, one per sounding sample: a note on a stereo pair takes two. The
  pool is fixed, sized by the voice limit, and allocated in `prepare()`. A voice that is not
  sounding costs no time in `process()` (D2).
- **When every voice is busy, the node takes whole notes** (D3):
  - It takes every voice one note-on started, and goes on taking notes until the new note has
    the voices it needs.
  - It takes released notes first, starting with the one whose note-off came first. Only if
    none is left does it take held notes, starting with the one whose note-on came first.
  - Every note-on and note-off gets a number in stream order, so there are no ties.
- **A taken voice fades out over 6 ms** (D5):
  - It ramps linearly to zero over 6 ms from the new note's onset.
  - Each slot carries one fading tail beside its sample, so the new note starts at its
    event's offset. If a slot is taken again within 6 ms, the older tail stops at once.
  - A tail does not count against the limit.
  - `prepare()` derives the fade's length in samples from the sample rate.
- **The same key struck again** (D4). A note's key is its channel and key number.
  - A note-on for a key whose previous note still sounds starts a new note beside it, and
    neither takes over nor releases the old one.
  - A note-off ends the oldest held note of its key. A note-off with no held note on its key
    does nothing.
  - A taken note's note-off is dropped and releases no other note. A count per channel and key
    of taken notes still awaiting their note-off is enough, because held notes are taken
    oldest first.
  - The node does not keep an unmatched note-off as a credit for the key's next note-on: it
    takes the bytes as they are.

### Persistence

From ticket 05, D5, plus the rulings below.

- **Saved inside the graph, once, whole:** CONTENT holds the soundfont file's bytes as loaded.
  SETTINGS holds the bank, program and voice limit. An empty node saves and reopens empty,
  with its voice limit. A path or URL is never saved.
- **Reopening** parses the saved bytes with the same parser as a load. A file that no longer
  parses, an absent preset, an invalid voice limit, or malformed or trailing settings fail Open
  with an explicit error and leave the caller's graph alone. Parsed zones, tables that depend
  on the sample rate, and voice state are derived and never saved.
- **The generic `MODULAR_GRAPH 1` container does not change.** CONTENT is hex, two characters
  per byte, and the container caps a file at 256 MiB, so a soundfont over about 128 MiB cannot
  be saved in a graph today. The node sets no limit of its own.
- **The codec checks ports** like the other codecs (`midi` in, `out` audio<2>) and matches the
  exact concrete type.

### What the web build (spec B) consumes

Spec B builds the web surface on this node. It needs the following, all of which tickets 01 to
03 deliver:

- the node constructed or loaded from bytes in memory, parsed once;
- a fresh `Engine` per render on the kept graph, each `prepare()` resetting every voice;
- stereo output;
- voices that end by themselves, so the glue's render-to-a-cap-then-trim tail (contract,
  decision 6) finds the release's end from the output.

Spec B, not this spec, owns `createInstrument`, `render`, the sink and the `MidiFileNode`
import from bytes.

### Decided while writing this spec

Decided by the planning hub under the judgment the user delegated for the map ("All, on your
best judgment"), 2026-11-24.

- **The two-spec cut.** The Modular-side work splits into (A) this spec, the soundfont node,
  and (B) the web build and its render surface, after map tickets 02, 03 and 08. This resolves
  the map's "How the Modular-side work cuts into specs" item, which graduates into this spec.
- **The codec stays out of `primitiveCodecs()`.** If it joined that span, the Windows editor's
  Open would accept a soundfont graph and then fail while drawing it, since the editor has no
  presentation for the node. Giving the editor one would let it run the node under Start/Stop,
  a realtime transport, which pulls in the `MidiBlockNode` held-count fix (06, the planning
  hub's decision). So the codec has its own accessor, which a client that saves soundfont
  nodes passes beside `primitiveCodecs()`. The editor's Open refuses such a graph with the
  ordinary "open: unsupported node kind" error, as it does for device codecs on other
  platforms. Adding the node to the editor is a later, separate ticket, and it carries the
  `MidiBlockNode` fix.
- **The FluidSynth comparison uses the full `FluidR3_GM.sf2`** with bank 0, program 0 selected
  in both renderers. Ticket 09, D10 named "the same extract", but making the extract is not
  this spec's. The node keeps whole files, so both renderers play the same preset, samples and
  generators. Checking that an extract renders the same as the full file is the extract
  tool's job, in spec B or later.
- **The comparison's native renders come from a throwaway harness** under
  `.scratch/soundfont-node/prototypes/`, outside the build, as the map's probes are. No
  existing native client plays a soundfont node, and `midi_cli` is not changed.
- **Filled from precedent:**
  - a failed load keeps the previous content, as `MidiFileNode`'s import does;
  - an empty node saves and reopens empty, as `modular.midi-file/1` does;
  - a load selects the new file's lowest preset and keeps the voice limit.
- **The slices:** tickets 01 to 03 complete the website's route. Ticket 01 is cut to fit one
  session: the ignored list and the skip-and-list rules move to ticket 04.

## Testing Decisions

A good test here drives the node only through what a client can do, and checks only what a
client can observe. A client loads a soundfont, sets the preset and voice limit, reads the
ignored list, catches errors, sends MIDI and reads the samples. Tests never reach into the
reader's tables, the voice slots or the envelope state. A change to the voice's maths that
keeps the output within tolerance must not break a test. Ticket 08's golden test is the one
deliberate exception (Further Notes, **Browser against native**).

**Two seams, both existing:**

1. **The node's authoring face and its rendered output.** A frame-exact MIDI source (a
   `MidiBlockNode` triggered by `TriggerAtNode`, or the test file's own event source) feeds
   the soundfont node, which feeds a two-channel `CaptureSinkNode`. The graph runs through
   `Engine` at several sample rates and odd block sizes. Authoring calls are made on the node,
   and errors are observed where they throw: at load, at a setting, or at `prepare()`. Prior
   art: the MIDI-file tests (exact frames at several rates and block sizes, errors at every
   truncation point) and the node tests (block-size invariance, voice allocation).
2. **The codec round trip.** `saveGraph` and `openGraph` on streams, with the soundfont codec
   passed beside `primitiveCodecs()`. The reopened graph renders the same samples, and corrupt
   content fails Open without replacing the graph. Prior art: the authoring tests' owned
   MIDI content, which "plays independently of its removed import source", and their
   corrupt-content cases.

**Tests build their own soundfonts.** A shared test-side builder writes small SoundFont 2 files
in memory, as the MIDI-file tests write Standard MIDI Files and `WavTestFiles.h` writes WAVs.
No licensed soundfont enters `tests/` or the repository (09, D10). The samples are chosen to
be measurable:

- a sine, for pitch and for the filter's gain at a frequency;
- DC, for envelope levels, pan and attenuation;
- a ramp, for interpolation and loop seams;
- short unlooped samples, for a voice ending at its sample's end.

A miniature of the default piano's layout, taken from ticket 10's measurements, checks ticket
09 D10's list end to end on synthetic samples:

- two mono zones per key range, panned ±500;
- 32 kHz samples rooted by `overridingRootKey`;
- mode 1 loops;
- a preset-level attenuation, and a release per velocity zone;
- a cutoff of 12623 cents with −1000 from the modulation envelope.

**Starting tolerances** (09, D10), which the tickets may tighten:

- pitch within ±1 cent of the derived pitch;
- each envelope stage within one frame of its derived time;
- velocity attenuation within 0.1 dB of 40·log10(v/127);
- the filter's gain at its derived cutoff within 1 dB of the design;
- a voice ends on the frame its envelope has fallen 96 dB;
- output bit-identical across block sizes, as every node's must be.

**Also tested:**

- A second `Engine` on the same graph renders the same samples as the first.
- A node loaded from a path renders after that file is deleted.
- `openGraph` with `primitiveCodecs()` alone refuses a soundfont graph with the unsupported-kind
  error.

**Where the tests live:** a new test executable for the soundfont library (one layer per test
file), and the codec's round trip beside the other persistence tests. Tests are doctest cases
named as sentences stating invariants. Frame offsets are odd or prime so they cannot align with
blocks, and every independent render builds a fresh graph. The focused checks and full suite
are the ones `docs/windows.md` names, which the first ticket updates for the new test entry.

**Not a test: the FluidSynth comparison.** It is the spec's acceptance step (ticket 07). It
needs an external player and a licensed file kept outside the repository, so it runs through a
throwaway harness and records its evidence in its ticket. FluidSynth uses different maths and
never matches sample for sample. "Sounds right" means, for every note of a test clip at
velocities 64 to 127:

- pitch within ±1 cent;
- onset within FluidSynth's 64-frame control block;
- a per-note RMS envelope within ±1 dB in 10 ms windows while the note is within 60 dB of its
  peak, after one global gain offset is removed.

Below velocity 64 the two differ by design (09, D2).

## Out of Scope

- **Spec B, the web build and its render surface:**
  - the Emscripten build;
  - where the module lives and how it reaches the site;
  - the glue (`loadModular`, `createInstrument`, `render`);
  - the output sink;
  - the `MidiFileNode` import from bytes;
  - the browser-against-native check;
  - the in-browser timing of a one-note preview (contract, decision 8).
- **midishare's page**, its Modular engine file, its fallback and its tests. Sessions here do
  not edit the midishare repository.
- **Making the single-preset extract** of the default piano, and where the extract and its MIT
  notice are kept: a tool's job, in spec B or later.
- **The Windows editor:** a palette or presentation entry for the node, and running it under
  Start/Stop. A later ticket that does this also counts held notes per channel and key in
  `MidiBlockNode`.
- **The `MidiBlockNode` held-flag limitation, kept as known:**
  - `MidiBlockNode` keeps one held flag per channel and key, so at Stop, retrigger or a
    file's end it sends one note-off per key. Under the voice policy, a second held note on
    that key and channel then keeps sounding.
  - No render on the site depends on it: every normal note gets its own note-off from the
    file, and a stuck staccato note is at most one per key.
  - It bites only under a realtime transport, and none gives the node one in this spec.
- **Choosing among soundfonts (the map's stretch goal)** beyond what the node already offers:
  loading another file, or selecting another preset.
- **Following program changes or the sustain pedal, per-channel presets, and a drum channel.**
  Each can be added later without changing this node's other behaviour. Drums later are a
  second node fed only channel 10.
- **Reading SoundFont 3.** The node keeps files whole, so a later version can read it with no
  save-format change.
- **24-bit samples** (`sm24`), **linked stereo pairs** (`wSampleLink`), and an effects unit
  (reverb, chorus). The output is dry, and reverb is wiring after the node.
- **Sharing one soundfont's samples between nodes**, and a denser save encoding for large
  soundfonts: kernel and workspace questions for the roadmap, if a DAW needs them.
- **Dynamic polyphony, a node-reported tail length, and realtime playback.**

## Further Notes

- **Order of work.** Tickets 01 to 05 and 08 change the same node, so they run one at a time;
  08 needs only 01. Ticket 06 needs only ticket 01, so it may run beside them once 01 is done.
  Ticket 07 waits for 02 and 03 and for the user's download approval.
- **Acceptance.**
  1. The tickets' own checks: focused checks, the full suite and the code review, per ticket.
  2. Ticket 07: the default piano against FluidSynth, within the thresholds above. The same
     ticket records native timings of a one-note render with the piano loaded and of map
     ticket 02's worst-case clip. midishare needs them to decide whether to render in a Web
     Worker.
  3. **Last: sound parity, by ear.** After ticket 07, and after spec B and midishare's change
     reach the live site, the user judges whether the new piano is close enough to today's for
     the abcjs fallback to go. This is the map's Sound parity item. It is not a ticket, because
     it needs the live site. Expect two differences: the node's output is dry, and its velocity
     curve may spread loudness wider than abcjs's synth.
- **Facts carried forward unconfirmed**, to be confirmed by ticket 07 while it has FluidSynth.
  No decision rests on either.
  - The specification texts on velocity to cutoff. The samples research read the 2.04 text as
    giving the modulator no amount source. FluidSynth's comment on its own default says the
    2.01 text gives it a negative velocity switch.
  - FluidSynth scales a file's `initialAttenuation` by 0.4, for compatibility with EMU10K1
    hardware. The node follows the specification's centibels. On the default piano this
    changes only the overall level, which the comparison's global offset removes.

  No specification PDF is downloaded for these.
- **Browser against native.** Map ticket 02 found that `std::sin` differs between MSVC and
  Emscripten's maths library by one unit in the last place in 1.56% of values. It warned that a
  node working in float maths, or feeding a library result back into its own state, can render
  differently in the browser. This node does both: envelopes, the filter and the LFOs. Map
  ticket 08,
  [How is a browser render checked against a native one?](../midishare-playback/issues/08-browser-render-checked-against-native.md),
  decided that a browser render equals the native one bit for bit, by construction. So this
  node computes no value that reaches its output with the platform's maths library: its
  ticket 08 gives it portable `exp2`, `sin` and `cos` behind the internal header ticket 01
  routes those calls through. The same ticket adds a golden test that pins the rendered bits.
  A maths change updates its values in the same commit, so review sees every change to them;
  this is the one exception to the testing rule above. Spec B runs the test under Node.
- **Licences.** The default piano is FluidR3_GM's (MIT), loaded by the client. The node library
  ships no soundfont, so no licensed asset enters Modular's library or repository (05, D4).
  `FluidR3_GM.sf2`, already on the development machine from ticket 10, stays outside both
  repositories. Fetching it again, if it is ever missing, needs the user's yes at that time.
- **Vocabulary.** Use `CONTEXT.md`'s terms: Modular (never "the engine" for the product),
  primitive node, stored node content, authored graph, Engine, compile, stream time, graph
  persistence. Soundfont, preset, bank, zone, generator, modulator, voice, voice limit and
  release are node-library words, documented in `docs/node-io.md`.
