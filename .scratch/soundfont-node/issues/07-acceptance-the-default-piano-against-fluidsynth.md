# 07: Acceptance: the default piano against FluidSynth

**What to build:** Evidence that the node plays the default piano right. FluidSynth is the
player that made the renders the site plays today and the most widely used SoundFont 2 player.
It renders the same MIDI clip from the same file and preset at the same sample rate, and the
node's render must match it within ticket 09 D10's thresholds. The ticket also records native
timings, which midishare needs to decide whether to render in a Web Worker. It changes no
product code. A failure is a finding to fix in the node, as its own ticket, not here. See the
[spec](../spec.md) (Testing Decisions and Further Notes).

**Blocked by:**
- 02: The piano's tone: loops, velocity, the filter and the modulation envelope;
- 03: When every voice is busy: whole notes taken, released first, oldest first, with a fade;
- **the user's approval of the FluidSynth download.** The planning hub decided, under the
  user's delegation, that the user approves it when this ticket is reached (map ticket 09,
  planning hub decision 2).

**Status:** done

## Checklist

- [x] **Before any download**, ask the user, through the planning hub, to approve one official
  FluidSynth 2.x release, naming the file, its source and its size. It is kept outside both
  repositories and never committed.
- [x] Use `FluidR3_GM.sf2`, already on the development machine from map ticket 10 and kept
  outside the repository. If it is missing, fetching it again needs the user's yes at that
  time; the earlier approval does not cover a re-fetch.
- [x] Compare on the full file, with bank 0, program 0 ("Yamaha Grand Piano") selected in both
  renderers. Checking that an extract renders the same is the extract tool's job, in spec B
  or later.
- [x] Write a throwaway harness under `.scratch/soundfont-node/prototypes/`, outside the
  build, as the map's probes are.
  - It renders the clip natively through a MIDI file node and the soundfont node.
  - It writes or compares the samples.
  - `midi_cli` and the product code do not change. Rendered audio and licensed files are
    never committed.
- [x] The test clip covers every key range of the preset at velocities 64 to 127, across its
  velocity zones. Each note is held long enough to measure its sustain, then its release.
  The clip is written by the harness.
- [x] FluidSynth renders the same MIDI bytes and file at the same sample rate, with reverb and
  chorus off.
- [ ] For every note, within the thresholds:
  - pitch within ±1 cent;
  - onset within FluidSynth's 64-frame control block;
  - a per-note RMS envelope within ±1 dB in 10 ms windows while the note is within 60 dB of
    its peak, after one global gain offset is removed.

  Below velocity 64 the two differ by design. Compare there only if FluidSynth's default
  modulator can be replaced with the node's.

  **Not met; the causes are found.** Onset and pitch hold on every note at both rates. Level
  holds on 7 of 180 notes at each rate. Two differences from FluidSynth explain every
  failure, and the node passes every note once both are removed. Fixing them is the node's
  follow-up, not this ticket's (Comments, and the open notes). Below velocity 64 the
  comparison ran, against a copy of the file that makes FluidSynth apply the node's
  velocity-to-cutoff curve.
- [x] Confirm, or record as unconfirmed, the two facts the spec carries:
  - the velocity-to-cutoff default's amount source in the specification texts, as FluidSynth's
    source describes them;
  - FluidSynth's 0.4 scaling of `initialAttenuation`.

  No specification PDF is downloaded for this.
- [x] Record native timings at 44.1 and 48 kHz, in Release:
  - loading the default piano;
  - a one-note render with it loaded, as the note preview does (0.25 s at velocity 105, then
    the release);
  - map ticket 02's worst-case clip.
- [x] If ticket 04 has landed, record the full file's ignored list too.
- [x] Record the evidence in this ticket: versions, commands, the clip, the numbers per note,
  and any failure, with its cause if found.
- [ ] The last acceptance item stays with the user and is not this ticket's: judging sound
  parity by ear on the live site, after spec B and midishare's change.

  It stays with the user.

## Comments

**2026-11-24 — implemented.** Branch `claude/soundfont-node-07-fluidsynth-acceptance`, cut from
a freshly fetched `origin/main` at `bc33cf8` and rebased before the push onto `22c5bd8` (web
build 08), which touches none of its files. No product code changed. Every number per note
is in [the results](../prototypes/07-fluidsynth-acceptance/results.md).

**In short.** Onset and pitch meet their thresholds on every note. Level does not: 7 of 180
notes pass at each rate. Two differences from FluidSynth explain every failure, and a build
of the node with both removed passes every note at both rates:

1. **The volume envelope's range.** FluidSynth's decay and release fall 96 dB over their
   stage's time; the node's fall 100 dB, as the spec says (09, D7). The node falls 4 % faster,
   so it is up to 2.6 dB quieter by the 60 dB floor.
2. **Velocity to cutoff.** FluidSynth applies none at all: it zeroes its own default
   modulator. The node lowers the cutoff by up to an octave at velocity 64 (09, D2), so its
   treble notes are up to 4.3 dB quieter there.

The open notes below recommend the follow-ups.

- *The download.* Approved by the user in the planning hub on 2026-11-24 ("Yes, approve it"),
  for an official FluidSynth 2.x release for the piano comparison.
  - The latest 2.x release, v2.6.3 ("fluidsynth 2.6.3", published 2026-11-22), of the
    official repository `FluidSynth/fluidsynth` (not a fork), checked with
    `gh api repos/FluidSynth/fluidsynth/releases` and `.../releases/latest`.
  - Its Windows x64 asset, `fluidsynth-v2.6.3-win10-x64-cpp11.zip`: 2,732,393 bytes, SHA-256
    `5522023a247de0e7d27c7678733d5abe7a591ef3b5fa400a520b8b50e0ae3791`, equal to the
    asset's GitHub `digest`. Downloaded with `gh release download` into a new, empty folder
    outside both repositories, `%USERPROFILE%\fluidsynth\2.6.3\`, and unzipped there.
  - It needed nothing more: its `bin\` holds `fluidsynth.exe`, `libfluidsynth-3.dll`,
    `sndfile.dll` and `SDL3.dll`. `fluidsynth --version` reports 2.6.3, float samples and the
    cpp11 OS abstraction.
  - It ran only from that folder, with explicit file arguments, offline (`-F`), with no MIDI
    driver (`-n`), no shell (`-i`) and no audio device. Nothing was installed, no environment
    variable changed, and no administrator rights were used. No FluidSynth file is
    committed.
  - FluidSynth's source was read, as text through the GitHub API, at the tag `v2.6.3`, and
    for one function at older tags. Nothing else was downloaded.
- *Inputs.* `FluidR3_GM.sf2` from map ticket 10, outside the repository: 148,398,306 bytes,
  SHA-256 `74594e8f…bc1cb0`, as ticket 10 recorded. Both renderers play bank 0, program 0
  ("Yamaha Grand Piano") of the full file: the harness calls `setPreset(0, 0)`, and the clip
  opens with bank select 0 (CC0 and CC32) and program change 0 for FluidSynth, which the node
  ignores. The timings load web build 04's single-preset extract instead, as midishare will:
  7,812,018 bytes, SHA-256 `9609e512…5be0d08`.
- *The harness*, under
  [`prototypes/07-fluidsynth-acceptance/`](../prototypes/07-fluidsynth-acceptance/run.ps1),
  outside the build. Nothing under `kernel/`, `workspace/`, `nodes/` or `apps/` changed.
  - `PROTOTYPE_make_clips.py` writes the clips. `PROTOTYPE_render.cpp` renders a clip through
    `TriggerAt(0) -> MidiFileNode -> SoundFontNode -> StereoSinkNode` in 512-frame blocks and
    writes float WAV; it also prints the ignored list and times the render surface's
    `Instrument`. `PROTOTYPE_compare.py` (numpy) compares note by note.
    `PROTOTYPE_add_modulator.py` writes the modified copy used below velocity 64. The
    `patches/` rebuild the harness from a patched copy of the sources, for the diagnosis.
  - `run.ps1` does it all: it compiles the harness with MSVC and CMake's Release flags
    (`/O2 /Ob2 /DNDEBUG /MD`), renders, compares and times. It writes only into its `-Out`
    folder, outside the repository:

    ```powershell
    powershell -NoProfile -ExecutionPolicy Bypass -File run.ps1 -Out <folder>
      -FluidSynth <...\fluidsynth-v2.6.3-win10-x64-cpp11\bin> -SoundFont <FluidR3_GM.sf2>
      -Extract <the extract> -Patch <patches\diagnose-envelope-96db.patch>,<...no-velocity-cutoff.patch>,<...both.patch>
    ```

  - FluidSynth's render, at each rate: `fluidsynth -n -i -R 0 -C 0 -r <rate> -O float -T wav
    -o synth.lock-memory=0 -F <out.wav> <font.sf2> <clip.mid>`. Reverb and chorus are off;
    every other setting is FluidSynth's default: gain 0.2, 256 voices, four-point
    interpolation.
- *The clip*, `clip.mid`: one key in each of the preset's 20 key ranges (21, 28, 32, 36, 40,
  44, 48, 52, 56, 60, 64, 68, 72, 76, 81, 87, 92, 97, 102 and 106: the middle of each range,
  but A0 for the lowest) at velocities 64, 72, 80, 88, 96, 104, 112, 120 and 127, one in each
  velocity zone from 64 up: 180 notes on channel 1, one at a time.
  - Each is held 1.28 s, long enough for the treble notes to reach their decay, then released
    for 1.28 s, longer than any of the preset's releases. The clip lasts 462 s.
  - Time runs in 1 ms ticks, and every event falls on a multiple of 640 ms. That is a whole
    number of FluidSynth's 64-frame blocks at both 44.1 and 48 kHz. FluidSynth's player acts
    on events only at block boundaries, so on this grid it starts and releases every note the
    same number of frames after its event, and each note is held equally long in both
    renderers.
  - `low.mid` repeats the layout at velocities 16, 32, 48 and 63: 80 notes.
- *How a note is measured* (`PROTOTYPE_compare.py`), at 44.1 and 48 kHz:
  - **Onset:** each render's first non-zero frame in the note's slot.
  - **Pitch:** both renderers read the same sample points, so a pitch difference shows as a
    lag that grows with time. While the note is held, 2,048-frame windows every 25 ms are
    cross-correlated with FluidSynth's and the peaks fitted with a line. The first window,
    which holds the attack, is searched within 8 frames of the onset difference, and each
    later one within 5 frames of the last, so a periodic waveform cannot slip by a period. A
    first version searched ±96 frames in every window; it slipped on the treble keys and
    reported up to 4 cents.
  - **Level:** the RMS of both channels in 10 ms windows from each render's own onset. The
    windows counted are those where FluidSynth's is within 60 dB of its peak. One global
    offset, the median difference over every counted window of every note at both rates, is
    removed before the ±1 dB test.

**Results, velocities 64 to 127, the node as built against FluidSynth.**

| Threshold | 44.1 kHz | 48 kHz |
| --- | --- | --- |
| Onset within 64 frames | 180 of 180: FluidSynth 22 frames later | 180 of 180: 18 frames later |
| Pitch within ±1 cent | 180 of 180: −0.0013 to +0.0032 cents | 180 of 180: −0.0015 to +0.0042 cents |
| Level within ±1 dB | 7 of 180; 4,006 of 27,230 windows outside | 7 of 180; 3,967 of 27,222 windows outside |

- **Onset.** The node's first sound comes 43 frames after the event at 44.1 kHz and 47 at
  48 kHz: the default `delayVolEnv` of −12000 timecents (0.98 ms), which the node honours to
  the frame. FluidSynth counts a delay in whole 64-frame blocks, rounded down
  (`NUM_BUFFERS_DELAY` in `fluid_voice.c`), so its default delay is none. Its first sound
  comes 65 frames after the event at both rates: its player starts the note one block after
  the event's frame, and its attack's first frame is silent. The waveforms are offset by
  the same amount: 19.2 to 21.0 frames at 44.1 kHz and 15.0 to 17.0 at 48 kHz.
- **Pitch** agrees to within 0.005 cents on every note: the node and FluidSynth derive the
  same increments.
- **The global offset** is +3.93 dB, the node louder. FluidSynth's master gain of 0.2
  (−13.98 dB), its 0.4 scaling of the piano's 140 cB (+8.40 dB) and its filter's gain at
  Q 0 (+1.51 dB) predict +4.08 dB.
- **Level fails** on every note but key 92 at velocities 80 to 127. Each note's worst window
  is 0.76 to 5.24 dB below FluidSynth's, never above. Per key (44.1 kHz; 48 kHz is within
  0.1 dB of it):

  | Key | Worst window (dB) | Level 20–100 ms in, velocity 64 / 127 (dB) | Release fall, node / FluidSynth |
  | --- | --- | --- | --- |
  | 21 | −2.48 | +0.02 / +0.07 | 1.0416 |
  | 28 | −2.46 | +0.02 / +0.08 | 1.0421 |
  | 32 | −2.38 | −0.02 / +0.07 | 1.0420 |
  | 36 | −2.32 | −0.08 / +0.06 | 1.0409 |
  | 40 | −2.20 | +0.01 / +0.08 | 1.0405 |
  | 44 | −1.97 | +0.00 / +0.07 | 1.0404 |
  | 48 | −2.09 | −0.08 / +0.06 | 1.0403 |
  | 52 | −2.23 | +0.00 / +0.07 | 1.0404 |
  | 56 | −2.08 | −0.14 / +0.05 | 1.0401 |
  | 60 | −2.16 | −0.13 / +0.05 | 1.0389 |
  | 64 | −2.02 | −0.27 / +0.02 | 1.0416 |
  | 68 | −1.91 | −0.15 / +0.04 | 1.0412 |
  | 72 | −1.82 | −0.10 / +0.05 | 1.0334 |
  | 76 | −1.74 | −0.43 / −0.00 | 1.0371 |
  | 81 | −1.82 | −0.27 / +0.00 | 1.0345 |
  | 87 | −1.43 | −0.08 / +0.03 | 1.0347 |
  | 92 | −1.16 | −0.35 / −0.09 | none: 60 dB down before the release |
  | 97 | −1.76 | −0.87 / −0.18 | none: 60 dB down before the release |
  | 102 | −3.15 | −2.22 / −0.32 | none: 60 dB down before the release |
  | 106 | −5.24 | −4.28 / −0.51 | none: 60 dB down before the release |

**Why the level fails.**

- **The envelope's range.** FluidSynth's decay and release move its envelope linearly from 1
  to 0 over the stage's time (`fluid_voice_update_param`, an increment of −1/count per
  64-frame block), and `fluid_rvoice_calc_amp` turns it into
  `fluid_cb2amp(FLUID_PEAK_ATTENUATION * (1 - env))` with `FLUID_PEAK_ATTENUATION` 960 cB.
  So a full stage falls 96 dB; the node's falls 100 dB. Measured: the node's release falls
  1.0405 times as fast as FluidSynth's (median of 288 note renders, 1.029 to 1.046), against
  100/96 = 1.0417. The gap grows by 0.04 dB for every dB fallen: in the bass, whose 1.28 s is
  all hold, it appears in the release and reaches −2.5 dB by the 60 dB floor; in the treble,
  whose hold is short, it starts in the decay.
- **Velocity to cutoff.** FluidSynth applies none (the facts below). The node takes
  `2400 (128 − v) / 128` cents off the cutoff: velocity 64 halves the piano's 6.7 kHz. That
  costs little level where a note's energy lies below the cutoff, and much in the treble: 20
  to 100 ms in, key 106 at velocity 64 is 4.28 dB quieter, key 102 2.22 dB and key 97
  0.87 dB, and from key 76 down at most 0.43 dB. The waveforms' normalised correlation with
  FluidSynth's is at least 0.961 with the curve and 0.9992 without it.
- **The diagnosis.** Each patch in `patches/` rebuilds the harness from a patched copy of the
  sources; the checkout's `nodes/` is not touched. `diagnose-envelope-96db` makes a full
  decay or release fall 96 dB over its time instead of 100, and `diagnose-no-velocity-cutoff`
  sets the velocity-to-cutoff depth to 0. "With the node's curve" is FluidSynth on the modified copy described under
  **Below velocity 64**. Notes passing the level test, of 180 at velocities 64 to 127:

  | Node | Against | 44.1 kHz | 48 kHz | Each note's worst window (dB) |
  | --- | --- | --- | --- | --- |
  | As built | FluidSynth | 7 | 7 | −5.24 to −0.76 |
  | 96 dB envelope | FluidSynth | 166 | 165 | −4.36 to +0.57 |
  | No velocity-to-cutoff | FluidSynth | 9 | 9 | −2.64 to −0.77 |
  | Both | FluidSynth | **180** | **180** | −0.54 to +0.56 |
  | As built | FluidSynth with the node's curve | 9 | 9 | −2.64 to −0.77 |
  | 96 dB envelope | FluidSynth with the node's curve | **180** | **180** | −0.63 to +0.57 |

  Onset and pitch pass on every note in every row.
- **What is left is small.** With both differences removed, every window is within 0.63 dB,
  and within 0.70 dB below velocity 64. The largest part is linear interpolation against
  FluidSynth's four-point one (ticket 02's note): it leaves key 106 about 0.6 dB darker even
  when both play the same cutoff. FluidSynth's 64-frame envelope steps and its whole-centibel
  gain table add less. The filter's 1.5 dB lower level (ticket 04's note) is constant and goes
  into the global offset.

**Below velocity 64.** FluidSynth's default modulator cannot be replaced from its command
line, but a file modulator of another identity is evaluated normally.
`PROTOTYPE_add_modulator.py` writes a copy of `FluidR3_GM.sf2` (outside the repository, never
committed) whose piano instrument's global zone carries one modulator: velocity, linear,
negative (`0x0102`), to `initialFilterFc`, amount −2400, with no amount source. FluidSynth
maps it to `−2400 (127 − v) / 128` cents, 18.75 cents above the node's at every velocity. The
node renders the unmodified file; it does not read file modulators yet (ticket 05). On
`low.mid`, 80 notes at velocities 16, 32, 48 and 63:

| Node | Against | 44.1 kHz | 48 kHz | Each note's worst window (dB) |
| --- | --- | --- | --- | --- |
| As built | FluidSynth with the node's curve | 4 | 4 | −2.67 to −0.87 |
| 96 dB envelope | FluidSynth with the node's curve | **80** | **80** | −0.70 to +0.58 |

So below 64 too, the envelope's range is the only difference once FluidSynth plays the node's
curve, and the node's velocity curve, `40 log10(v / 127)`, matches FluidSynth's concave one
down to velocity 16.

**The two facts** (spec, Further Notes; map ticket 09, "Facts the spec must confirm").

1. **The velocity-to-cutoff default's amount source: confirmed as FluidSynth's source
   describes the texts.** At `default_vel2filter_mod` in `src/synth/fluid_synth.c`, its
   comment cites SF2.01, page 53, section 8.4.2, and says the specification gives the
   secondary source a negative direction ("D=1"): a negative velocity switch, `0x0D02`, the
   form FluidR3's amount-0 switch-offs carry. FluidSynth makes it positive so that a
   soundfont overriding it with positive polarity, "Kingston Piano", matches. The 2.04 text
   was not read, so the samples research's reading of it, no amount source, stays
   unconfirmed.

   **And a fact the map did not have: FluidSynth applies no velocity-to-cutoff at all.** In
   `src/synth/fluid_mod.c`, `fluid_mod_get_value` returns 0 for any modulator identical to
   that default ("S. Christian Collins' mod, to stop forcing velocity based filtering"). The
   same code is at the tags `v1.1.11`, `v2.0.0`, `v2.3.0`, `v2.4.0`, `v2.5.7` and `v2.6.3`;
   earlier ones were not read. The measurement agrees (above). So two premises of 09, D2 do
   not hold: the linear curve does not equal FluidSynth's from velocity 64 up, and today's
   renders, if any FluidSynth from 1.1.11 on made them at velocity 85, have the piano's full
   6.7 kHz cutoff, not 4.2 kHz.
2. **FluidSynth's 0.4 scaling of `initialAttenuation`: confirmed.** `src/sfloader/fluid_defsfont.c`
   defines `EMU_ATTENUATION_FACTOR` as `0.4f` and multiplies every `initialAttenuation`
   generator by it when it loads a zone, at preset and instrument level, "to stay compatible"
   with EMU8k/10k hardware. Modulators are not scaled. The global offset agrees, within
   0.15 dB of its prediction (above).

Ticket 02's items for this ticket, from the same source:

- *The 128 divisor:* FluidSynth normalises a linear, negative, unipolar velocity as
  `(127 − v) / 128` (`fluid_mod_transform_source_value`), so the divisor is 128, but its
  numerator is 127 − v, not the node's 128 − v. It matters only for a file modulator, as
  FluidSynth zeroes its default.
- *The modulation attack linear in cents:* confirmed. Its attack adds 1/count to the
  envelope's value every block, and the value multiplies `modEnvToFilterFc` in cents.
- *The 0.45 cap:* confirmed. `fluid_iir_filter_calc` (`fluid_iir_filter_impl.cpp`) clips the
  cutoff at `fluid_hz2ct(0.45 * output_rate)`.
- *Linear interpolation:* within the thresholds on this piano (above).

Ticket 04's notes: the resonance level is the constant above. A `velocity` generator of 0 is
not exercised: the piano sets no `velocity` generator.

**Timings** (native, MSVC Release with CMake's flags, Windows 11 on an AMD Ryzen 7 9800X3D).
`Instrument` is the render surface's own class (`apps/web`), so these are the calls the web
module makes. Loading is the `Instrument`'s construction from bytes already in memory: it
parses and checks the whole file. The renders use an `Instrument` loaded with the extract.
Each figure is a median: of 21 loads of the extract, 5 of the full file, 51 previews and 5
renders of each clip. Ranges span three runs of the harness; the short and ten-minute clips
ran once.

| | 44.1 kHz | 48 kHz |
| --- | --- | --- |
| Loading the extract, 7.8 MB | 2.8–3.1 ms | (the same load) |
| Loading the full file, 148 MB | 15.2–15.9 ms | (the same load) |
| The note preview, the first render after loading | 1.44–1.65 ms | 1.41–1.55 ms |
| The note preview, later renders | 1.30–1.31 ms | 1.40–1.42 ms |
| Map ticket 02's short clip, 11.1 s | 72 ms | 74 ms |
| Map ticket 02's worst-case clip, 261.8 s, 1,644 notes | 2.45–2.65 s | 2.73–3.01 s |
| Map ticket 02's ten-minute clip, 597.6 s | 2.48 s | 2.80 s |

- The preview renders 0.592 s: its 0.25 s, then the release, trimmed at −80 dBFS by the
  render surface.
- The worst case takes 9.4 to 11.5 ms per second of audio, 8.5 to 9.4 times the sine voice's
  288 and 320 ms (map ticket 02). The ten-minute clip of whole-note triads costs less per
  second, with fewer voices sounding at once.

**The full file's ignored list** (ticket 04 has landed): one entry,
`soundfont: reverb and chorus sends are ignored`. Its lowest preset is 0:0.

- *Checks, on the final tree.*
  - `python scripts/check_docs.py`: 0 problems.
  - `git diff --check`: clean.
  - The native suite was not run: nothing that builds changed. The harness is outside the
    CMake build; it compiled and ran with MSVC as above. CI's build path runs the suite,
    since the branch changes files other than Markdown.
  - No TDD: the ticket changes no product code, and its harness is a throwaway (spec ruling
    C3).
- *Review.* REVIEW

**Open notes for the planning hub.**

- *Follow-up 1, needed for the thresholds: the volume envelope's range.* Recommended: a node
  ticket under which decay and release fall 96 dB over their stage's time, FluidSynth's
  `FLUID_PEAK_ATTENUATION`, instead of 100 dB. It changes 09, D7 and the spec's "a stage's
  time is the time for a full 100 dB change", so it is a planning decision first. With it
  alone, 165 to 166 notes of 180 pass; with FluidSynth playing the node's curve, all of them,
  above and below velocity 64. The same ticket should decide the sustain level too.
  FluidSynth reads `sustainVolEnv` as a fraction of the same 96 dB, 0.1 % per centibel, so
  500 cB sustains 48 dB down where the node sustains 50 dB down. The piano does not exercise
  it: its sustain of 1000 cB ends the note. The node's tests of stage times and its golden
  values would change with the ticket.
- *Follow-up 2, a policy choice: velocity to cutoff.* FluidSynth, and so today's sound, has
  none. 09, D2 chose the linear curve partly because it "equals FluidSynth from velocity 64
  up", which does not hold. Two ways on:
  - drop the default velocity-to-cutoff modulator, as FluidSynth does. With follow-up 1, the
    node then passes every threshold against FluidSynth as it is. Softer notes are never
    brighter, because brightness no longer follows velocity. Recommended, since the
    acceptance compares against FluidSynth and today's sound;
  - or keep D2's curve and accept that the treble differs from FluidSynth below velocity
    127, by up to 4.3 dB at velocity 64 on key 106. The thresholds then stay unmet on 14 to
    15 notes per rate even after follow-up 1.

  Either is a reversal or a reaffirmation of D2, for the planning hub or the user. Ticket 05
  should know the outcome: its amount-0 switch-offs only matter while the default exists.
- *No action:* the node's default 1 ms delay (FluidSynth rounds it to nothing), linear
  interpolation (about 0.6 dB on the top key) and the filter's level. All are inside the
  thresholds.
- *For midishare's Web Worker decision* (map ticket 04, decision 12; the timings above).
  Map ticket 02 measured the module at 1.1 to 1.25 times native time. On that basis a page's
  main thread would block for about 2.7 to 3.8 s on the worst-case clip, about 2.7 to 3.5 s
  on ten minutes, and about 0.1 s on an ordinary clip. A preview takes 1.3 to 1.7 ms natively,
  and web build 08 measured 1.5 to 2 ms in browsers. So a full render can block the page
  for seconds while a preview cannot; the choice is midishare's.
