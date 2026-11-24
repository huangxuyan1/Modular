# 01: Tracer: a loaded soundfont plays notes at their pitch through the volume envelope

**What to build:** A client loads a SoundFont 2 file into a new soundfont node, from bytes in
memory or from a path, and sends it MIDI. Each note sounds at its correct pitch, through its
volume envelope, in stereo, and ends by itself. This is the narrowest complete path through
the node: the reader, the node's authoring face, a plain voice and note matching, observed
through a rendered output. Ticket 02 makes it sound like the piano, and ticket 03 completes
the pool. See the [spec](../spec.md) for every rule cited here.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

**Interim rules, which later tickets replace.** Each is stated so it is tested, documented and
not mistaken for the end state.

- **Loops (ticket 02):** every zone plays its sample once, to its end, whatever its
  `sampleModes`.
- **Pool full (ticket 03):** a note-on that finds too few free voices for all its zones is not
  started.
- **Unplayable zones (ticket 04):** a zone whose sample is in ROM or empty is refused at load
  with an error. Ticket 04 relaxes this to skip-and-list.
- **Silent ignoring (ticket 04):** generators and modulators the node does not yet read are
  ignored silently, effects sends included. The default piano carries `reverbEffectsSend`, so
  it must load. There is no ignored list yet.

## Checklist

- [ ] A new library in the node library, beside the MIDI library, holds the reader and the
  node. It links only the header-only node library. A new test executable joins CTest, and
  `docs/windows.md`'s description of the suite is updated to match.
- [ ] A shared test-side builder writes small SoundFont 2 files in memory: presets,
  instruments, global zones at both levels, generators, sample headers and 16-bit samples. No
  licensed soundfont enters the repository.
- [ ] The reader parses a well-formed SoundFont 2 file from bytes in memory, and from a path
  as a desktop convenience. It reads the RIFF form, the `ifil` version, the sample data and
  the nine hydra lists, skipping an `sm24` chunk.
- [ ] A load checks the whole file, every preset, and throws `soundfont: <what went wrong>`
  for:
  - a malformed RIFF structure, or a truncated chunk (tested at every byte boundary, as the
    MIDI-file tests do);
  - a hydra list of the wrong size;
  - an index or a sample pointing outside its list or the sample data;
  - no preset.
- [ ] A load throws `soundfont: compressed samples (sf3) are not supported` for an `ifil`
  major version 3 or a sample's compressed bit. Any other major version than 2 throws too.
- [ ] Zone resolution:
  - Global zones apply at both levels.
  - A note plays every instrument zone whose key and velocity ranges, at both levels, contain
    it.
  - Preset-level values add to instrument-level ones. Instrument-only generators at the preset
    level are ignored.
  - Final values are clamped to the specification's ranges.

  Tests cover a preset-level attenuation added to an instrument's, a preset-level release per
  velocity zone, and an out-of-range value that is clamped.
- [ ] The node's face:
  - `midi` in (`event<midi>`), `out` (`audio<2>`).
  - It can be constructed empty or with content. Empty, it is not ready, and `prepare()`
    throws asking for a soundfont.
  - A failed load throws and keeps the previous content.
  - A successful load selects the lowest bank, then the lowest program in it, and keeps the
    voice limit.
  - The preset setting throws for a bank and program the file lacks.
  - The voice-limit setting accepts 1 to 65535, defaults to 256, and throws otherwise.
    `prepare()` throws when the limit is below the most voices one note of the selected preset
    can start.
- [ ] The voice:
  - Every matching zone plays as its own mono voice.
  - Pitch comes from the root key (`overridingRootKey`, else the header's original pitch, 255
    or illegal meaning 60) and the sample's rate against the graph's rate, within ±1 cent, at
    two graph rates.
  - Playback is interpolated, with `initialAttenuation`, and equal-power pan: centred is 3 dB
    down on each side, ±500 fully one-sided.
  - A stereo pair, a left zone at −500 and a right zone at +500, plays as two voices with its
    image kept.
- [ ] The volume envelope:
  - Delay, attack, hold, decay, sustain and release in timecents and centibels, shaped as the
    spec says.
  - Each stage within one frame of its derived time.
  - A note-off starts the release, and a release that starts below full level ends sooner.
- [ ] A voice ends on the frame its envelope has fallen 96 dB below its peak after its attack,
  held or released, or when it passes its sample's end. After the last voice the output is
  exact zeros.
- [ ] Note matching:
  - Velocity is each note-on's byte, and velocity 0 is a note-off.
  - Every channel, channel 10 included, plays the one preset. Program changes, controllers and
    pitch bend do nothing.
  - A key struck again gets a new note beside the old one.
  - A note-off ends its key's oldest held note.
  - A note-off with no held note on its key does nothing. Two parts in unison, on one channel
    or two, both sound.
- [ ] Output is bit-identical at several odd block sizes, and `process()` allocates nothing.
- [ ] `prepare()` never reads the soundfont again. A second `Engine` on the same graph renders
  the same samples as the first, and a node loaded from a path renders after the file is
  deleted.
- [ ] `SineSynthNode`'s comment is reworded so it stops reading as a rule for every instrument
  in the library (ticket 05, D1).
- [ ] `docs/node-io.md` gains a soundfont-node section: library, ports, settings, loading and
  errors, what the voice honours so far, and the interim rules above.
- [ ] Nothing changes under `kernel/` or `workspace/`. Kernel purity and the full suite pass
  (`docs/windows.md`), and `/code-review` runs on the branch's diff.
