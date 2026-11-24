# Is the soundfont node one primitive or a supernode of voices, and where do its samples live?

Type: grilling
Status: open

## Question

The user asked for "a soundfont node that reads a piano soundfont". From outside, a supernode
is indistinguishable from a primitive node (`CONTEXT.md`), so either shape satisfies that; the
choice is about what is inside, and it decides where the samples live. The node must be a
reusable part of the node library that a DAW can use later, not midishare-specific code.

**The shape.**

- **A supernode:** MIDI-in boundary → `VoiceAllocatorNode` → N sample-playing voices →
  `MixNode` → audio-out boundary. This follows the stated composition rule: polyphony is "a
  `VoiceAllocatorNode`, several of these, and a `MixNode` — not a bigger synth" (`MidiNodes.h`).
  Every voice needs the same samples, and the allocator cannot see when a voice's release has
  ended (see [How does a released voice keep sounding until its release ends?](06-release-survives-voice-allocation.md)).
- **One polyphonic primitive** that reads the soundfont and allocates its own voices. The
  samples have one owner and release is internal, but it goes against the composition rule,
  and a user cannot rewire its insides.

**Where the samples live**, if voices are separate nodes. Modular's rule is that stored node
content belongs to its node and is saved with it (`CONTEXT.md`, **Stored node content**).

- **Each voice owns a copy.** Keeps the rule, costs N times the memory and the save file.
- **One node owns the samples, and voices read them over a wire.** No port domain carries
  ownership of shared data. A pointer written into a `value` slot would type-check, but nothing
  would own, save or reload what it points at. A reference-carrying subtype is a kernel
  question.
- **Voices share an immutable bank by pointer, outside the graph.** Cheap, but the bank becomes
  storage the graph does not hold, which the terminal claim's derivability clause forbids,
  unless the bank is itself authored content saved once.

**Persistence**, in either shape: is the soundfont saved inside the graph as stored node
content (the whole file, or only the preset in use), or named by reference, which is
out-of-band storage? Check against `docs/graph-persistence.md`: what is saved, once or N
times, and what a reload rebuilds.

Name the trade-off, and whether the answer is durable enough for an ADR.
