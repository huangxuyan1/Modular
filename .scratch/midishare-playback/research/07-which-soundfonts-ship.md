# Which soundfonts ship, in what format, under what licence and at what size?

Resolves the research ticket `.scratch/midishare-playback/issues/07-which-samples-ship.md`.
Date: 2026-11-24. Read-only research: no soundfont, sample or archive was downloaded. Sizes come
from published directory listings and API metadata. Anything not stated by a primary source is
marked **unconfirmed**. Licence readings are plain readings of the texts, not legal advice.

## Summary

**Recommendation for the default piano:** take FluidR3_GM's acoustic grand piano (bank 0,
program 0) from `FluidR3_GM.sf2`, extract it into a piano-only SoundFont 2 file, and ship that
file as one of the site's own assets. Modular reads it at authoring time as 16-bit PCM.

- It is the piano the site plays today, rendered note by note, so the sound stays the same.
- Its author released it under the MIT licence. MIT allows copying, modifying and redistributing
  it, so extracting one preset is allowed, as long as the copyright and permission notice goes
  with the file.
- SoundFont 2 keeps Modular free of any codec, and keeps the instrument an authored node that
  owns its samples.

**The first open fact is the size of that extract.** The full bank is 148,398,306 bytes, about
six times Cloudflare's 25 MiB limit per file. The piano preset's own size is not published.
For comparison, today's 60 per-note MP3s for notes 36–95 total 1,396,572 bytes (1.33 MiB).

- If the extract is too large, the fallback is FreePats' Upright Piano KW "small" SF2. It is
  CC0, so it has no conditions at all, and it is about 10 MiB unpacked (unconfirmed). It is a
  different sound: an upright piano recorded in a living room.
- **For the stretch goal (a choice of soundfonts):** extract further presets from FluidR3_GM,
  which keeps one licence and one notice. The second choice is GeneralUser GS: a permissive
  licence with no attribution required. It also needs extracting (30.8 MiB is over the limit),
  and it leans on modulators and the low-pass filter, so it constrains the subset ticket.
- **Avoid** TimGM6mb (GPL-2, whose reach into rendered audio is unclear) and the Salamander SF2
  (296 MiB).

## Candidates

Sizes are exact bytes where an API or listing gave bytes; "M"/"MiB" figures are as listed.

| Name | Licence (verified at) | What it requires | Size | Format | Notes |
|---|---|---|---|---|---|
| **FluidR3_GM** (full GM bank) | MIT (`COPYING` and `README` in Debian's `fluid-soundfont` 3.1 source) | Keep the copyright and permission notice in copies or substantial portions. No share-alike. Extracting a preset is a modification, which MIT permits. | 148,398,306 B (141.5 MiB) | SF2 | Source of today's sound. Debian's README says it combines edited public-domain samples with the author's own recordings. Piano preset size, layers and loops are **unconfirmed** (needs the file). |
| **midi-js-soundfonts, `FluidR3_GM/acoustic_grand_piano-mp3/`** (per-note renders abcjs fetches) | The repository is MIT. Its README labels the FluidR3_GM renders "Creative Commons Attribution 3.0", which conflicts with FluidR3's own MIT. | MIT notice for the repository. CC BY 3.0 if its label is honoured: credit, title and URI, and mark changes. MIT notice for FluidR3. All three are cheap to meet. Self-hosting is allowed. | 89 files, 1,983,629 B in the fork abcjs uses. Notes C2–B6 (60 files): 1,396,572 B. | One MP3 per pitch | abcjs 6.7.2's default URL is `paulrosen.github.io/midi-js-soundfonts/FluidR3_GM/`. The original repository it was forked from lists the same file sizes. The generator script linked from the README renders each note with FluidSynth at velocity 85, a 3000 ms note-on, chorus and reverb off, gain 0.5 and FluidSynth's default 44,100 Hz, then LAME VBR at 8–64 kbps. Whether these exact settings made these files is **unconfirmed**. |
| **FluidR3Mono_GM.sf3** (MuseScore) | MIT (MuseScore handbook, and the licence text in MuseScore's `share/sound`) | MIT notice | 23,712,790 B (22.6 MiB) in MuseScore's `share/sound` today. The MuseScore 3 handbook lists 13.8 MB for an older build. | SF3 (Ogg Vorbis) | FluidR3 mixed to mono where appropriate. It fits under 25 MiB, but it needs a Vorbis decoder. |
| **MuseScore_General** (and its MuseScore 4 successor `MS Basic.sf3`) | MIT. `MuseScore_General_License.md` also requires its acknowledgements and copyright notices to be kept in any derivative work. | MIT notice plus the listed acknowledgements | `.sf2` 206M, `.sf3` 38M (osuosl listing, v0.2, 2020). `MS Basic.sf3`: 51,278,610 B. | SF2 or SF3 | `Sample_Sources.csv` says the Grand Piano (000:000) is Akai S5000 Steinway Model D close-mic samples, public domain (confirmed with Akai), with the sample set cut from 250 MB to 94 MB. Whether 94 MB is the piano's size inside this file is **unconfirmed**. |
| **GeneralUser GS** v2.0.3 (2026-02-22) | GeneralUser GS License v2.0 (`documentation/LICENSE.txt`) | Use in software projects and modification are allowed, commercial use included. No attribution, no share-alike. Asks sites not to link directly to the author's download files; hosting your own copy is fine. Extracting a preset is not mentioned; the permission to modify the bank plainly covers it (**unconfirmed** legally). | 32,319,396 B (30.8 MiB) | SF2. The README describes converting it to SF3 yourself. | 261 presets and 13 kits. The README says it relies heavily on SoundFont synthesis and modulator features and needs a standards-compliant synth. It rates TinySoundFont, which has no modulators, as poor, and says MuseScore 4 disabling the low-pass filter breaks many presets. The author admits some samples' origin is uncertain. |
| **TimGM6mb** | GPL-2 (Debian `timgm6mb-soundfont` copyright) | Copyleft: any distributed modification (an extract) is GPL-2 with its source form. Whether the GPL reaches audio rendered from it is unclear: the GPL FAQ says output is covered when it copies the program's own art or audio. | 5,969,788 B (5.7 MiB) | SF2 | The only full GM set under 25 MiB that needs no codec. The author says the samples are his own, public domain or GPL. Piano details **unconfirmed**. |
| **Salamander Grand Piano** V3, FreePats SF2 | CC BY 3.0 (sfzinstruments page, FreePats page). One forum post claims it was relicensed to the public domain; **unconfirmed**, and the official pages still say CC BY 3.0. | Credit, title, URI and a credit for adaptations; mark changes. No share-alike. No DRM on the work. | SF2 296 MiB; SFZ WAV 44.1 kHz 16-bit 394 MiB | SF2 or SFZ | Yamaha C5 recorded at 48 kHz 24-bit, 16 velocity layers, sampled every minor third from the lowest A. Far too large. Smaller third-party SF2 conversions exist, but their provenance was not verified. |
| **Upright Piano KW** (FreePats) | CC0 1.0 | None | "Small" SF2: 5.8 MiB download (10 MiB). Full SF2: 27 MiB (56 MiB). The page does not say what the figure in brackets means; it is probably the unpacked size. | SF2 inside a `.7z` | Kawai upright recorded in 2017. Full set: stereo, 2 velocity layers. The page says the bass notes loop. The small set's layers, channels and rate are **unconfirmed**. |

Not candidates: FluidR3_GS.sf2 (3,201,926 B in the same Debian source) holds only the GS
extension presets. The FreePats GM set is GPL-3+ and its SF2 is 227 MiB (307 MiB). MusyngKite
and FatBoy (midi-js-soundfonts) are CC BY-SA 3.0, which is share-alike, and far larger.

### Per-candidate sampling detail for notes 36–95

| Candidate | Recordings covering 36–95 | Repitch per note | Velocity layers | Loops | Rate / depth |
|---|---|---|---|---|---|
| midi-js MP3s | 60, one per pitch | none in the browser (FluidSynth repitched FluidR3 when rendering) | 1 (velocity 85 per the generator script; abcjs scales gain) | none; fixed render, note-off at 3 s | MP3 VBR ≤64 kbps; 44.1 kHz assumed from FluidSynth's default; **unconfirmed** |
| FluidR3_GM piano | **unconfirmed** | **unconfirmed** | **unconfirmed** | **unconfirmed** | **unconfirmed** |
| Salamander V3 | 20, a sample every minor third (C2, D♯2, F♯2, A2 … A6) | at most ±1 semitone if each sample covers its key ±1 (**unconfirmed** for the SF2) | 16 | not stated | 48 kHz/24-bit source; SF2 depth **unconfirmed** |
| Upright Piano KW | **unconfirmed** | **unconfirmed** | 2 (full set); small **unconfirmed** | bass notes loop | **unconfirmed** |
| GeneralUser GS, MuseScore_General, TimGM6mb, FluidR3Mono pianos | **unconfirmed** | **unconfirmed** | **unconfirmed** | **unconfirmed** | **unconfirmed** |

No primary source publishes the zone layout of the FluidR3, GeneralUser, MuseScore or TimGM6mb
pianos. Reading the files is the only way; see "Open facts that need the file".

## What the node must read (SoundFont 2.04 specification)

The 2.04 spec (revision 2002, adding 24-bit samples; its page footers still say 2.01) defines
the following. Numbers are generator enumerators.

**Structure.** A preset has zones, each pointing to an instrument (generator 41). An instrument
has zones, each pointing to a sample (`sampleID`, 53). A first zone with no terminal generator is
a *global zone*, whose generators and modulators apply to every other zone. Instrument-level
generators are absolute. Preset-level generators are *added* to them (§8.5).

The sample generators are only valid at the instrument level: the address offsets 0–4, 12, 45 and
50, `sampleModes` 54, `overridingRootKey` 58 and `exclusiveClass` 57. So are `keynum` 46 and
`velocity` 47.

**What a typical piano preset uses:**

- **Zone selection:** `keyRange` 43 and `velRange` 44. Velocity layers are zones with disjoint
  `velRange`s. When present, `keyRange` must come first and `velRange` second.
- **Pitch:** the root key is `overridingRootKey` 58 if it is 0–127, otherwise the sample
  header's `byOriginalPitch`, where 255 or an illegal value means 60. Then `coarseTune` 51
  (semitones), `fineTune` 52 (cents), `scaleTuning` 56 (default 100 cents per key) and the
  header's `chPitchCorrection` (cents). The header's `dwSampleRate` gives the playback rate
  ratio. Derived from these definitions: cents = scaleTuning·(key − root) + 100·coarse + fine +
  correction, and increment = 2^(cents/1200) · dwSampleRate / engine rate. That fits Modular's
  one global rate without resampling the source.
- **Sample bounds and loops:** header start/end/loop points plus the offset generators 0–4, 12,
  45 and 50. `sampleModes` 54: 0 no loop, 1 loop continuously, 2 treated as no loop, 3 loop
  while the key is held then play the rest. The header requires at least 8 points before
  `dwStartloop` and after `dwEndloop`, and at least 32 points in a loop. Each sample is followed
  by at least 46 zero points.
- **Volume envelope:** `delayVolEnv` 33, `attackVolEnv` 34, `holdVolEnv` 35, `decayVolEnv` 36,
  `releaseVolEnv` 38 (timecents; default −12000, under 1 ms), `sustainVolEnv` 37 (cB of
  attenuation; default 0). Pianos commonly shorten decay with pitch through
  `keynumToVolEnvDecay` 40 and `keynumToVolEnvHold` 39 (timecents per key). That a given
  candidate uses these is **unconfirmed**.
- **Level and placement:** `initialAttenuation` 48 (cB), `pan` 17 (−500 left to +500 right,
  0.1 % units). A stereo pair is a left and a right sample linked by `wSampleLink`. They play in
  sync, pitch follows the right sample's generators, and `pan` places each side.
- **Filter:** `initialFilterFc` 8 (default 13500 cents, which means open) and `initialFilterQ`
  9 (cB).
- **Not usually needed for a piano:** the modulation envelope 25–32, the LFOs 5–7, 10, 13 and
  21–24, the reverb and chorus sends 16 and 15, and `exclusiveClass` 57. GeneralUser GS's README
  suggests its presets do use modulation; **unconfirmed** for its piano.

**Default modulators (§8.4)** apply at the instrument level even when the file lists none.
"Turning them off" takes an explicit modulator in the file.

| Source | Destination | Amount | Shape |
|---|---|---|---|
| Note-on velocity | initialAttenuation | 960 cB | negative unipolar, concave |
| Note-on velocity | initialFilterFc | −2400 cents | negative unipolar, linear (2.01 dropped the old "attack < 7 ms" condition) |
| Channel pressure | vibrato LFO to pitch | 50 cents | linear |
| CC1 | vibrato LFO to pitch | 50 cents | linear |
| CC7 | initialAttenuation | 960 cB | negative unipolar, concave |
| CC10 | pan | 1000 (0.1 %) | bipolar, linear |
| CC11 | initialAttenuation | 960 cB | negative unipolar, concave |
| CC91 / CC93 | reverb / chorus send | 200 (0.1 %) | linear |
| Pitch wheel × wheel sensitivity | initial pitch | 12700 cents | bipolar, linear |

For a single-channel piano with no controllers, the first two rows are what matter: velocity to
attenuation, and velocity to filter cutoff.

**Sample data.** The `smpl` chunk holds 16-bit signed little-endian samples. An optional `sm24`
chunk (2.04) holds the low byte of 24-bit data. A reader that skips `sm24` still plays the file
at 16 bits, as the spec allows.

**SoundFont 3** has no official specification. FluidSynth's RFC page sets `ifil` major version 3
and uses sample-type bit `0x10` for a compressed sample, typically Ogg Vorbis. `dwStart` and
`dwEnd` become byte offsets into that sample's compressed stream. Loop points stay in decoded
sample points, relative to the decoded data. Compressed and PCM samples may be mixed in one file.
Files from cognitone's `sf2convert` are incompatible.

## Format trade-off

| Route | Codec in Modular? | Authored, saveable node? | Size | Other consequences |
|---|---|---|---|---|
| **SF2 read by Modular at authoring time** (as `MidiFile::parse` reads bytes) | No. RIFF parsing and 16-bit PCM only. | Yes. The node owns its samples as stored content, saved and reloaded like other primitives. | Largest: uncompressed PCM. Full GM banks exceed 25 MiB; a single-preset extract is needed. | Full SF2 fidelity: layers, loops, envelope, filter. Reusable by a later DAW. |
| **SF3, decoded inside Modular** | Yes. A Vorbis decoder in `nodes/`, not `kernel/`, so the purity grep still passes, but it is a new third-party dependency and adds to the module's size. | Yes | Smallest structured format. GeneralUser's README says SF3 cuts the file to under a third of its size. | One parser for SF2 and SF3. Decoder choice and cost are **unconfirmed**. |
| **SF3, decoded by the browser** (the page extracts each sample's Ogg stream and calls `decodeAudioData`) | No | Only partly. The page must understand SF3 to slice out samples, so the instrument is split between JavaScript and Modular. | Small | `decodeAudioData` resamples to the context's rate (W3C Web Audio). Loop points and `dwSampleRate` must then be rescaled, and loop points can land between samples. Safari added the Ogg container (Vorbis, Opus) in 18.4. Whether each SF3 sample is a standalone Ogg file the browser can decode is **unconfirmed**. |
| **Per-note compressed files decoded by the browser** (today's model) | No | No. The page owns the samples and hands floats in at run time, so the graph cannot save or reload the instrument by itself. This conflicts with **Stored node content** in `CONTEXT.md`, which says stored content belongs to its node. | Smallest: 1.33 MiB for 36–95 | One velocity layer, and a fixed render with note-off at about 3 s, so held notes cannot sustain past the render. No loops. Already resampled to the context's rate. MP3 encoder padding at the start could shift onsets; **unconfirmed**. |

**Answer.** Codec-free and authored at the same time means SF2 read by Modular. Browser
decoding keeps Modular codec-free but moves the instrument out of the graph. SF3 keeps the node
authored but puts a codec in `nodes/`, or splits the parse with the page.

## Hosting limit (Cloudflare Workers static assets)

- **Largest single file: 25 MiB** (26,214,400 bytes) on both Free and Paid plans (Workers
  platform limits, "Static Assets").
- At most 20,000 files per Worker version on Free and 100,000 on Paid (Wrangler 4.34.0 or
  later for the higher counts).
- Response bodies have no size limit, but the CDN cache caps objects at 512 MB below Enterprise.
- Static assets answer a `Range` request with `200` and the whole file, not `206`
  ("Billing and limitations").
- Measured against 25 MiB: TimGM6mb (5.7 MiB), Upright Piano KW small (about 10 MiB) and
  FluidR3Mono_GM.sf3 (22.6 MiB) fit. GeneralUser GS (30.8 MiB), MuseScore_General.sf3 (38M),
  `MS Basic.sf3` (48.9 MiB) and FluidR3_GM.sf2 (141.5 MiB) do not.
- Whether Cloudflare compresses `.sf2` responses is **unconfirmed**. PCM compresses poorly with
  gzip or brotli in any case.

## Open facts that need the file

1. **FluidR3_GM acoustic grand piano (bank 0, program 0):** the extract's size; how many
   samples cover notes 36–95 and the largest repitch; velocity layers; stereo pairs or mono;
   sample rate and whether `sm24` is present; `sampleModes` and loop lengths; which envelope,
   `keynumToVolEnv*`, attenuation, pan and filter generators it sets; and any instrument-level
   modulators that override the defaults.
2. The same for Upright Piano KW small, plus its actual unpacked size and the meaning of the
   "(10MiB)" figure.
3. The same for the stretch-goal presets (GeneralUser GS, other FluidR3_GM programs), above all
   which modulators and filter settings they rely on.
4. **midi-js MP3s:** the actual bitrate, sample rate, channel count and duration, and encoder
   padding. The velocity of 85 and the 3 s note-on come only from the linked generator script.
5. **FluidR3Mono_GM.sf3 and MuseScore_General.sf3:** Vorbis quality, and whether each compressed
   sample is a complete Ogg stream.
6. **MuseScore_General piano:** its byte size inside the `.sf2`. The CSV's "94 MB" may describe
   the source set.
7. Whether the extract tool (for example Polyphone) preserves every generator and modulator,
   and what notice file goes beside the extract.

## Sources

- Debian `fluid-soundfont` 3.1-6 source (file sizes, `COPYING`, `README`): https://sources.debian.org/src/fluid-soundfont/3.1-6/
- Debian `fluid-soundfont` copyright: https://metadata.ftp-master.debian.org/changelogs/main/f/fluid-soundfont/unstable_copyright
- Debian `fluid-soundfont-gm` package: https://packages.debian.org/sid/fluid-soundfont-gm
- midi-js-soundfonts README and licence: https://github.com/gleitz/midi-js-soundfonts and https://raw.githubusercontent.com/gleitz/midi-js-soundfonts/gh-pages/LICENSE.txt
- Fork abcjs uses: https://paulrosen.github.io/midi-js-soundfonts ; file listing: https://api.github.com/repos/paulrosen/midi-js-soundfonts/contents/FluidR3_GM/acoustic_grand_piano-mp3?ref=gh-pages
- Generator script linked from the README: https://github.com/gleitz/MIDI.js/blob/master/generator/ruby/soundfont_builder.rb
- abcjs 6.7.2 synth source: https://cdn.jsdelivr.net/npm/abcjs@6.7.2/src/synth/create-synth.js and https://cdn.jsdelivr.net/npm/abcjs@6.7.2/src/synth/load-note.js
- FluidSynth synth settings (default sample rate): https://www.fluidsynth.org/api/settings_synth.html
- FluidSynth SoundFont 3 RFC: https://www.fluidsynth.org/wiki/SoundFont3Format/
- MuseScore_General files, licence and sample sources: https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/
- MuseScore `share/sound` listing: https://api.github.com/repos/musescore/MuseScore/contents/share/sound
- MuseScore 3 handbook, SoundFonts: https://musescore.org/en/handbook/3/soundfonts-and-sfz-files
- GeneralUser GS repository, README, changelog and licence: https://github.com/mrbumpy409/GeneralUser-GS (`documentation/README.md`, `documentation/CHANGELOG.md`, `documentation/LICENSE.txt`)
- Debian `timgm6mb-soundfont` 1.3-5 source and copyright: https://sources.debian.org/src/timgm6mb-soundfont/1.3-5/
- FreePats acoustic pianos (Upright Piano KW, Salamander SF2): https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html
- FreePats GM set: https://freepats.zenvoid.org/SoundSets/general-midi.html
- Salamander Grand Piano: https://sfzinstruments.github.io/pianos/salamander
- CC BY 3.0 legal code: https://creativecommons.org/licenses/by/3.0/legalcode
- GPL FAQ (output of a GPL program, aggregation): https://www.gnu.org/licenses/gpl-faq.en.html
- SoundFont 2.04 Technical Specification (mirror): https://www.synthfont.com/sfspec24.pdf (also https://polyphone.io/doc/files/sf_specifications_v2.04.pdf)
- W3C Web Audio API, `decodeAudioData`: https://www.w3.org/TR/webaudio/
- WebKit, Safari 18.4 (Ogg container for Opus and Vorbis): https://webkit.org/blog/16574/webkit-features-in-safari-18-4/
- Cloudflare Workers platform limits: https://developers.cloudflare.com/workers/platform/limits/
- Cloudflare static assets, billing and limitations: https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/
