# Measure the FluidR3_GM piano before it becomes the default

Type: task
Status: open
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
