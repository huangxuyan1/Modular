# 04: The rest of the voice, and the reader's ignore rules

**What to build:** A soundfont other than the default piano plays as its author meant. Its
vibrato and tremolo LFOs, filter resonance, tuning, envelope key scaling and modulation
envelope pitch sweeps are all honoured. A file the node only partly reads still loads:

- a zone it cannot play is skipped;
- a bad loop plays unlooped;
- what the node ignores, such as effects sends, is listed after the load where a client can
  read it.

See the [spec](../spec.md) (ticket 09, D1 and D3).

**Blocked by:** 02: The piano's tone: loops, velocity, the filter and the modulation envelope.

**Status:** done

## Checklist

- [x] The modulation LFO is honoured: `delayModLFO`, `freqModLFO`, `modLfoToPitch`,
  `modLfoToFilterFc` and `modLfoToVolume`.
- [x] The vibrato LFO is honoured: `delayVibLFO`, `freqVibLFO` and `vibLfoToPitch`. Both LFOs
  are shaped as the specification defines and advance per sample.
- [x] `initialFilterQ` sets the filter's resonance, and the gain at cutoff matches the design
  within 1 dB at several Q values.
- [x] Tuning, all within ±1 cent:
  - `coarseTune`, `fineTune`, `scaleTuning` and the sample header's pitch correction enter the
    pitch as the spec's formula says;
  - the `keynum` generator replaces the key;
  - `modEnvToPitch` adds the modulation envelope's pitch sweep.
- [x] Key scaling: `keynumToVolEnvHold`, `keynumToVolEnvDecay`, `keynumToModEnvHold` and
  `keynumToModEnvDecay` scale their stages per key, within one frame.
- [x] The `velocity` generator replaces a note's velocity within its zone.
- [x] A load builds a read-only ignored list, in the errors' short form, which
  `process()` never sees:
  - effects sends (`soundfont: reverb and chorus sends are ignored`, or its final wording);
  - link modulators;
  - zones skipped because their sample is in ROM or empty;
  - loops played unlooped because they lie outside their sample or are empty.

  A miniature of the default piano lists its reverb send.
- [x] A zone whose sample is in ROM or empty is skipped and listed. This replaces ticket 01's
  interim refusal; the rest of the file plays.
- [x] Unknown, unused or reserved generators, instrument-only generators at the preset level,
  and modulators the specification calls illegal stay ignored, unlisted, as the specification
  says.
- [x] Output stays bit-identical across block sizes, and `process()` allocates nothing.
- [x] `docs/node-io.md` records the whole generator table the node honours, the ignored list
  and the skip rules, and drops ticket 01's remaining interims.
- [x] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — planning hub rulings, binding this ticket.** Decided by the planning hub under
the user's delegation, from the open notes of tickets 01, 02 and 08.

1. *Rate-0 zones* (ticket 01's note). A zone whose sample rate is 0 is skipped and listed, like
   ROM and empty zones, not refused.
2. *Bad loops* (ticket 02's note). A loop that plays unlooped because it is empty or out of
   range is listed with its reason.
3. *A guard for the portable-maths rule* (ticket 08's open item). A source check in the style
   of `kernel_purity` fails if a soundfont node source other than `nodes/src/SoundFontMaths.h`
   calls a maths-library transcendental (`exp`, `exp2`, `pow`, `log`, `log10`, `sin`, `cos`
   or `tan`, in `std::` or C form). `floor`, `ceil`, `fmod`, `llround`, `isfinite`, `ldexp`,
   `frexp` and `sqrt` are allowed. `docs/windows.md` names it.
4. *The golden test.* A change to the golden clip's rendered bits updates its values in the
   same commit, with the same values in Debug and Release. The clip is extended where the new
   paths are not otherwise pinned.

**2026-11-24 — implemented.** Branch `claude/soundfont-node-04-rest-of-the-voice`, cut from a
freshly fetched `origin/main` at `f57d9ce`. Before the push it was rebased onto `1febc11`
(web build 05, the `web` CI job, and 06, the render surface's hardening). Only
`docs/windows.md` conflicted, in its CI section; both sides were kept.

- *What was built.*
  - **The reader** (`nodes/src/SoundFont.cpp`).
    - A zone whose sample is in ROM, empty or has a sample rate of 0 is skipped, so the rest of
      the file plays. Ticket 01's interim refusal and its test are gone.
    - Each instrument zone's points are placed once, at load. A loop outside the points the
      zone plays, or one whose end is not after its start, is dropped and its reason kept.
    - Modulators are read only to find links: a source that is the link controller (127), or a
      destination with bit 15 set.
    - The header's pitch correction is read.
    - A load leaves `SoundFont::ignored()`, which the node's `ignored()` returns: each kind
      once, in a fixed order, in the errors' short form:
      - `soundfont: reverb and chorus sends are ignored`;
      - `soundfont: link modulators are ignored`;
      - `soundfont: zones playing a sample in rom are skipped`, and likewise `an empty
        sample` and `a sample with no sample rate`;
      - `soundfont: loops outside their sample play unlooped`, and `soundfont: empty loops
        play unlooped`.
  - **The voice** (`nodes/src/SoundFontNode.cpp`).
    - Pitch in cents is `scaleTuning (key - root) + 100 coarseTune + fineTune + correction`,
      plus `modEnvToPitch`, `modLfoToPitch` and `vibLfoToPitch` times their sources. A zone
      whose pitch moves recomputes its increment every frame.
    - `keynum` and `velocity` replace the note's key and velocity within the zone: in its
      pitch, key scaling and default modulators, not in choosing zones.
    - Key scaling: each envelope's hold and decay are derived at the note-on, so each envelope
      now carries its own timeline.
    - Two LFOs, the specification's triangle, each with its delay and step, advanced every
      frame from the note-on. The modulation LFO moves the pitch, the cutoff (in whole cents,
      with the modulation envelope) and the volume, in centibels.
    - `initialFilterQ` sets the filter's damping and its gain at DC; see the judgement calls.
    - Every new transcendental call goes through `SoundFontMaths.h`.
  - **The guard**: `scripts/check_soundfont_maths.cmake`, CTest's `soundfont_portable_maths`.
    It reads `nodes/src/SoundFont*` and `nodes/include/modular/nodes/SoundFont*`, less the
    maths header. It blanks string literals, strips comments, and fails on a call of a banned
    function in `std::`, global or C form, with its `f` and `l` variants. It also fails when
    the maths header or the sources are missing. `docs/windows.md` and `docs/web.md` name it;
    it runs under both presets.
  - **The golden test.** The golden font gains a second preset, program 1, whose clip pins the
    LFOs, resonance, tuning, the header's correction, `keynum`, the `velocity` generator and
    key scaling, with a note taken while both LFOs run, at 44.1, 48 and 22.05 kHz. The first
    clip's values did not change in any commit, so no earlier path changed its bits.
  - **Docs**: `docs/node-io.md` records the generator table the node honours, the ignored list
    and the skip rules, and the rest of the voice, and drops ticket 01's interims.
- *Tests* (`tests/test_soundfont.cpp`, CTest `soundfont_tests`: 85 doctest cases and 9,813
  assertions, from 67).
  - **New:**
    - the ignored list and skip rules: zones in ROM, empty or rateless; sends at either level;
      a send of zero or below; link modulators; unknown, reserved and misplaced generators and
      illegal modulators, unlisted; every preset listed; the list's life with the node's
      content; the miniature piano lists its reverb send;
    - tuning, `keynum` and `velocity`;
    - key scaling of both envelopes;
    - `modEnvToPitch`, both LFOs' pitch, and the modulation LFO's cutoff and volume;
    - `initialFilterQ` at the cutoff and at DC;
    - the second golden clip.
  - **Changed:** bad loops are listed with their reason, and a zone that does not loop lists
    none; a ROM sample no zone plays is not listed; the busy font takes every new path, so the
    block-size and allocation tests cover them.
- *Red.* Each slice was written test-first and seen failing: the listing and skip tests, then
  tuning, `keynum` and `velocity`, key scaling, the pitch and LFO tests, and `initialFilterQ`
  (its 0 cB cases passed, as at Q 0 the design is ticket 02's). The tests that passed at once
  were checked by mutation; each mutation was caught, then restored:
  - sends listed whatever their amount, and sends listed when non-zero rather than positive:
    the send tests;
  - a modulator to a destination past 60 listed as a link: the unlisted test;
  - key scaling's clamp removed, or its sign flipped: the key-scaling tests.

  An 11-mutant sweep removed each new path in turn: tuning, `scaleTuning`, `keynum`,
  `velocity`, resonance, key scaling, the triangle's rise, the LFO's cutoff and volume terms,
  `modEnvToPitch` and the vibrato LFO. Every mutant changed the second golden clip's bits and
  failed a tolerance test of its own; none changed the first clip.

  The guard flagged planted `std::sin`, bare `sin`, `::exp2`, `powf`, `std::log10 (`,
  `std::tanh`, `cosf`, `::exp10`, `std::beta` and `expintf`, also after a `/*` inside a
  string. It passed `maths::` calls, allowed functions, comments and strings, and failed with
  its maths header missing.
- *Checks, on the final tree*, after the rebase and the review fixes:
  - Script focused checks: `-Target soundfont_tests -TestRegex
    '^(soundfont_tests|soundfont_portable_maths)$'`.
  - Full suite through the script: Debug 12/12 and Release 12/12 CTest entries, with both
    `desktop` tests, kernel purity and the new `soundfont_portable_maths`. The golden values
    are the same in both. One Debug run failed `soundfont_extract` because this session edited
    a file between the two runs it compares, which stamps the tree's dirty flag; the rerun,
    with the tree left alone, passed.
  - The `web` presets, by hand with emsdk 6.0.13 and its Node 24.19.0, as `docs/web.md`
    describes: 10/10 CTest entries. `soundfont_tests` passes all 85 cases under Node, both
    golden clips with the native values.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
  - Nothing under `kernel/` or `workspace/` changed.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at `b6e9d51`,
  after the rebase, with a clean working tree.
  - **Standards.**
    - Fixed:
      - A note-off at frame 48,000, on a block boundary at 48 kHz, moved to an odd frame.
      - Lines over 90 columns were wrapped.
      - The guard did not ban `exp10`, which glibc and musl provide, nor C++17's special
        functions, which its comment claimed. Both are banned now. It stripped comments
        before strings, so a `/*` in a string hid what followed; strings now go first.
      - Comments: the CTest entry's, the guard's, and the envelope comment `prepare()` kept
        after the decay moved into `scaleToKey`.
      - A negative send was listed though it clamps to no send; only a positive one is now,
        with a test.
      - Each `Lfo` carries its own step, so an LFO and its step no longer travel in pairs.
      - The reader's two "local, else global" lookups are one helper.
      - Renames: `found` to `listed`, `kListed` to `kListingEntries`, and a local `At` each to
        `Phase` and `Window`. An `optional` replaces the count used as a "no skip" sentinel.
    - Kept, as judgement calls:
      - `scaleToKey` derives the sustain levels again from their generators, as `prepare()`
        does. Keeping them in the plan would add two fields for two divisions.
      - The key-scaling cases stay positional structs.
  - **Spec.** It found nothing missing and rulings 1 to 4 met.
    - Fixed:
      - The modulation envelope's key-scaling test pinned bits, which the Testing Decisions
        allow only the golden test. It now measures each scaled stage within one frame, by
        two renders that agree until that stage takes effect, at 32 kHz, keynum and the
        hold's lower clamp included.
      - `docs/node-io.md` overstated how far the filter's resonance follows FluidSynth. It
        now states the reading exactly; see the judgement calls.
    - Kept, as defensible extensions: a modulator whose output feeds another is listed as a
      link too; a send of zero is not listed; the guard bans more than the ruling names.
- *Judgement calls.*
  - **The ignored list** has one entry per kind, in a fixed order, with no counts and no
    names, in the errors' short form. It covers the whole file, every preset and instrument,
    not only the selected preset, since a load checks the whole file.
  - **Sends** are listed when a zone at either level gives one above 0; a send of 0 or below
    is no send once clamped.
  - **Links**: a source that is the link controller (127, not a CC), or a destination with
    bit 15 set. A link as an amount source is illegal, so it is ignored unlisted.
  - **Bad loops** are checked against the points the zone plays, after its offsets, and
    listed only for a zone that would loop (`sampleModes` 1 or 3). "Empty" is an end not
    after the start, a reversed loop included; "outside" is a point outside.
  - **`initialFilterQ`.** Its Q is `10^(dB / 20) / sqrt(2)`, FluidSynth's, so the response
    has FluidSynth's shape at every Q, and Q 0 cB is ticket 02's Butterworth response. Its
    gain at DC falls by half the decibels, the specification's text, so it is unity at
    0 cB. So the gain at the cutoff is `dB - 3` above DC, where the 2.04 text says `dB`, and
    the level is a constant 1.5 dB below FluidSynth's at every Q.
  - **The LFOs' frequency** is absolute cents with 440 Hz at 6900: 0 cents is 8.1758 Hz, the
    specification's 8.176. Their delays are whole frames, rounded, as the envelopes' are.
  - **`modLfoToVolume`** is a gain on top of the clamped attenuation, as the spec lists it, so
    a positive depth can lift a voice above its sample at the LFO's peak.
  - **Key scaling** keeps the scaled stage within its range, -12000 to 5000 timecents for a
    hold and -12000 to 8000 for a decay, as FluidSynth does.
  - **A `velocity` generator of 0** plays the zone at velocity 0's 96 dB of attenuation.
    FluidSynth ignores a 0; the specification allows 0 to 127.
  - **A pitch that moves** is recomputed every frame through `exp2`, only for zones with a
    pitch depth. A zone without one keeps its note-on increment, so the default piano pays
    nothing for it.
  - **The golden extension** is a second preset and clip rather than a change to the first,
    so the first clip's unchanged values show that no earlier path changed its bits.

**Open notes for the planning hub.**

- *For ticket 07, against FluidSynth:* the resonance reading above. The shape should match at
  every Q, with a level 1.5 dB below FluidSynth's, which the comparison's global offset
  absorbs; the default piano sets no Q.
- *For ticket 05:* the reader reads modulators only to find links (`readLinks` in
  `nodes/src/SoundFont.cpp`). File modulators extend it there. `exclusiveClass` is read and
  clamped but not yet honoured.
