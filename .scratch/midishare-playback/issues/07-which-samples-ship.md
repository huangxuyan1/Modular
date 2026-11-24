# Which soundfonts ship, in what format, under what licence and at what size?

Type: research
Status: resolved

## Question

The site downloads its piano today from a third-party host at play time. At the destination a
Modular soundfont node reads a piano soundfont by default, and, as a stretch goal if time
allows, the site's users choose among a few (the user's round-1 answer). The soundfonts become
the site's own assets, so they need a licence that allows redistribution and a size a browser
downloads without complaint.

Find out, from primary sources:

- **Candidates.** Soundfonts with a redistributable licence that can serve as the default
  piano. Include the set the site uses today (FluidR3_GM, as rendered per note by the
  `midi-js-soundfonts` project) and at least two small alternatives. Cover both single-piano
  soundfonts and small General MIDI sets, whose other presets could serve the stretch goal.
- **For each:** the licence and what it requires (attribution, share-alike, whether extracting
  one preset into a smaller file is allowed); file size, and if possible the size of the piano
  preset alone; format (SoundFont 2, or SoundFont 3 with Ogg Vorbis samples); sample rate and
  bit depth; how many recordings cover midishare's range (C2–B6, MIDI notes 36–95) and how far
  each is repitched; velocity layers; loops.
- **What the node must read.** Which SoundFont 2 generators the candidate piano presets use
  (key and velocity ranges, root key and tuning, sample modes and loops, the volume envelope,
  attenuation, pan, filter), where that can be learned without downloading the file. This
  feeds the subset ticket.
- **Format trade-off.** A SoundFont 2 file read by Modular at authoring time, as `MidiFile`
  reads MIDI; a SoundFont 3 file, whose Vorbis samples need a decoder somewhere; or compressed
  per-note files the browser decodes (`decodeAudioData`) and hands Modular as float samples.
  Which keeps Modular free of a codec, and which keeps the instrument an authored, saveable
  node?
- **Hosting limit.** The largest single static file the site's host (Cloudflare Workers static
  assets) serves.

Deliverable: a short table of candidates with licence, size and format, the open facts that
need the file itself, and a recommendation for the default piano.

## Answer

Resolved 2026-11-24 by research, read-only: no soundfont or sample file was downloaded.
Findings, sources and the full candidate table:
[Which soundfonts ship](../research/07-which-soundfonts-ship.md).

- **Format: SoundFont 2, read by Modular at authoring time.** It is the only route that keeps
  Modular free of a codec *and* keeps the instrument an authored node that owns its samples.
  SoundFont 3 puts a Vorbis decoder in `nodes/` or splits the parse with the page. Per-note files
  decoded by the browser move the samples out of the graph, against **Stored node content**, and
  `decodeAudioData` resamples to the context's rate, which moves every loop point.
- **Hosting limit: 25 MiB per file** (Cloudflare Workers static assets, Free and Paid). Every
  full General MIDI bank worth shipping is over it (FluidR3_GM 141.5 MiB, GeneralUser GS
  30.8 MiB), so the site ships a single-preset extract.
- **Recommended default: FluidR3_GM's acoustic grand piano** (bank 0, program 0), extracted into
  a piano-only SoundFont 2 file. It is the piano the site plays today, and FluidR3's own licence
  is MIT, which allows the extract with the notice kept. The `midi-js-soundfonts` renders the site
  fetches today are labelled CC BY 3.0 there; crediting both costs nothing.
- **Fallback: FreePats' Upright Piano KW "small" SF2**, CC0, about 10 MiB (unconfirmed), a
  different sound.
- **Stretch goal:** further FluidR3_GM presets first (one licence, one notice), GeneralUser GS
  second (permissive, but it leans on modulators and the filter). Avoid TimGM6mb (GPL-2) and the
  296 MiB Salamander SF2.
- **For the subset ticket:** with no controllers, the default modulators that matter are
  note-on velocity to attenuation (960 cB, concave) and to filter cutoff (−2400 cents). Today's
  per-note renders are one velocity layer at velocity 85, cut at 3 s.
- **Open, needs the file:** the extract's size, and the FluidR3 piano's zone layout, velocity
  layers, loops, envelope, filter and any modulators of its own. That measurement is
  [Measure the FluidR3_GM piano before it becomes the default](10-measure-the-fluidr3-piano.md).

Which soundfont becomes the default is a judgment call for the planning hub, made once the
measurement says whether the extract fits.
