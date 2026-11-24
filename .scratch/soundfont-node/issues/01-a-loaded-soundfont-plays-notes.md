# 01: Tracer: a loaded soundfont plays notes at their pitch through the volume envelope

**What to build:** A client loads a SoundFont 2 file into a new soundfont node, from bytes in
memory or from a path, and sends it MIDI. Each note sounds at its correct pitch, through its
volume envelope, in stereo, and ends by itself. This is the narrowest complete path through
the node: the reader, the node's authoring face, a plain voice and note matching, observed
through a rendered output. Ticket 02 makes it sound like the piano, and ticket 03 completes
the pool. See the [spec](../spec.md) for every rule cited here.

**Blocked by:** None (can start immediately).

**Status:** done

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

- [x] A new library in the node library, beside the MIDI library, holds the reader and the
  node. It links only the header-only node library. A new test executable joins CTest, and
  `docs/windows.md`'s description of the suite is updated to match.
- [x] A shared test-side builder writes small SoundFont 2 files in memory: presets,
  instruments, global zones at both levels, generators, sample headers and 16-bit samples. No
  licensed soundfont enters the repository.
- [x] The reader parses a well-formed SoundFont 2 file from bytes in memory, and from a path
  as a desktop convenience. It reads the RIFF form, the `ifil` version, the sample data and
  the nine hydra lists, skipping an `sm24` chunk.
- [x] A load checks the whole file, every preset, and throws `soundfont: <what went wrong>`
  for:
  - a malformed RIFF structure, or a truncated chunk (tested at every byte boundary, as the
    MIDI-file tests do);
  - a hydra list of the wrong size;
  - an index or a sample pointing outside its list or the sample data;
  - no preset.
- [x] A load throws `soundfont: compressed samples (sf3) are not supported` for an `ifil`
  major version 3 or a sample's compressed bit. Any other major version than 2 throws too.
- [x] Zone resolution:
  - Global zones apply at both levels.
  - A note plays every instrument zone whose key and velocity ranges, at both levels, contain
    it.
  - Preset-level values add to instrument-level ones. Instrument-only generators at the preset
    level are ignored.
  - Final values are clamped to the specification's ranges.

  Tests cover a preset-level attenuation added to an instrument's, a preset-level release per
  velocity zone, and an out-of-range value that is clamped.
- [x] The node's face:
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
- [x] The voice:
  - Every matching zone plays as its own mono voice.
  - Pitch comes from the root key (`overridingRootKey`, else the header's original pitch, 255
    or illegal meaning 60) and the sample's rate against the graph's rate, within ±1 cent, at
    two graph rates.
  - Playback is interpolated, with `initialAttenuation`, and equal-power pan: centred is 3 dB
    down on each side, ±500 fully one-sided.
  - A stereo pair, a left zone at −500 and a right zone at +500, plays as two voices with its
    image kept.
- [x] The volume envelope:
  - Delay, attack, hold, decay, sustain and release in timecents and centibels, shaped as the
    spec says.
  - Each stage within one frame of its derived time.
  - A note-off starts the release, and a release that starts below full level ends sooner.
- [x] A voice ends on the frame its envelope has fallen 96 dB below its peak after its attack,
  held or released, or when it passes its sample's end. After the last voice the output is
  exact zeros.
- [x] Note matching:
  - Velocity is each note-on's byte, and velocity 0 is a note-off.
  - Every channel, channel 10 included, plays the one preset. Program changes, controllers and
    pitch bend do nothing.
  - A key struck again gets a new note beside the old one.
  - A note-off ends its key's oldest held note.
  - A note-off with no held note on its key does nothing. Two parts in unison, on one channel
    or two, both sound.
- [x] Output is bit-identical at several odd block sizes, and `process()` allocates nothing.
- [x] `prepare()` never reads the soundfont again. A second `Engine` on the same graph renders
  the same samples as the first, and a node loaded from a path renders after the file is
  deleted.
- [x] `SineSynthNode`'s comment is reworded so it stops reading as a rule for every instrument
  in the library (ticket 05, D1).
- [x] `docs/node-io.md` gains a soundfont-node section: library, ports, settings, loading and
  errors, what the voice honours so far, and the interim rules above.
- [x] Nothing changes under `kernel/` or `workspace/`. Kernel purity and the full suite pass
  (`docs/windows.md`), and `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/soundfont-node-01-tracer`, cut from a freshly
fetched `origin/main` at `28c95d6`, which had not moved by the push.

- *What was built.* `modular_soundfont` (`nodes/src/SoundFont.cpp`, `SoundFontNode.cpp`) links
  only `modular_nodes`. The reader is `SoundFont` (`parse(bytes)`, `read(path)`). The node is
  `SoundFontNode`, with ports `midi` and `out`, `load`, `setPreset` and `setVoiceLimit`.
  `nodes/src/SoundFontMaths.h` holds every transcendental call that reaches the output
  (`exp2`, `exp10`, `log10`, `sin`), wrapping `std::` for now, per the coordinator's map
  ticket 08 ruling. Time and frame conversion use `double`. The test-side builder is
  `tests/SoundFontTestFiles.h`. The tests are in `tests/test_soundfont.cpp`, which is CTest's
  `soundfont_tests`: 45 doctest cases and 8,957 assertions.
- *Seams.* Every test drives the node's authoring face and reads its rendered output. Renders
  run through `Engine` into a two-channel `CaptureSinkNode`, with MIDI at exact frames. They
  run at 44.1 and 48 kHz and at blocks of 1, 7, 127, 128, 509 and 512, and each render builds
  a fresh graph. Event offsets are odd or prime. Errors are observed where they throw: at
  load, at a setting, or at `prepare()`.
- *Red.* Each behaviour was written test-first. Several tests passed on their first run because
  earlier work already covered them. For those, a temporary mutation of the node confirmed
  that each fails when its behaviour breaks. The mutations, each then restored:
  - root key, rate ratio and interpolation;
  - newest-first note-off;
  - events handled at the block's start, which breaks block-size identity;
  - a `std::vector` in `process()`, which breaks the allocation count;
  - a voice ending one frame late, which keeps its slot busy.

  Two characterisation tests passed on their first run with no mutation tried: the loop
  interim rule and the check of every preset. The first describes a rule the code meets by
  not reading loops. The second covers checks that are not per preset.
- *Checks, on the final tree.*
  - Script focused check: `-Target soundfont_tests -TestRegex '^soundfont_tests$'`.
  - Full suite through the script: Debug 9/9 and Release 9/9 CTest entries. These include
    both `desktop` tests, `graph_editor_app` and `graph_editor_gesture`, and kernel purity.
  - `python scripts/check_docs.py`: 92 files, 0 problems.
  - `git diff --check`: clean.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at `fe3e864`,
  with a clean working tree.
  - Standards, fixed:
    - Events moved from frame 0 and round frames to odd frames.
    - Long code lines were wrapped. Test-case names stay on one line, as in the other
      suites.
    - `hasPreset`, which nothing called, was removed.
    - `PresetNumber` gained `==`.
    - `hasGlobal` and `global` became one `std::optional`.
    - Header record sizes now come from the size table.
    - Renames: `Plan` to `PreparedZone`, `notes_` to `note_ons_`, and the local `free` to
      `idle`.
  - Standards, kept, all judgement calls:
    - `SoundFont::read` mirrors `MidiFile::read` and its messages on purpose.
    - The zone count is computed in both `select()` and `noteOn()`.
    - Several test helpers read a scaled level.
    - The generator tables index by number, with comments.
    - `matches` stays a free function rather than a member of the public `Zone`.
  - Spec, fixed:
    - Added a test of the loop interim rule (modes 1 and 3).
    - Added a test that a load checks a preset it does not select.
    - Bytes after the RIFF chunk are now ignored instead of refused. The spec refuses only "a
      malformed RIFF structure", and this way the default piano cannot fail on padding.
    - The unused `sample_rate_` was removed.
    - The `SoundFontMaths.h` comment now treats map ticket 08 as open.
    - The ticket numbers in `docs/node-io.md` were corrected.
    - A voice's slot is now free from the frame after its last sound, not one frame later.
  - Spec, kept: the rate-0 refusal. See the open notes.
- *Judgement calls.*
  - The volume envelope's delay also holds the sample back.
  - Interpolation is linear, with the sample silent past its last point.
  - A missing `smpl` chunk counts as no sample data.
  - `keynum`, `velocity` and `overridingRootKey` clamp to −1…127, where −1 means unset.
  - Generators after a zone's terminal generator are ignored. A repeated generator keeps its
    last value.
  - A later zone at either level with no terminal generator is ignored.

**Open notes for the planning hub.**

- *A held note whose voices end by themselves (ticket 03).* Today such a note vanishes from
  the held set. A later note-off on its key then releases the key's next held note, which
  belongs to a newer note-on. The interim pool-full rule has the same effect: a note it does
  not start leaves a spare note-off, which can release a later note of that key early.
  Ticket 03's count of taken notes, one per channel and key, covers the second case. The first
  needs the same idea: count notes that end while held.
- *Rate-0 samples (ticket 04).* A zone whose sample header gives a sample rate of 0 is
  refused at load. This guard is this ticket's own, and the spec does not list it. Ticket 04
  should decide whether to skip and list such zones alongside the ROM and empty ones.
- *FluidR3's RIFF size* was not checked against its file length. Trailing bytes are now
  ignored, so the answer no longer matters for loading.
