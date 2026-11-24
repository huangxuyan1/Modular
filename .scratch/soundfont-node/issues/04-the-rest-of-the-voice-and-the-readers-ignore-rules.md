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

**Status:** ready-for-agent

## Checklist

- [ ] The modulation LFO is honoured: `delayModLFO`, `freqModLFO`, `modLfoToPitch`,
  `modLfoToFilterFc` and `modLfoToVolume`.
- [ ] The vibrato LFO is honoured: `delayVibLFO`, `freqVibLFO` and `vibLfoToPitch`. Both LFOs
  are shaped as the specification defines and advance per sample.
- [ ] `initialFilterQ` sets the filter's resonance, and the gain at cutoff matches the design
  within 1 dB at several Q values.
- [ ] Tuning, all within ±1 cent:
  - `coarseTune`, `fineTune`, `scaleTuning` and the sample header's pitch correction enter the
    pitch as the spec's formula says;
  - the `keynum` generator replaces the key;
  - `modEnvToPitch` adds the modulation envelope's pitch sweep.
- [ ] Key scaling: `keynumToVolEnvHold`, `keynumToVolEnvDecay`, `keynumToModEnvHold` and
  `keynumToModEnvDecay` scale their stages per key, within one frame.
- [ ] The `velocity` generator replaces a note's velocity within its zone.
- [ ] A load builds a read-only ignored list, in the errors' short form, which
  `process()` never sees:
  - effects sends (`soundfont: reverb and chorus sends are ignored`, or its final wording);
  - link modulators;
  - zones skipped because their sample is in ROM or empty;
  - loops played unlooped because they lie outside their sample or are empty.

  A miniature of the default piano lists its reverb send.
- [ ] A zone whose sample is in ROM or empty is skipped and listed. This replaces ticket 01's
  interim refusal; the rest of the file plays.
- [ ] Unknown, unused or reserved generators, instrument-only generators at the preset level,
  and modulators the specification calls illegal stay ignored, unlisted, as the specification
  says.
- [ ] Output stays bit-identical across block sizes, and `process()` allocates nothing.
- [ ] `docs/node-io.md` records the whole generator table the node honours, the ignored list
  and the skip rules, and drops ticket 01's remaining interims.
- [ ] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.
