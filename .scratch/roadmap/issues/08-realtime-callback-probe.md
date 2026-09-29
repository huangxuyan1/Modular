# Probe: does the block loop survive a real driver callback and a mid-stream recompile?

Type: prototype
Status: open
Blocked by: 07

## Question

Everything built so far is offline — the render CLI, and every invariant test that compares
sample vectors byte-for-byte. Three expensive-class unknowns are currently theoretical
because nothing has ever run under a real callback: non-static latency, thread safety
between audio and editing, and recompiling without a dropout.

This is a **spike**, and deliberately not the device-IO work — that lands much later, with
JUCE. No JUCE commitment here; throwaway code.

- Drive the existing block loop from a real hardware callback (CoreAudio directly is
  enough on this platform) with genuinely unstable block sizes.
- Force one recompile mid-stream — the simplest structural edit available — and measure
  whether audio glitches, drops, or survives.
- Report numbers, not opinions: what was the callback deadline, what did the compile cost,
  where did allocation happen. The kernel's rule is no allocation in the block loop, with
  `EventBuffer::push` a documented exception — does that hold under a real callback?
- Note whether any of this contradicts the settled device-boundary decisions (block size
  never the time unit, device-ness in the boundary node).

Outcome: converts three speculative unknowns into measurements, and tells the ordering
whether live editing is a near or far claim. Depends on ticket 07 because "the simplest
structural edit" needs to be a defined operation first.
