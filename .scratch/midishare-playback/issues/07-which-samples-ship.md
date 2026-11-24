# Which soundfonts ship, in what format, under what licence and at what size?

Type: research
Status: open

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
