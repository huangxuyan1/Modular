# 02: The piano's tone: loops, velocity, the filter and the modulation envelope

**What to build:** Notes now loop, and their loudness and brightness follow velocity, through
the default modulators and the piano's low-pass filter. A held note on a looped zone sustains
and keeps looping through its release, as every zone of the default piano asks. A softer note
is quieter and darker, never brighter. A miniature of the default piano's layout, built in the
test from ticket 10's measurements, plays ticket 09 D10's list end to end. See the
[spec](../spec.md).

**Blocked by:** 01: Tracer: a loaded soundfont plays notes at their pitch through the volume
envelope.

**Status:** ready-for-agent

## Checklist

- [ ] Loops replace ticket 01's interim rule:
  - `sampleModes` 0 and 2 play to the sample's end.
  - 1 loops for the voice's whole life, its release included.
  - 3 loops while the note is held, then plays through the loop's end to the sample's end.
- [ ] Start, end and loop points are the header's plus the zone's address-offset generators:
  the fine offset plus 32,768 times the coarse one.
- [ ] While looping, interpolation takes its neighbours across the loop's seam, so points
  beyond the loop are never read. A ramp sample shows no step at the seam.
- [ ] Loops shorter than 32 points, or closer than 8 points to an end, play as given.
- [ ] A loop outside its sample, or an empty loop, plays unlooped. Listing it is ticket 04's.
- [ ] A voice on a mode 1 loop ends only through its envelope's 96 dB floor (ticket 06, D1).
- [ ] The velocity-to-attenuation default modulator (960 cB, concave) gives a gain within
  0.1 dB of 40·log10(v/127) at several velocities: −3.3 dB at 105, −7.0 at 85, −11.9 at 64.
- [ ] The controller defaults are held at General MIDI's power-on values, which comes to a
  constant 4.2 dB of attenuation from CC7 100 and nothing else. Controllers in the MIDI stream,
  the sustain pedal included, change nothing.
- [ ] The total of `initialAttenuation` and the modulators is clamped to 0–1440 cB, so no voice
  is louder than its sample.
- [ ] The filter:
  - It is the specification's two-pole resonant low-pass. Its gain at the derived cutoff is
    within 1 dB of the design, at Q 0 cB. Reading `initialFilterQ` is ticket 04's.
  - The cutoff is `initialFilterFc` plus `modEnvToFilterFc` times the modulation envelope,
    plus the velocity-to-cutoff default modulator: −2400 cents, linear at every velocity, with
    no velocity switch.
- [ ] The modulation envelope (delay, attack, hold, decay, sustain, release) drives
  `modEnvToFilterFc`. Its stages are within one frame of their derived times. Its pitch depth
  is ticket 04's.
- [ ] The miniature default piano plays end to end:
  - two mono zones per key range, panned ±500;
  - 32 kHz samples rooted by `overridingRootKey`;
  - mode 1 loops;
  - a preset-level 140 cB attenuation, and a release per velocity zone added to the
    instrument's;
  - an instrument cutoff of 12623 cents with −1000 from a modulation envelope whose only slow
    stage is a 100 s release.

  A note's pitch, image, level, cutoff and release time all come out as derived, at two graph
  rates.
- [ ] Output stays bit-identical across block sizes, and `process()` allocates nothing.
- [ ] `docs/node-io.md` records loops, velocity, the default modulators, held controllers and
  the filter, and drops ticket 01's loop interim.
- [ ] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.
