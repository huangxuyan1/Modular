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
  `/code-review` runs on the branch's diff. The planning hub completed the review with Codex.

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
    - Choosing the preset (`select()`, at authoring time) gives each zone its modulators
      in order: the default modulators no instrument modulator replaces, then the
      instrument's, then the preset's.
    - A note-on adds each modulator's output, for the zone's key and velocity, to the zone's
      generators, keeps each sum within its range (and whole where the generator counts
      whole things, the cutoff excepted), and derives the voice from the sums. Everything
      `prepare()` used to derive per zone from generators is now derived per voice; the
      voice carries it, so `render()` reads only the voice, and `prepare()` only resets
      the pool.
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
- *Tests* (`tests/test_soundfont.cpp`, CTest `soundfont_tests`: 102 doctest cases, from 85).
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

  Two survived at first, controllers 32 to 63 and general controller 1 allowed: a source
  held at 0 gives 0 on a rising curve, so the illegal test could not hear them kept. Its
  sources now fall from 1, and both are caught.
- *Golden values.* The first two clips' values and web build 07's are unchanged. The golden
  font gains program 2, whose clip pins file modulators at both levels along every curve
  and exclusive classes ending voices across channels beside pool takes. Temporary counters
  confirmed the clip's and the block-size stream's comments, and were removed.
- *A heap check under Node, in web build's smoke test.* `web_smoke` checked that twenty more
  renders leave the module's heap in use exactly as the first render left it.
  - With each zone's modulator list rebuilt in every `prepare()`, it was 8 bytes over. The
    lists do not depend on the sample rate, so choosing the preset now makes them.
  - It then was 8 bytes under: 300,920 after the first render, 300,912 after the second
    and every one after. `main`'s node, built in this worktree, gives 231,224 both times.
  - The voices are larger now, so the pool is about 70 KB larger, which moves where later
    blocks land. dlmalloc keeps a remainder smaller than its smallest chunk inside the
    block it hands out, so the first render's count can differ by such a remainder
    without anything growing.
  - The test (`tests/test_web_smoke.mjs`) now compares from the second render on, still
    exactly, and `docs/web.md` says so. This is web build's file, changed because this
    ticket moved its count; the planning hub may prefer another form.
- *Judgement calls.*
  - **Bits.** The default modulators run through the general evaluation and keep ticket
    02's bits (fractions, and CC7 before velocity), shown by every golden value.
  - **Normalization.** Linear and switch sources over their range (128, or the pitch
    wheel's 16384), as ticket 02's velocity to cutoff and FluidSynth; unipolar concave and
    convex over 0 to 127, as ticket 02's velocity to attenuation; bipolar curves by the
    distance from the centre over half the range, as FluidSynth's, so a centred controller
    gives exactly 0 but 127 gives 0.75 on the concave curve, where 0 gives -1. The curves
    are capped at 1, which only a 14-bit source or a bipolar one can reach.
  - **No controller** as a primary source is the constant 1, as the 2.04 text says;
    FluidSynth drops such a modulator.
  - **Held controllers:** the pitch wheel's sensitivity at 2 semitones, General MIDI's,
    where the spec says "every other source 0". Only a file modulator with that source
    sees it.
  - **The defaults played:** CC7, velocity to attenuation and to cutoff, CC11, CC10, CC1
    and channel pressure. The sends' (the node has none) and the pitch wheel's (its
    destination is the pitch itself, and it adds nothing centred) are left out.
  - **Identity.** Superseding, local over global and within a zone (the last kept), uses
    the source, destination, amount source and transform. Replacing a default ignores the
    amount source, as D2 rules, and only an instrument-level modulator replaces one; a
    preset-level one adds, as 2.04's preset modulators add to instrument ones.
  - **An illegal modulator replaces nothing.** D3 ignores it, so a switch-off whose amount
    source is bank select leaves the default velocity-to-cutoff in place.
  - **Destinations.** Every generator a voice reads, except `keynum` and `velocity`, which
    the modulators read as their sources, and those that choose zones or name what they
    play; these are ignored unlisted. A modulator to a send with a non-zero amount lists the
    sends, though a negative one alone cannot make a send.
  - **Ranges.** A modulated sum is clamped to its generator's range and rounded, half away
    from zero, for whole-number generators. The cutoff is not clamped, as ticket 02 summed
    the velocity modulator into it; the filter caps it.
  - **Address offsets.** A note-on places a voice's points from its sample header and
    offsets; a modulated loop outside the points played plays unlooped, unlisted.
  - **Exclusive class.** A note's zone of a class ends its class even when its own voice
    ends before its first frame. Tails, already fading, are not ended again. A modulated
    class is rounded and kept within 0 to 127.
  - **Per-voice derivation.** Each voice now derives about a dozen portable exponentials
    and two sines at its note-on, which `prepare()` derived per zone before.
- *Review.* The planning hub completed the two-axis review with Codex. Its findings and
  fixes are recorded below.

### Codex review (2026-11-24)

The planning hub completed the review with Codex and accepted all seven findings
and the web smoke-test change. The notes above describe the pre-review implementation.

1. Prepare the selection before replacing the bank. Injecting a selection allocation
   failure proves the old bytes, preset and render survive.
2. Ignore modulators to Sample Generators unlisted at both levels. The tests cover
   every such destination, including a class modulator that cannot cause or stop a choke.
3. Sum identical instrument and preset modulator amounts before applying the transform.
   The absolute-value test covers opposing signs and sums wider than the on-disk amount.
4. Clamp file-modulated generators after adding their preset offsets and file modulators.
   A pan test distinguishes this from clamping the generator sum first.
5. Count only incoming voices that can play a frame when taking slots. A zero-frame
   zone leaves an existing voice in a full pool sounding unchanged.
6. Within a zone, the last record wins on source, destination and amount source;
   across zones, identity also includes the transform. Tests distinguish both keys.
7. Collect the incoming classes in a fixed bitset and end them in one pool pass.
   The layered callback regression compares many same-class layers against no class.
8. The web smoke checks zero held frames and exact linear memory after each repeat,
   with an 8-byte bound on the whole run's allocator variation. A missing release, a
   one-time retained allocation and a transient retained allocation each fail it.

**Verification.** The Windows Debug red run failed 8 of 108 SoundFont cases.
After the fixes, Windows Debug and Release each passed 12 of 12 CTest entries,
including both desktop tests. The Emscripten build passed 9 of 9 CTest entries,
including 108 of 108 SoundFont cases and 14 of 14 Node smoke cases. The program 2
fixture now uses legal class generators on both sides, and its updated golden
hashes match in Debug, Release and Node at all three rates.

**Open notes for the planning hub.**

- *For ticket 07, against FluidSynth:* the bipolar curves' asymmetry is FluidSynth's;
  FluidSynth drops a modulator with no controller as its primary source and scopes
  exclusive class per channel with a -2000 timecent release, where the node keeps the
  constant 1 and D8's whole-node 6 ms fade. FluidR3's census found no constant modulators.
- *Note-on cost:* the per-voice derivation above, for ticket 07's timings.
- *Unlisted ignores:* modulators to `keynum` or `velocity` are ignored unlisted. The
  specification states no rule for them; listing them would be a new entry.
