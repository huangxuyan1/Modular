# 02: The piano's tone: loops, velocity, the filter and the modulation envelope

**What to build:** Notes now loop, and their loudness and brightness follow velocity, through
the default modulators and the piano's low-pass filter. A held note on a looped zone sustains
and keeps looping through its release, as every zone of the default piano asks. A softer note
is quieter and darker, never brighter. A miniature of the default piano's layout, built in the
test from ticket 10's measurements, plays ticket 09 D10's list end to end. See the
[spec](../spec.md).

**Blocked by:** 01: Tracer: a loaded soundfont plays notes at their pitch through the volume
envelope.

**Status:** done

## Checklist

- [x] Loops replace ticket 01's interim rule:
  - `sampleModes` 0 and 2 play to the sample's end.
  - 1 loops for the voice's whole life, its release included.
  - 3 loops while the note is held, then plays through the loop's end to the sample's end.
- [x] Start, end and loop points are the header's plus the zone's address-offset generators:
  the fine offset plus 32,768 times the coarse one.
- [x] While looping, interpolation takes its neighbours across the loop's seam, so points
  beyond the loop are never read. A ramp sample shows no step at the seam.
- [x] Loops shorter than 32 points, or closer than 8 points to an end, play as given.
- [x] A loop outside its sample, or an empty loop, plays unlooped. Listing it is ticket 04's.
- [x] A voice on a mode 1 loop ends only through its envelope's 96 dB floor (ticket 06, D1).
- [x] The velocity-to-attenuation default modulator (960 cB, concave) gives a gain within
  0.1 dB of 40·log10(v/127) at several velocities: −3.3 dB at 105, −7.0 at 85, −11.9 at 64.
- [x] The controller defaults are held at General MIDI's power-on values, which comes to a
  constant 4.2 dB of attenuation from CC7 100 and nothing else. Controllers in the MIDI stream,
  the sustain pedal included, change nothing.
- [x] The total of `initialAttenuation` and the modulators is clamped to 0–1440 cB, so no voice
  is louder than its sample.
- [x] The filter:
  - It is the specification's two-pole resonant low-pass. Its gain at the derived cutoff is
    within 1 dB of the design, at Q 0 cB. Reading `initialFilterQ` is ticket 04's.
  - The cutoff is `initialFilterFc` plus `modEnvToFilterFc` times the modulation envelope,
    plus the velocity-to-cutoff default modulator: −2400 cents, linear at every velocity, with
    no velocity switch.
- [x] The modulation envelope (delay, attack, hold, decay, sustain, release) drives
  `modEnvToFilterFc`. Its stages are within one frame of their derived times. Its pitch depth
  is ticket 04's.
- [x] The miniature default piano plays end to end:
  - two mono zones per key range, panned ±500;
  - 32 kHz samples rooted by `overridingRootKey`;
  - mode 1 loops;
  - a preset-level 140 cB attenuation, and a release per velocity zone added to the
    instrument's;
  - an instrument cutoff of 12623 cents with −1000 from a modulation envelope whose only slow
    stage is a 100 s release.

  A note's pitch, image, level, cutoff and release time all come out as derived, at two graph
  rates.
- [x] Output stays bit-identical across block sizes, and `process()` allocates nothing.
- [x] `docs/node-io.md` records loops, velocity, the default modulators, held controllers and
  the filter, and drops ticket 01's loop interim.
- [x] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/soundfont-node-02-pianos-tone`, cut from a freshly
fetched `origin/main` at `c6246ee`. Before the push it was rebased onto `53d38e5` (ticket 06,
which touched the same headers and `docs/node-io.md`) and then onto `4db75a1` (spec B's
planning). Both rebases were clean. `main` moved to `043113e` (web build 01: MIDI times in
double, and no floating-point contraction) just as the branch was pushed. That was merged
in, and the full suite ran 9/9 again in Debug and Release.

- *What was built.*
  - **The reader** now reads each sample header's loop points. A zone's start, end and loop
    points are the header's plus its address offsets: the fine offset plus 32768 times the
    coarse one. The start and end are kept within the header's sample. A loop that is empty
    or lies outside the points the zone plays gives the zone an empty loop, so it plays
    unlooped.
  - **Loops.**
    - Mode 1 loops through the release, and mode 3 while held. Modes 0 and 2, and any value
      other than 1 and 3, play to the sample's end.
    - While a voice loops, interpolation follows the loop's last point with its first, and
      the position wraps by `fmod`, which is exact.
    - Ticket 01's interim loop rule and its test are gone.
  - **Default modulators**, in `nodes/src/SoundFontNode.cpp`:
    - velocity to attenuation, `-400 log10(v / 127)` cB;
    - CC7 held at 100, the same curve: 41.5 cB;
    - the total with `initialAttenuation` clamped to 0–1440 cB;
    - velocity to cutoff, `-2400 (128 - v) / 128` cents.
  - **The filter.**
    - A state-variable low-pass (the trapezoidal "SVF"), whose response is the bilinear
      transform's: the cutoff is exact, DC passes unchanged, and modulation is well behaved.
    - At Q 0 cB it is a Butterworth response, `1/Q = sqrt(2)`, 3 dB down at the cutoff.
    - It sits before the amplifier, as in the specification's synthesis model.
    - Its coefficients follow the cutoff frame by frame, rounded to whole cents, so they are
      recomputed only when the cutoff moves by a cent. The cutoff is capped at 0.45 of the
      graph's rate.
  - **The modulation envelope** shares the volume envelope's stage timing (`Timeline`,
    `Envelope::settle`). Its attack, decay and release are linear in level, and its sustain is
    a decrease in 0.1 % steps. Its level times `modEnvToFilterFc` moves the cutoff.
  - **The maths seam.** `SoundFontMaths.h` gains `cos`. The filter's tangent is
    `sin / cos`, so spec ticket 08's planned portable `exp2`, `sin` and `cos` cover it. The
    seam's comment now records map ticket 08's decision.
- *Tests* (`tests/test_soundfont.cpp`, CTest `soundfont_tests`: 59 doctest cases and 9,058
  assertions).
  - **Loops:**
    - modes 0 and 2 play to the end;
    - a mode 1 loop sounds through its release to the 96 dB floor, and a held one ends only
      at its decay's floor;
    - mode 3 plays on to the sample's end from the derived loop position;
    - the seam: a looped ramp equals the same ramp written out loop after loop, bit for bit.
      The subcases are a short loop and a loop near the sample's ends;
    - bad and empty loops play unlooped;
    - address offsets move by fine plus 32768 times coarse.
  - **Level:**
    - velocity's `40 log10(v / 127)`, within 0.1 dB at 105, 85 and 64;
    - CC7's 4.2 dB, with no other controller in the stream changing anything;
    - the 1440 cB clamp.
  - **Filter:**
    - −3.01 dB at the derived cutoff, and the two-pole slope an octave either side, at 44.1
      and 48 kHz;
    - velocity to cutoff at eight velocities across 64, each darker and quieter than the last.
  - **The modulation envelope:**
    - plateau levels, and its release halfway;
    - every stage boundary within one frame, by comparing two renders that agree until the
      stage takes effect. The attack's length and linear shape are pinned this way: twice the
      attack with twice the depth matches until the shorter attack ends.
  - **The miniature default piano**, at 44.1 and 48 kHz: pitch within 0.001 cent, image,
    level within 0.004 dB, cutoff, and release end within one frame.
  - **Ticket 01's tests** now carry the default modulators' gain, and its interpolation test
    checks the ramp's slope, which a low-pass keeps.
  - **The busy font** now loops in both modes, filters, modulates and has a sample that runs
    out while held, for the block-size and allocation tests.
- *Red.* Each slice was written test-first and seen failing. Afterwards these mutations were
  each caught, then restored:
  - a coarse multiplier of 32767;
  - no seam wrap in interpolation;
  - a velocity switch at 64;
  - a modulation release that always starts from 1;
  - an attack that jumps to 1.
- *Checks, on the final tree.*
  - Script focused checks: `-Target soundfont_tests -TestRegex '^soundfont_tests$'`, and
    after each rebase `-TestRegex '^(soundfont_tests|authoring_tests)$'`.
  - Full suite through the script: Debug 9/9 and Release 9/9 CTest entries. These include
    both `desktop` tests and kernel purity.
  - `python scripts/check_docs.py`: 0 problems.
  - `git diff --check`: clean.
  - Nothing under `kernel/` or `workspace/` changed.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at `1e2f211`,
  with a clean working tree.
  - **Standards: no hard violations.**
    - Fixed:
      - Sample indices became `std::size_t`.
      - The voice's `gain` comment now says what it holds.
      - `cutoff` became `baseCutoff`.
      - `toneGainDb` reuses `amplitude`.
      - The duplicated generator insertion was removed.
      - The renders' helper became `firstDifferentToneFrame`.
      - `kPi` became `std::numbers::pi`.
    - Kept, as judgement calls:
      - Test-case names stay on one line, as in ticket 01.
      - The two envelopes' parameters and the `Stage` switches. They differ only in how decay
        and release move the level, and a shape type per envelope would add indirection to
        every frame.
      - `Envelope::level`'s two uses, which its comment states.
  - **Spec.**
    - Fixed: the modulation envelope's attack length and shape, and its release halfway, are
      now tested.
    - Kept and recorded below: the start and end clamp, the 0.45 cap, the loop check against
      the points played, the attack linear in cents, and the 128 divisor.
- *Judgement calls.*
  - The start and end are kept within the header's sample, so an offset cannot read another
    sample's points.
  - A loop counts as outside its sample when it leaves the points the zone plays after its
    offsets, not only the header's sample. A start offset past the loop's start therefore
    plays unlooped.
  - The filter is Butterworth at Q 0 cB, −3 dB at the cutoff, as FluidSynth reads the
    specification's "not resonant". It sits before the amplifier.
  - The cutoff is capped at 0.45 of the graph's rate, as FluidSynth caps it. Above the cap
    the gain no longer follows the design. There is no bypass at 20 kHz, so a voice with the
    default `initialFilterFc` is filtered at about 19.7 kHz.
  - The filter's coefficients follow the cutoff in whole cents. A cent moves the gain at the
    cutoff by about 0.005 dB, and it keeps the filter cheap during the default piano's
    100 s modulation release.
  - The modulation envelope rises linearly in level, so linearly in cents, as FluidSynth's
    does. The 2.04 text calls that attack "convex", linear in Hz. On the default piano the
    attack is about 1 ms.
  - Velocity to cutoff divides by 128, as ticket 09 D2's table was derived and as
    FluidSynth normalizes a linear source. The concave curve uses 127, as this ticket states.
    So velocity 127 lowers the cutoff by 18.75 cents.
  - The lower attenuation clamp, at 0 cB, cannot be reached until ticket 05's file
    modulators can subtract, so only the 1440 cB ceiling is tested.

**Open notes for the planning hub.**

- *Linear interpolation darkens high partials.*
  - Reading a 32 kHz sample between its points loses about 1.6 dB at 7.5 kHz, and about
    4 dB at 12 kHz.
  - The miniature piano's cutoff check cancels the loss by comparing against the same piano
    with its filter opened.
  - FluidSynth interpolates with four points by default, so ticket 07 may find the node
    slightly darker above the piano's cutoff.
  - The spec bounds interpolation only by pitch. Raising its order would be a later
    ticket's choice.
- *For ticket 07 to confirm against FluidSynth:*
  - velocity to cutoff's 128 divisor;
  - the modulation attack being linear in cents;
  - the 0.45 cap.
- *For spec ticket 08:*
  - This ticket's calls through the seam are `exp2` (timecents and the cutoff), `log10` (the
    concave curve), `exp10` (centibels to gain), and `sin` and `cos` (pan and the filter).
  - `fmod`, `ceil` and `llround` are called directly, since they are exact.
- *For ticket 04's ignored list:* the reader marks a loop played unlooped by leaving the
  zone's loop empty. Listing it needs the reason kept alongside.
