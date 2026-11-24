# Does a voice's release survive the allocator freeing it at note-off?

Type: grilling
Status: open

## Question

`VoiceAllocatorNode` marks a voice free the moment its note-off arrives, and the next note-on
takes the first free voice (`MidiNodes.h`). A sine with a hard off never noticed. A piano
voice is still sounding after note-off, so a quick passage can cut a ringing release short,
which is audible.

The voice knows it is still busy; the allocator does not. Telling it needs a wire from each
voice back to the allocator, which is a cycle, and a cycle is a compile error until delay
compensation lands.

Decide:

- Is this a different allocation strategy in user space? The allocator's own comment says "a
  different strategy is simply a different node". For example, prefer the voice released
  longest ago, or hold a voice for a release time the allocator is configured with, measured
  in stream time.
- Or does it need the voice's real state, and therefore wait on cycles and delay compensation
  (the roadmap's "PDC and cycles")?
- What a stolen voice does to its tail: cut it, or fade it out quickly so stealing does not
  click.

The answer must stay block-size invariant: allocation may depend only on the event stream and
stream time, as today.
