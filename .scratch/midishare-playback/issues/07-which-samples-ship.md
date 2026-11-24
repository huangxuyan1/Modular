# Which piano samples ship, in what format, under what licence and at what size?

Type: research
Status: open

## Question

The site downloads its piano today from a third-party host at play time. At the destination
the samples are the site's own assets, so they need a licence that allows redistribution and a
size a browser downloads without complaint.

Find out, from primary sources:

- Candidate piano sample sets with a redistributable licence, including the licence of the
  set the site uses today (FluidR3_GM, as rendered by the `midi-js-soundfonts` project) and of
  at least two small alternatives.
- For each, the download size for one piano preset across midishare's range (C2–B6, MIDI
  notes 36–95), the number of distinct recordings (one per note, or one per several notes,
  repitched), velocity layers and loop points.
- Format trade-off: a SoundFont 2 file parsed by Modular at authoring time, as `MidiFile`
  parses MIDI, against compressed per-note files the browser decodes (`decodeAudioData`) and
  hands Modular as float samples. Which one keeps Modular free of a codec, and which one keeps
  the instrument an authored, saveable graph?
- What attribution, if any, the site must show.

Deliverable: a short table of candidates with licence, size and format, and a recommendation.
