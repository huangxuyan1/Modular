# How does a released voice keep sounding until its release ends?

Type: grilling
Status: open
Blocked by: 05

## Question

`VoiceAllocatorNode` marks a voice free the moment its note-off arrives, and the next note-on
takes the first free voice (`MidiNodes.h`). A sine with a hard off never noticed. A piano voice
is still sounding after note-off, so a quick passage can cut a ringing release short, which is
audible.

**Narrowed by
[Is the soundfont node one primitive or a supernode of voices, and where do its samples live?](05-soundfont-node-shape-and-samples.md)**
(2026-11-24, provisional until its planning PR merges). The soundfont node is one polyphonic
primitive with a fixed pool of internal voices, so it sees each voice's state, and no wire
back to an allocator is needed. The branch where voices are separate nodes is gone from this
map. `VoiceAllocatorNode` keeps the limitation for composed instruments, but the only one today
is the sine voice, which never ships, so that lies outside this destination.

What is left is the node's own voice policy:

- **The pool.** Its default size, and what it counts: notes, or sounding samples, since a note
  with stereo samples plays two.
- **When every voice is busy**, which one is taken: a released voice before a held one, and
  among those, the oldest or the quietest?
- **The same key struck again** while its previous note still sounds, held or releasing: a new
  voice beside the old one, or the old voice taken over?
- **What a taken voice does to its tail:** cut it, or fade it out quickly so taking it does not
  click, and over how long.

The answer must stay block-size invariant: allocation may depend only on the event stream and
stream time, as today. SoundFont 2 rules that stop other voices (exclusive class) belong to
[Which part of the SoundFont format does the node read?](09-soundfont-format-subset.md), not
to this ticket.
