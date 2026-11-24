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

**Status:** ready-for-agent

## Checklist

- [ ] **Before any download**, ask the user, through the planning hub, to approve one official
  FluidSynth 2.x release, naming the file, its source and its size. It is kept outside both
  repositories and never committed.
- [ ] Use `FluidR3_GM.sf2`, already on the development machine from map ticket 10 and kept
  outside the repository. If it is missing, fetching it again needs the user's yes at that
  time; the earlier approval does not cover a re-fetch.
- [ ] Compare on the full file, with bank 0, program 0 ("Yamaha Grand Piano") selected in both
  renderers. Checking that an extract renders the same is the extract tool's job, in spec B
  or later.
- [ ] Write a throwaway harness under `.scratch/soundfont-node/prototypes/`, outside the
  build, as the map's probes are.
  - It renders the clip natively through a MIDI file node and the soundfont node.
  - It writes or compares the samples.
  - `midi_cli` and the product code do not change. Rendered audio and licensed files are
    never committed.
- [ ] The test clip covers every key range of the preset at velocities 64 to 127, across its
  velocity zones. Each note is held long enough to measure its sustain, then its release.
  The clip is written by the harness.
- [ ] FluidSynth renders the same MIDI bytes and file at the same sample rate, with reverb and
  chorus off.
- [ ] For every note, within the thresholds:
  - pitch within ±1 cent;
  - onset within FluidSynth's 64-frame control block;
  - a per-note RMS envelope within ±1 dB in 10 ms windows while the note is within 60 dB of
    its peak, after one global gain offset is removed.

  Below velocity 64 the two differ by design. Compare there only if FluidSynth's default
  modulator can be replaced with the node's.
- [ ] Confirm, or record as unconfirmed, the two facts the spec carries:
  - the velocity-to-cutoff default's amount source in the specification texts, as FluidSynth's
    source describes them;
  - FluidSynth's 0.4 scaling of `initialAttenuation`.

  No specification PDF is downloaded for this.
- [ ] Record native timings at 44.1 and 48 kHz, in Release:
  - loading the default piano;
  - a one-note render with it loaded, as the note preview does (0.25 s at velocity 105, then
    the release);
  - map ticket 02's worst-case clip.
- [ ] If ticket 04 has landed, record the full file's ignored list too.
- [ ] Record the evidence in this ticket: versions, commands, the clip, the numbers per note,
  and any failure, with its cause if found.
- [ ] The last acceptance item stays with the user and is not this ticket's: judging sound
  parity by ear on the live site, after spec B and midishare's change.
