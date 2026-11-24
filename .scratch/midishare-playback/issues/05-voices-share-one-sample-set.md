# How do the voices of a sampled instrument share one set of samples?

Type: grilling
Status: open

## Question

A sampled piano is a voice allocator, N sample-playing voices and a mixer: polyphony is "a
`VoiceAllocatorNode`, several of these, and a `MixNode` — not a bigger synth"
(`MidiNodes.h`). Each voice needs the same few megabytes of samples. Modular's rule is that
stored node content belongs to its node and is saved with it (`CONTEXT.md`, **Stored node
content**), so the obvious build stores N copies, in memory and in the save file.

Decide where the samples live:

- **Each voice owns a copy.** Keeps the rule, costs N times the memory and the file size.
- **One node owns the samples, and voices read them over a wire.** No port domain carries
  ownership of shared data. A pointer written into a `value` slot would type-check, but nothing
  would own, save or reload what it points at. A reference-carrying subtype is a kernel
  question.
- **Voices share an immutable bank by pointer, outside the graph.** Cheap, but the bank becomes
  storage the graph does not hold, which is what the terminal claim's derivability clause
  forbids, unless the bank is itself authored content saved once.
- **One polyphonic sampler primitive** that allocates its own voices. Cheapest to build, and
  against the stated composition rule.

Name the trade-off, and whether the answer is durable enough for an ADR. Check it against
graph persistence (`docs/graph-persistence.md`): what is saved, once or N times, and what a
reload rebuilds.
