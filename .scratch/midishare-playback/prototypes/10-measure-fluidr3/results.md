# FluidR3_GM bank 0, program 0: measured

Results for
[Measure the FluidR3_GM piano before it becomes the default](../../issues/10-measure-the-fluidr3-piano.md),
2026-11-24. Throwaway, outside the build.

- **Input.** `FluidR3_GM.sf2` from Debian's `fluid-soundfont` 3.1-6 source
  (`https://sources.debian.org/data/main/f/fluid-soundfont/3.1-6/FluidR3_GM.sf2`),
  148,398,306 bytes, SHA-256
  `74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0`. Both match the
  sources.debian.org API entry. The file is kept outside the repository and was never executed.
- **Reader.** [`PROTOTYPE_measure_preset.py`](PROTOTYPE_measure_preset.py), Python standard
  library only: `python -I PROTOTYPE_measure_preset.py <path to FluidR3_GM.sf2>` (optional
  arguments: bank, program, lowest and highest key; defaults 0 0 36 95). It prints the hydra
  and statistics, never sample data. Sizes are counted from the headers. No extract was
  written.

## The file

`ifil` 2.01, no `sm24` chunk, 189 presets, 193 instruments, 1,418 samples, `smpl` 148,196,112
bytes. INFO `ICOP`: "Frank Wen 2000-2002, 2008; Toby Smithe 2008". `ICMT`: "Licensed under the
MIT License." The full MIT text is not in the file.

## The preset

Bank 0, program 0 is **"Yamaha Grand Piano"**. It has one instrument (148, also named "Yamaha
Grand Piano") with 40 samples: 20 left/right pairs, one pair per key range, all 16-bit at
**32,000 Hz**. No zone sets a modulator, so only the default modulators apply.

### Sizes

"Padded" adds the 46 zero points that must follow each sample. The extract is a whole SF2
file: RIFF and LIST headers, the pdta hydra and an INFO list with room for about 1.2 KB of
MIT notice. Only that INFO part (1,360 bytes) is an estimate.

| Case | Samples | Sample data, raw | Padded | Extract file | Share of 25 MiB |
|---|---|---|---|---|---|
| Every zone (keys 0–108) | 40 | 7,802,940 B | 7,806,620 B | **7,811,742 B (7.45 MiB)** | 30 % |
| Zones covering notes 36–95 (keys 35–99 stay covered) | 30 | 5,376,364 B | 5,379,124 B | 5,383,418 B (5.13 MiB) | 21 % |
| The same, each stereo pair kept as one channel | 15 | 2,688,182 B | 2,689,562 B | 2,692,650 B (2.57 MiB) | 10 % |

Every case fits under 25 MiB (26,214,400 bytes). The full preset leaves 18,402,658 bytes
spare. Today the site downloads 1,396,572 bytes of MP3 for notes 36–95. The three cases are
5.6×, 3.9× and 1.9× that.

### Coverage, repitch, velocity, channels

- **Notes 36–95** are covered by 15 key ranges, which play **30 samples**. Their roots, from
  `overridingRootKey`, are 38, 42, 46, 50, 54, 58, 62, 66, 70, 74, 78, 84, 90, 94 and 99.
  Each root sits at the top of its range. Notes are therefore only ever **pitched down**, by
  at most **5 semitones** (keys 79 and 85, in the two 6-key ranges); the 4- and 5-key ranges
  go down at most 3 or 4. No zone sets `coarseTune`, `fineTune` or `scaleTuning`, and
  `chPitchCorrection` is 0. Every sample header says `byOriginalPitch` 60, so a reader that
  ignored `overridingRootKey` would play every note at the wrong pitch. No zone covers keys
  109–127.
- **Velocity.** The preset has 9 velocity zones (0–64, 65–72, 73–80, 81–88, 89–96, 97–104,
  105–112, 113–120 and 121–127), but all of them play the same instrument and samples. They
  differ only in a preset-level `releaseVolEnv`: −1586, −1200, −884, −617, −386, −182, 0,
  +165 and +316 timecents. That is added to the instrument's 0 and gives a release of
  **0.4 s to 1.2 s**, longer for harder notes. So there is **one recorded velocity layer**.
  Velocity otherwise acts only through the default modulators.
- **Stereo.** Each key range has two zones: a left sample (type 4) panned −500 and a right
  sample (type 2) panned +500. Every `wSampleLink` is 0, which points at sample 0 ("Gun").
  Across the whole file, 970 samples are typed left or right and none has a link that points
  back. The pairs are found only by their matching zones. A pair's channels really differ:
  correlation runs from −0.86 to +0.79. Pairs from key 63 up are often anti-correlated: the
  side energy exceeds the mid energy by up to 11 dB, at keys 95–104.
- **`sm24`:** absent. 16-bit only.

### Loops and the zone table

Every zone has `sampleModes` 1 (loop continuously, also during release). The loop-offset
generators appear on some zones, all with the value 0; no other address offset is used. Loops
sit at the tail of each sample. They run from 293 to 48,606 points (9 ms to 1.52 s) across
the preset, and from 293 to 35,112 points within notes 36–95.

Hold and decay are per zone, in timecents ("-" means unset, the −12000 default). Both
channels of a range share every value except where shown.

| Keys | Root | Samples L/R | holdVolEnv | decayVolEnv | Points | Seconds | Loop points | Loop start |
|---|---|---|---|---|---|---|---|---|
| 0–26 | 26 | 1133/1134 | 3102 | 6429 | 219,502 | 6.86 | 48,606 | 78 % |
| 27–30 | 30 | 1135/1136 | 3214 | 6471 | 195,250 | 6.10 | 35,134 | 82 % |
| 31–34 | 34 | 1137/1138 | 2951 | 6471 | 165,049 | 5.16 | 35,591 | 78 % |
| 35–38 | 38 | 1139/1140 | 1783 | 6590 | 193,116 | 6.03 | 34,164 | 82 % |
| 39–42 | 42 | 1141/1142 | 2786 | 6471 | 155,608 | 4.86 | 35,112 | 77 % |
| 43–46 | 46 | 1143/1144 | 2169 | 6495 | 141,108 | 4.41 | 31,951 | 77 % |
| 47–50 | 50 | 1145/1146 | 1902 | 6386 | 109,546 | 3.42 | 32,175 | 71 % |
| 51–54 | 54 | 1147/1148 | 0 | 6491 | 84,485 | 2.64 | 29,461 | 65 % |
| 55–58 | 58 | 1149/1150 | 0 | 6386 | 87,545 | 2.74 | 34,856 | 60 % |
| 59–62 | 62 | 1151/1152 | −884 | 5830 | 103,887 | 3.25 | 30,855 | 70 % |
| 63–66 | 66 | 1153/1154 | −884 | 5573 | 88,431 | 2.76 | 26,045 | 71 % |
| 67–70 | 70 | 1155/1156 | 583 | 4800 | 71,092 | 2.22 | 19,617 | 72 % |
| 71–74 | 74 | 1157/1158 | 1200 | 4688 | 67,050 | 2.10 | 22,882 | 66 % |
| 75–78 | 78 | 1159/1160 | 702 | 5212 | 75,721 | 2.37 | 28,211 | 63 % |
| 79–84 | 84 | 1161/1162 | −1200 | 5204 | 46,539 | 1.45 | 2,656 | 94 % |
| 85–90 | 90 | 1163/1164 | −1200 | 4151 | 60,331 | 1.89 | 20,047 | 67 % |
| 91–94 | 94 | 1165/1166 | −11959 / - | 3986 | 41,516 | 1.30 | 1,283 | 97 % |
| 95–99 | 99 | 1167/1168 | −11959 | 2854 | 18,116 | 0.57 | 293 | 98 % |
| 100–104 | 104 | 1169/1170 | - | 2716 | 14,107 | 0.44 | 409 | 97 % |
| 105–108 | 108 | 1171/1172 | −11959 | 2265 | 12,736 | 0.40 | 685 | 95 % |

Hold runs from about 1 ms to 6.4 s and decay from 3.7 s to 45 s. Both are set per zone. Zones
for keys 58 and below also set `sustainVolEnv` 1000 themselves, the same value they would
inherit from the global zone.

### Every generator

- **Preset global zone:** `initialAttenuation` 140 cB (14 dB), `reverbEffectsSend` 100
  (10 %).
- **Preset zones (9):** `velRange`, `releaseVolEnv` (above), `instrument` 148.
- **Instrument global zone:** `initialFilterFc` 12623 cents (about 12.0 kHz),
  `modEnvToFilterFc` −1000 cents, `releaseModEnv` 7973 timecents (100 s), `attackVolEnv` −8359
  timecents (8 ms), `sustainVolEnv` 1000 cB (100 dB, so the note decays to silence),
  `releaseVolEnv` 0 (1 s).
- **Instrument zones (40):** `keyRange`, `overridingRootKey`, `sampleModes` 1, `pan` ±500,
  `holdVolEnv`, `decayVolEnv`, `sustainVolEnv` 1000 (keys 58 and below), `sampleID`, and the
  four loop-offset generators at 0 on some zones.
- **Not set anywhere in the preset:** `keynumToVolEnvHold`, `keynumToVolEnvDecay`, both LFOs,
  `delayVolEnv`, every modulation-envelope stage except release, `initialFilterQ`,
  `chorusEffectsSend`, `exclusiveClass`, `coarseTune`, `fineTune`, `scaleTuning`, `keynum`,
  `velocity`, and the start and end address offsets.

## Facts for the planner

- **The filter is audible.** The modulation envelope's stages are all instant except a 100 s
  release, and its sustain is the default full level. So `modEnvToFilterFc` acts as a constant
  −1000 cents for the whole note, and the cutoff sits at about **6.7 kHz** (11623 cents). The
  default velocity-to-cutoff modulator (−2400 cents) lowers it further. By the spec's linear
  curve that gives about 5.0 kHz at velocity 100, 4.2 kHz at 85 (the velocity of the MP3
  generator script), 3.4 kHz at 64 and 2.6 kHz at 40. These figures are derived, not rendered. A node that ignores the filter
  plays this piano brighter than FluidSynth does.
- **FluidSynth departs from the spec here.** Its master-branch source sets a velocity
  *switch* as the second source of that default modulator, so the cutoff drop applies only
  from velocity 64 up (`src/synth/fluid_synth.c`, `default_vel2filter_mod`). Which FluidSynth
  version rendered today's MP3s is unknown.
- **Stereo pairs are two mono zones.** `wSampleLink` is unusable throughout FluidR3, so a node
  that requires valid links would refuse the file. Each zone plays as its own panned mono
  voice: 2 voices per note. Keeping one channel per pair is the only safe mono route, because
  summing L+R would cancel much of the anti-correlated upper register.
- **Rate.** Every sample is 32 kHz, and every pitch change is 0 to −5 semitones. Against
  Modular's global rate the phase increment is `2^(cents/1200) · 32000 / rate`, below 1 at
  44.1 or 48 kHz, so playback always interpolates up.
- **Three presets share the samples.** 0:1 "Bright Yamaha Grand" and 0:3 "Honky Tonk" use
  their own instruments but play all 40 of these samples. Adding them to the extract costs only
  hydra bytes, a few KB. This matters for the stretch goal.
- **A single-preset extract keeps** 1 preset (a global zone and 9 velocity zones), 1
  instrument (a global zone and 40 zones), 40 samples, no modulators, and an INFO list that
  should carry the full MIT notice, because the file's own `ICMT` only names the licence.

## Fit and what each case costs in sound

- **Every zone, 7.45 MiB:** nothing lost. It is the preset as the bank defines it.
- **Notes 36–95, 5.13 MiB:** nothing within the roll's range. Keys 35–99 stay covered, and
  keys outside them go silent: notes outside the roll's range that a clip's ABC might hold
  (**unconfirmed** whether midishare allows them).
- **36–95 with one channel per pair, 2.57 MiB:** the stereo image is lost. Each note is one
  microphone's side of the recording rather than the two hard-panned sides, so the tone
  changes too. Mixing the two sides down instead of choosing one would thin the upper
  register, where they are anti-correlated.
