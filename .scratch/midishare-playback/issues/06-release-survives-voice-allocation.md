# How does a released voice keep sounding until its release ends?

Type: grilling
Status: resolved
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

## Answer

**A note-off never frees a voice. A voice stays busy until its own sound ends, and only then
can a new note take it.** The pool counts sounding samples, 256 by default as in FluidSynth.
When every voice is busy, the node takes whole notes: released notes before held ones, oldest
first, each faded out over 6 ms. A key struck again gets a new voice beside the old one, and
note-offs end a key's held notes oldest first, so two parts in unison both sound. Nothing
changes under `kernel/` or `workspace/`, and no ADR is needed.

Every decision below was decided under the node-policy delegation in `AGENTS.md`,
2026-11-24. Like every planning record, it is provisional until its planning PR merges.
Section numbers refer to the SoundFont 2.04 specification.

### D1. A voice ends by itself, never at its note-off

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

A voice is one sounding sample. It is busy from its note-on until either of these happens:

- after its attack, its volume envelope has fallen 96 dB below its peak, in release or while
  the key is still held, in decay or sustain;
- its sample reaches its end point with no loop left to play.

A note-off starts the release and frees nothing. So a quick passage cannot cut a ringing
release: the next note takes a free voice, and takes a busy one only when none is free (D3).

- **Why 96 dB.** SoundFont 2's release falls linearly in dB from the current level, and its
  time is the time to fall 100 dB from full scale (§8.1.2, `releaseVolEnv`). The spec's
  reference amplifier, the EMU8000's, drops to zero gain at 96 dB of attenuation, a jump the
  spec calls inaudible in a 16-bit system (§9.1.7). The sample rules are §9.1.2's. An unlooped sample stops at its end
  point. A loop that holds until release plays on to the end point after the note-off. A loop
  that continues through release loops "until the note becomes inaudible", and the 96 dB floor
  is where that happens.
- **The envelope alone decides it.** After the attack, the envelope only falls, so a voice
  past the floor can never become audible again, whatever gains
  [Which part of the SoundFont format does the node read?](09-soundfont-format-subset.md) has
  the node apply on top. FluidSynth also frees a voice that has become too quiet, held or not
  (`fluid_rvoice.c`). Its floor is far lower, but it needs an estimate of the most gain the
  voice could still receive.
- **What follows.** A held note whose envelope decays to full attenuation frees itself. If a
  preset instead sustains at an audible level on a loop, its note sounds until its note-off.
  FluidR3's piano is the first kind
  ([Measure the FluidR3_GM piano before it becomes the default](10-measure-the-fluidr3-piano.md)).
  Its sustain is 100 dB, so a held note fades to silence within its hold and decay, at most
  about 50 s. Every zone loops continuously, so the 96 dB floor is the only way its voices end.
  Its releases last 0.4–1.2 s, by velocity. Once the last voice has ended, the node writes
  exact zeros.

### D2. The pool counts sounding samples, 256 by default

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **What it counts: voices, each one sounding sample.** A note on a stereo pair takes two, and
  a layered preset takes one per layer. FluidSynth's `synth.polyphony` counts the same unit:
  "A note event produces one or more voices." The cost of a render follows samples, not notes,
  and a count of notes could not size a fixed pool, because how many samples a note starts
  depends on the preset.
- **Default 256:** FluidSynth's default for the same unit, which is 128 stereo notes. (sfizz,
  an SFZ player, defaults to 64 and allows up to 256.)
- **What a dense clip needs.** At any moment the node needs about
  (held notes + note-ons per second × release time) × samples per note. If the pool fills, D3
  takes the oldest releases first. With a pool of 256, stereo samples and h held notes, each
  release then lives (128 − h) / r seconds, where r is the rate of note-ons. A release of R
  seconds (to fall 100 dB) is then 100 · (128 − h) / (r · R) dB down when it is taken.
  Keeping that at 60 dB or more, well under the notes just struck, allows
  r ≤ 100 · (128 − h) / (60 · R). The figures here use ticket 10's measurements: every
  FluidR3 piano note is a stereo pair, and its longest release is R = 1.2 s. With 16 held
  notes, the bound is about 155 note-ons per second.
  - **A busy clip:** two parts at Q=140, the right hand in 16ths and the left in four-note
    chords on 8ths, makes 28 note-ons per second. About 5 held and 34 releasing notes make
    about 77 voices.
  - **A pathological clip:** four voices of four-note chords in 16ths at Q=180 makes 192
    note-ons per second and would want about 490 voices. It fills the pool, but each release
    taken is about 49 dB down, under the 112 notes struck since its note-off. A clip of 5000
    characters holds about 17 seconds of such music.
  - **Today:** abcjs mixes every note into its buffer with a 200 ms fade (`fadeLength`) and
    never takes a voice. A site clip should not fill a pool of 256.
- **Cost and limits.** The pool is allocated in `prepare()`. A voice that is not sounding
  costs no time in `process()`, so a larger pool costs memory, not render time. The voice
  limit accepts 1 to 65535, FluidSynth's range, and any other value is an authoring error. A
  limit below the most voices one note of the selected preset can start would leave that
  note never sounding whole, so `prepare()` throws.

### D3. When every voice is busy: whole notes, released before held, oldest first

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **The unit is a note.** The node takes every voice that one note-on started, so a stereo
  pair or a layered note never loses one side. It takes notes until the new note has the
  voices it needs.
- **The order.** First the released notes, starting with the one whose note-off came first.
  Then, only if no released note is left, the held notes, starting with the one whose note-on
  came first. If ticket 09 has the node follow the sustain pedal, a note the pedal keeps counts
  as held, because SoundFont 2 keeps such a note "in the key-on state" (§9.2).
- **No ties.** Every note-on and note-off gets a number in stream order, so the choice depends
  only on the event stream.
- **Why oldest, not quietest.** A SoundFont 2 release falls at a fixed number of dB per
  second, so the note released first is the furthest down. For a piano, the note struck first
  has decayed most. Age comes from the events alone. A quietest rule would need each voice's
  level. Read from the envelope, the level misses the decay recorded in a piano sample. Read
  from the output, it needs a level meter on every voice. Either way it would depend on which
  gains ticket 09 adds.
- **What others do.** FluidSynth kills the voice with the lowest score, adding −2000 for a
  released voice, 1000 divided by the seconds since its note-on, and a loudness term. So a
  released voice less than half a second old outlasts an old held one. JUCE's `Synthesiser`
  also takes the oldest first, prefers a voice already playing the new pitch, and protects the
  lowest and highest held notes. D2 shows that a site clip almost never fills the pool, and that what it
  takes is deep in its release, so neither refinement is needed. Either can be added later as
  node policy.

### D4. The same key struck again: a new voice beside the old one, note-offs oldest first

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

A note's key is its channel and key number. The node plays every channel (the node-shape
decision's D2), and MIDI matches a note-off on both.

- **A note-on** for a key whose previous note still sounds, held or releasing, starts a new
  note beside it. The new note-on neither takes over nor releases the old note. A releasing
  note keeps releasing, and a held note keeps sounding until a note-off ends it.
- **A note-off** ends the oldest held note of its key. A note-off with no held note on its key
  does nothing.
- **A note that was taken (D3)** still has a note-off on its way. When it arrives, it is
  dropped and releases no other note. `VoiceAllocatorNode` behaves the same way: a note-off is
  dropped "if that note was already stolen". Held notes are taken oldest first, so on any key a
  taken note is older than every held one. A count per channel and key of taken notes still
  waiting for their note-off is therefore enough.
- **Why oldest first.** MIDI 1.0 leaves a repeated note-on to the receiver, but the
  transmitter "must send a corresponding Note Off message for every Note On sent", so each
  note-off ends one note. Two parts in unison then both sound, and the key sounds until its
  last note-off, whatever order the note-offs come in. Oldest first also plays an overlap
  correctly. abcjs makes slurred notes overlap on purpose
  (`slurredBreakBetweenNotes = -0.001`, "make the slurred notes actually overlap"), by 4 ms in
  the file (ticket 04's facts). So a slur over a repeated pitch writes the second note-on
  before the first note-off. Oldest first
  lets that note-off end the first note. Newest first would cut the second.
- **What others do. It is not oldest first.** FluidSynth, at its default `synth.note-cut` of
  0, releases every earlier voice of the key at the new note-on
  (`fluid_synth_release_voice_on_same_note_LOCAL`). A note-off then releases every voice of
  the key that is still on (`fluid_synth_noteoff_monopoly`). So on one channel, FluidSynth
  plays a unison as one note that ends at the first note-off, and it silences the second note
  of an overlapping slur. JUCE's `Synthesiser` also releases a ringing note when the same note
  is struck again.

### D5. A taken voice fades out over 6 ms

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **Fade, not cut.** A taken voice ramps linearly to zero over 6 ms, the fade-out of SFZ's
  default `off_mode=fast`. FluidSynth stops a voice it takes for polyphony at once
  (`fluid_voice_off`), which can click. The fade starts at the new note's onset, and that
  note's attack masks it.
- **Where the fade plays.** Each voice slot carries one fading tail beside its sample, so the
  new note starts at its event's offset with no delay. If a slot is taken again within 6 ms,
  the older tail stops at once. That can only happen if the whole pool turns over within
  6 ms. A tail does not count against the limit, so for 6 ms the slot plays two samples.
- `prepare()` derives the fade's length in samples from the sample rate (the node-shape
  decision's D4), and the fade advances per sample.

### D6. Block-size invariance, and nothing allocated in `process()`

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- The node allocates and takes voices at each event's sample offset, in stream order, by the
  numbers D3 gives. A voice's end (D1) and a fade (D5) advance per sample. So which voice plays
  what depends only on the event stream and stream time, at any block size.
- `prepare()` allocates the voices, their tails and the counts of taken notes, one per channel
  and key (16 × 128). `process()` allocates nothing.

### D7. No ADR, and no glossary change

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

This is policy inside one primitive node, reversed by changing that node, and it touches
neither the kernel nor the workspace. Voice, voice limit and release are node-library words.
They belong in `docs/node-io.md` when the node is built, as the node-shape decision says.

### The contract ticket's facts, weighed

The planning hub passed on two facts from
[What does the page hand Modular, and what does it get back?](04-page-to-modular-contract.md),
which its answer now records.

- **Unisons must both sound.** D4 gives this on one channel and across channels. abcjs gives
  each voice of the tune its own channel (`channel: i` in `abc_midi_flattener.js`), and a
  `%%MIDI channel` line can put two voices on one channel. FluidSynth's rule would not meet
  this on one channel.
- **Note-offs written before their note-ons.** abcjs's source confirms the cause. It
  computes a staccato gap as beats per second times 0.4 of the note's length
  (`abc_midi_flattener.js`). `abc_midi_create.js` multiplies that by beats per second again
  before subtracting it from the note's length in whole notes. So the gap grows with the
  square of the tempo, and above Q=95 in a simple meter it is longer than the note. Such a
  note-off does nothing, or ends an earlier held note of its key, and the staccato note stays
  held. Here is how this policy behaves:
  - **The limit and the order hold.** However many notes are stuck, the pool never grows, and
    D3 takes them in its usual order.
  - **Stuck notes do not pile up on a key.** Each stray note-off ends the key's oldest held
    note, so a key has at most one stuck note at a time. Stuck notes are bounded by the keys
    played staccato, not by the number of staccato notes. On FluidR3's piano, a stuck note
    fades to the 96 dB floor and frees itself within about 50 s (D1), and `MidiBlockNode`'s
    release when the file ends ends the rest. So the contract's "a piano would ring to the end
    of the file" is the worst case, not the rule.
  - **Worse, through matching:** oldest first passes the stuck state on. Suppose a key has a
    stuck note and a normal note is struck on it later. The normal note's note-off ends the
    stuck note, so the normal note sounds on until the key's next note-off. Newest first would
    leave the old stuck note ringing instead, but it cuts the slurred repeats D4 protects.
    FluidSynth's rule would clear a stuck note at the key's next note-on, but it fails both
    unisons and slurs.
  - **Slightly worse, through stealing:** a stuck note counts as held. If the pool ever fills,
    it outlasts fresh releases, where FluidSynth's age term would take it first. D2 shows that
    a site clip does not fill the pool.
  - **No repair.** The node does not keep an unmatched note-off as a credit for the key's
    next note-on. That would guess at the sender's intent, and the contract takes the bytes as
    they are and leaves fixing them to midishare.

### What this leaves to other tickets

- **[Which part of the SoundFont format does the node read?](09-soundfont-format-subset.md)**
  owns the envelope's shape and the gains on top of it (D1 uses only the envelope's fall), the
  sustain pedal (D3 ranks a pedal-held note as held) and exclusive class. Exclusive class could
  reuse D5's fade. FluidSynth instead gives the cut note a release of −2000 timecents, about
  0.3 s to fall 100 dB.
- **[What does the page hand Modular, and what does it get back?](04-page-to-modular-contract.md)**
  renders to a 2 s cap past the file's end and trims below −80 dBFS. D1 fits it: the file's
  end releases every note `MidiBlockNode` still marks held, and each voice then ends within
  its release, at most 1.2 s on FluidR3's piano.
- **`MidiBlockNode` keeps one held flag per channel and key**, so at Stop, retrigger or the end
  of a file it sends one note-off per key. Under D4, a second held note on that key, such as a
  unison on one channel, then keeps sounding. The site renders whole clips and stops in the
  page, so this is off the destination's path. Counting held notes per channel and key in
  `MidiBlockNode` would fix it. Whether and when is the planning hub's call, not this
  ticket's.
