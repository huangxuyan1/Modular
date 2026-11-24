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

**Status:** done

## Checklist

- [x] File modulators at both levels are evaluated once per voice, at its note-on, as the
  2.04 specification defines:
  - Sources: velocity, key number, a constant, or a controller held at its General MIDI
    power-on value.
  - Transforms: curve type, polarity and direction.
  - Each also has an amount source and an amount.

  Each becomes a fixed offset to its destination generator for the voice's life. Nothing is
  evaluated per sample, and `process()` allocates nothing.
- [x] Preset-level modulators add to instrument-level ones.
- [x] The default modulators run through the same evaluation, with the values ticket 02
  established unchanged.
- [x] A file modulator replaces a default one when their source, destination and transform
  match, **whatever their amount sources**. A synthetic zone carrying FluidR3's switch-off
  (source `0x0102`, note-on velocity, to `initialFilterFc`, with amount 0 and amount source
  `0x0D02`) plays with no velocity-to-cutoff drop.
- [x] A link modulator, whose source is another modulator's output, stays ignored and listed.
  Illegal modulators stay ignored, as the specification says.
- [x] Exclusive class:
  - When a note starts a zone with a non-zero `exclusiveClass`, every other sounding voice of
    that class fades linearly to zero over 6 ms from the new note's sample, and its slot is
    free at once.
  - The voices the note itself starts are spared, so a pair's two sides survive.
  - The scope is the whole node, on every channel.
- [x] Output stays bit-identical across block sizes, including when a class choke and a pool
  take land on the same slot.
- [x] `docs/node-io.md` records file modulators, the replacement rule and exclusive class.
- [x] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/soundfont-node-05-file-modulators-and-exclusive-class`,
cut from a freshly fetched `origin/main` at `bc33cf8`.

- *What was built.*
  - **The reader** (`nodes/src/SoundFont.cpp`) reads every modulator record. Per zone it
    keeps the legal ones that move something in a voice, the last of identical ones
    (identical: the same source, destination, amount source and transform). `zones()` gives
    each played zone its instrument-level and preset-level lists, each the global zone's
    modulators superseded by identical ones of the zone's own. It ignores, unlisted:
    - sources or amount sources the specification forbids or does not define: MIDI
      controllers 0, 6, 32 to 63, 98 to 101 and 120 to 127, general controllers other than
      0, 2, 3, 10, 13, 14 and 16, and curve types past 3; the link as an amount source;
    - transforms other than 0 and 2;
    - destinations that move nothing in a voice: unused, reserved and unknown generators,
      `instrument`, `keyRange`, `velRange`, `sampleID`, `keynum` and `velocity`; and, at the
      preset level, generators valid only in an instrument.

    Links stay listed, and a modulator with an amount to an effects send lists the sends.
    `SoundFont::place` and `SoundFont::bounded` are public now, so the node places a voice's
    points and keeps a modulated value as the reader keeps a generator; `Zone` carries its
    sample header's points instead of placed ones.
  - **The node** (`nodes/src/SoundFontNode.cpp`).
    - `prepare()` gives each zone its modulators in order: the default modulators no
      instrument modulator replaces, then the instrument's, then the preset's.
    - A note-on adds each modulator's output, for the zone's key and velocity, to the zone's
      generators, keeps each sum within its range (and whole where the generator counts
      whole things, the cutoff excepted), and derives the voice from the sums. Everything
      `prepare()` used to derive per zone from generators is now derived per voice; the
      voice carries it, so `render()` reads only the voice.
    - Sources map to fractions, numerator over denominator, and the amount is divided by
      the denominators first. So the default modulators come out bit for bit as ticket 02
      computed them; a plain product of the mapped values differed in the last place at 6 of
      the 127 velocities. CC7 comes before velocity in the defaults, as ticket 02 summed them.
    - A note's voices are made in room `prepare()` allocates, then each non-zero exclusive
      class among them ends every other sounding voice of its class, which moves to its
      slot's tail with the pool's 6 ms fade and frees the slot; only then does the pool take
      notes. `takeNote` shares that move (`fadeOut`).
  - **Docs**: `docs/node-io.md` records file modulators (sources, curves, normalization,
    output, levels, the replacement rule, ranges, address offsets and what is ignored),
    the defaults as the node plays them, and exclusive class.
- *Tests* (`tests/test_soundfont.cpp`, CTest `soundfont_tests`: 102 doctest cases, from 86).
  - **New:**
    - a constant modulator to each of 43 generators renders bit for bit as the generator
      moved by its amount, and each move is heard;
    - curves: the key through every curve, polarity and direction at nine keys, against the
      specification's formulas;
    - sources: velocity, key, the constant, and held controllers, with controllers in the
      stream moving nothing;
    - amount sources and the absolute transform;
    - ranges and whole values: pan clamped, attenuation floored at 0 cB, and a fractional
      root key and sample mode rounded;
    - replacement: FluidR3's switch-off (and the same with no amount source or CC1's) plays
      as the zone with its cutoff raised by the default's drop, bit for bit, at five
      velocities; a replacement at half the amount halves velocity's decibels; a modulator
      unlike the default by transform, curve or destination, or a preset's, replaces nothing;
    - levels: preset adds to instrument, local supersedes global at both levels, a global
      zone's modulator applies, the last identical one in a zone is kept;
    - ignored: destinations that move nothing, at both levels; sends listed only with an
      amount; illegal modulators, which also replace no default;
    - exclusive class: the 6 ms fade frame by frame at 44.1 and 48 kHz, held and released;
      own voices, other classes and no class spared, across channels, and a hi-hat struck
      again; the ended voice's slot free before the pool takes; a modulated class;
    - block sizes 1, 7, 127 and 509 against 512 at two rates, through a class's end and a
      take on one slot both ways round;
    - the third golden clip.
  - **Changed:** the busy font carries file modulators at both levels that leave every
    voice's life as it was, and the allocation test also runs the drum-kit stream.
- *Red.* The fixed-offset test failed first on all 43 generators but one, the sustain,
  whose step was not heard before its note-off; its values were corrected. The exclusive
  class tests failed first. The curve, source, amount, replacement, level and ignore tests
  passed at once, since the first slice's evaluation already covered them, so a sweep of 25
  mutants checked them; every mutant failed at least one test:
  - linear sources over 127; concave and convex swapped; direction ignored on unipolar
    curves; a bipolar curve's sign flipped; the switch on above half; the absolute
    transform ignored; the amount source ignored; the wheel's sensitivity 0;
  - replacement ignoring the transform, or needing the amount source; preset modulators
    replacing defaults; no range after modulation;
  - a class ending only off channel 9; an ended voice cut, not faded; the class ending
    after the pool takes;
  - curve type 4, controllers 32 to 63, general controller 1 and transform 3 allowed;
    identical modulators both kept; identity ignoring the amount source; preset modulators
    to instrument-only generators kept; a send modulator of amount 0 listed; global zone
    modulators dropped; whole generators not rounded.
- *Golden values.* The first two clips' values and web build 07's are unchanged. The golden
  font gains program 2, whose clip pins file modulators at both levels along every curve
  and exclusive classes ending voices across channels beside pool takes. Temporary counters
  confirmed the clip's and the block-size stream's comments, and were removed.
