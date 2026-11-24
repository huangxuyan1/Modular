# How does a released voice keep sounding until its release ends?

Type: grilling
Status: open
Blocked by: 05

## Question

`VoiceAllocatorNode` marks a voice free the moment its note-off arrives, and the next note-on
takes the first free voice (`MidiNodes.h`). A sine with a hard off never noticed. A piano voice
is still sounding after note-off, so a quick passage can cut a ringing release short, which is
audible.

Where this is decided depends on
[Is the soundfont node one primitive or a supernode of voices, and where do its samples live?](05-soundfont-node-shape-and-samples.md):

- **Voices as separate nodes.** The voice knows it is still busy; the allocator does not.
  Telling it needs a wire from each voice back to the allocator, which is a cycle, and a cycle
  is a compile error until delay compensation lands. So either it is a different allocation
  strategy in user space (the allocator's own comment says "a different strategy is simply a
  different node"): prefer the voice released longest ago, or hold a voice for a release time
  the allocator is configured with, in stream time. Or it needs the voice's real state, and
  waits on the roadmap's "PDC and cycles".
- **One primitive.** The node sees its own voices' state, so the question is only its stealing
  policy.

Either way, decide what a stolen voice does to its tail: cut it, or fade it out quickly so
stealing does not click. The answer must stay block-size invariant: allocation may depend only
on the event stream and stream time, as today.
