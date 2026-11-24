# Measure the FluidR3_GM piano before it becomes the default

Type: task
Status: resolved
Blocked by: 07

## Question

Research recommends FluidR3_GM's acoustic grand piano (bank 0, program 0) as the default,
extracted into a piano-only SoundFont 2 file
([Which soundfonts ship, in what format, under what licence and at what size?](07-which-samples-ship.md)).
No published source gives that preset's size or layout, and the site's host serves at most
25 MiB per file. The default cannot be chosen, and the format subset cannot be sized, until
someone reads the file.

**Download, which needs the user's approval through the planning hub:** `FluidR3_GM.sf2`,
148,398,306 bytes (141.5 MiB), from Debian's `fluid-soundfont` 3.1-6 source on
`sources.debian.org`. MIT licence. It is kept outside this repository and never committed.

Checklist:

1. Read the file with a throwaway script under `.scratch/midishare-playback/prototypes/`,
   outside the build, using only a standard library. No SoundFont editor is installed.
2. For bank 0, program 0, report:
   - the bytes of sample data the preset uses, in three cases: every zone; only zones that
     cover MIDI notes 36–95; and the same with each stereo pair kept as one channel;
   - how many samples cover notes 36–95, and the largest repitch in semitones;
   - velocity layers, stereo pairs or mono, sample rates, and whether `sm24` data is present;
   - `sampleModes` and loop lengths;
   - every generator the preset and its instrument set, including the envelope,
     `keynumToVolEnv*`, attenuation, pan and filter;
   - any modulators of its own, beyond the defaults.
3. Say whether some case fits under 25 MiB, and what it would cost in sound.

The answer feeds the default-soundfont decision and
[Which part of the SoundFont format does the node read?](09-soundfont-format-subset.md).
If no case fits, the research's fallback, FreePats' Upright Piano KW "small" (CC0), gets the
same measurement.

## Answer

Resolved 2026-11-24. The user approved the one download through the planning hub. The file
matched Debian's size and SHA-256, was read by a throwaway standard-library reader and stays
outside the repository. Full figures, the zone table and every generator:
[FluidR3_GM bank 0, program 0: measured](../prototypes/10-measure-fluidr3/results.md).

- **Every case fits under 25 MiB.** A single-preset SF2 extract with every zone is
  7,811,742 bytes (7.45 MiB, 30 % of the limit). Keeping only the zones that cover notes
  36–95 gives 5.13 MiB; also keeping one channel per stereo pair gives 2.57 MiB. Sample data
  is 7,802,940, 5,376,364 and 2,688,182 bytes before the 46-point padding. So the fallback
  needs no measurement. Today's MP3s for notes 36–95 are 1.33 MiB.
- **Cost in sound.** The full preset loses nothing. The 36–95 cut silences keys below 35 and
  above 99. One channel per pair loses the stereo image and changes the tone. Summing the two
  sides instead would thin the upper register, where they are anti-correlated.
- **Layout.** Preset "Yamaha Grand Piano": one instrument, 20 key ranges, each a left and a
  right mono zone panned ±500. Notes 36–95 use 30 samples, with roots at the top of each range,
  so every note is pitched down by 0 to 5 semitones. All samples are 32 kHz 16-bit, and there
  is no `sm24`. There is one recorded velocity layer: the 9 velocity zones change only the
  release, 0.4–1.2 s. Every zone loops continuously (`sampleModes` 1), with loops of 293–35,112
  points (9 ms–1.1 s) in 36–95.
- **Generators.** Preset level: `initialAttenuation` 140 cB, `reverbEffectsSend` 100, and
  `releaseVolEnv` per velocity zone. Instrument global zone: attack 8 ms, sustain 100 dB (each
  note decays to silence), release 1 s, `initialFilterFc` 12623 cents, `modEnvToFilterFc` −1000
  and `releaseModEnv` 100 s. Per zone: key range, `overridingRootKey`, pan, hold (1 ms–6.4 s)
  and decay (3.7–45 s). There are no `keynumToVolEnv*`, LFOs, `exclusiveClass`, tuning
  generators or modulators of its own; only the defaults apply.
- **For the subset ticket.** `overridingRootKey` is required, because every sample header says
  60. The filter is audible: its effective cutoff is about 6.7 kHz before the default
  velocity-to-cutoff modulator, and FluidSynth applies that modulator only from velocity 64 up.
  `wSampleLink` is 0 for every sample in FluidR3, so a reader that requires valid stereo links
  would refuse the file.
- **For the stretch goal.** Presets 0:1 "Bright Yamaha Grand" and 0:3 "Honky Tonk" play the
  same 40 samples, so adding them to an extract costs a few KB of hydra.

Choosing the default is the planning hub's call.
