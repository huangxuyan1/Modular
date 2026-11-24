# 05: File modulators and exclusive class

**What to build:** A General MIDI bank's presets sound as their author set them. The file's
own modulators shape each note, including the amount-0 modulators FluidR3 uses to switch off
the default velocity-to-cutoff modulator. A drum kit's exclusive classes choke: a closed hi-hat
ends the open one with a short fade. See the [spec](../spec.md) (ticket 09, D2 and D8).

**Blocked by:**
- 03: When every voice is busy: whole notes taken, released first, oldest first, with a fade
  (exclusive class reuses its fade);
- 04: The rest of the voice, and the reader's ignore rules (modulators can target every
  generator it adds).

**Status:** ready-for-agent

## Checklist

- [ ] File modulators at both levels are evaluated once per voice, at its note-on, as the
  2.04 specification defines:
  - Sources: velocity, key number, a constant, or a controller held at its General MIDI
    power-on value.
  - Transforms: curve type, polarity and direction.
  - Each also has an amount source and an amount.

  Each becomes a fixed offset to its destination generator for the voice's life. Nothing is
  evaluated per sample, and `process()` allocates nothing.
- [ ] Preset-level modulators add to instrument-level ones.
- [ ] The default modulators run through the same evaluation, with the values ticket 02
  established unchanged.
- [ ] A file modulator replaces a default one when their source, destination and transform
  match, **whatever their amount sources**. A synthetic zone carrying FluidR3's switch-off
  (source `0x0102`, note-on velocity, to `initialFilterFc`, with amount 0 and amount source
  `0x0D02`) plays with no velocity-to-cutoff drop.
- [ ] A link modulator, whose source is another modulator's output, stays ignored and listed.
  Illegal modulators stay ignored, as the specification says.
- [ ] Exclusive class:
  - When a note starts a zone with a non-zero `exclusiveClass`, every other sounding voice of
    that class fades linearly to zero over 6 ms from the new note's sample, and its slot is
    free at once.
  - The voices the note itself starts are spared, so a pair's two sides survive.
  - The scope is the whole node, on every channel.
- [ ] Output stays bit-identical across block sizes, including when a class choke and a pool
  take land on the same slot.
- [ ] `docs/node-io.md` records file modulators, the replacement rule and exclusive class.
- [ ] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.
