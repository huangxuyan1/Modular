# Which part of the SoundFont format does the node read?

Type: grilling
Status: open
Blocked by: 07, 10

## Question

The soundfont node reads a soundfont at authoring time, as `MidiFile` reads MIDI: file I/O and
parsing never happen in `process()`. The SoundFont 2 format is large (presets, instruments,
zones, generators, modulators), and a node that reads all of it is a large piece of work. A
node that reads too little plays the chosen piano wrongly.

Using the default soundfont that
[Which soundfonts ship, in what format, under what licence and at what size?](07-which-samples-ship.md)
recommends, decide:

- Which generators the node honours: key and velocity ranges, root key and tuning, sample modes
  and loops, the volume envelope, attenuation, pan, the filter. Which may it ignore without an
  audible difference on that piano, and which must a reusable node honour anyway, since a DAW
  will load other soundfonts later?
- Default modulators (velocity to attenuation, velocity to filter): honoured, approximated, or
  ignored?
- What the node does with a soundfont that uses something it does not read: refuse it at
  authoring time with an error, or play it as best it can. Authoring errors throw, by the
  kernel's convention.
- SoundFont 3 (Ogg Vorbis samples): read, or refused? Reading it puts a decoder in the node
  library.
- What the node reads velocity from, given that abcjs assigns loudness and the site's own notes
  carry none.

The answer fixes the node's authoring-time interface and the size of its spec.
