# 03: When every voice is busy: whole notes taken, released first, oldest first, with a fade

**What to build:** When the pool is full, a new note still sounds. The node takes whole notes,
released ones before held ones and oldest first, and fades each taken voice out over 6 ms, so
taking a voice does not click. A note that was taken later gets its note-off, and that note-off
releases nothing else. With this ticket, the default piano and the whole voice policy are
complete: **tickets 01 to 03 complete the website's route**. See the [spec](../spec.md)
(ticket 06's decisions).

**Blocked by:** 01: Tracer: a loaded soundfont plays notes at their pitch through the volume
envelope.

**Status:** done

## Checklist

- [x] Stealing replaces ticket 01's interim rule. A note-on that finds too few free voices
  takes whole notes, every voice one note-on started, until it has the voices it needs. A
  stereo pair or a layered note never loses one side.
- [x] Released notes go first, the one whose note-off came first first. Only when none is left
  are held notes taken, the one whose note-on came first first. Every note-on and note-off has
  a number in stream order, so there are no ties.
- [x] A taken voice ramps linearly to zero over 6 ms from the new note's onset. The new note
  starts at its event's offset, with no delay.
- [x] Each slot carries one fading tail beside its sample. A slot taken again within 6 ms stops
  its older tail at once, and a tail does not count against the limit. `prepare()` derives the
  fade's length in samples from the sample rate.
- [x] Taken notes:
  - A count per channel and key tracks taken notes still awaiting their note-off.
  - When that note-off arrives, it is dropped and releases no other note.
  - A note-off that matches nothing still does nothing. The node keeps no credit for a later
    note-on.

  The count is the general one the planning hub's ruling asks for (next item): per channel and
  key, the note-ons and the note-offs that found a held note. A taken note's note-off is spent
  on it because it is its key's oldest held note.
- [x] *Added by the planning hub's ruling (2026-11-24, see Comments):* held-note bookkeeping
  does not depend on a voice's life. A note-off always ends its key's oldest held note, or is
  dropped if that note's voices already ended or were taken. Tested: a held note on a short
  unlooped sample ends by itself, a second note on the same key is struck, and the first
  note-off does not release the second note. The same test covers a held note whose envelope
  reaches the 96 dB floor and one that started no voice.
- [x] Tests at a small voice limit show:
  - releases taken before held notes;
  - oldest first;
  - whole stereo notes taken together;
  - the fade's length and shape;
  - a dropped note-off after a take;
  - a unison that both sounds and is taken note by note.
- [x] `prepare()` allocates the voices, their tails and the 16 × 128 counts. `process()`
  allocates nothing. Allocation and taking depend only on the event stream, so output is
  bit-identical across block sizes even while the pool turns over.
- [x] `docs/node-io.md`:
  - records the full voice policy and drops ticket 01's pool interim;
  - lists the known `MidiBlockNode` limitation. It keeps one held flag per channel and key, so
    at Stop, retrigger or a file's end, a second held note on the same key and channel keeps
    sounding. That bites only under a realtime transport. The ticket that first gives the node
    one, such as the editor's, counts held notes per channel and key in `MidiBlockNode`.
- [x] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — planning hub ruling, binding this ticket.** Decided by the planning hub under
the user's delegation, from ticket 01's open note on held notes whose voices end by themselves.

- *The problem.* A held note whose voices end by themselves, because its sample runs out or
  its envelope reaches the 96 dB floor while still held, dropped out of the held set. A later
  note-off on that key then released a *newer* note.
- *The ruling.* Held-note bookkeeping must not depend on a voice's life. Held notes are tracked
  per channel and key independently of whether their voices still sound, the same way the
  count of taken notes works. A note-off then always ends that key's oldest held note, or is
  dropped if that note's voices already ended or were taken.
- *Its test.* A held note on a short unlooped sample ends by itself. A second note on the same
  key is struck. The first note-off must not release the second note.

**2026-11-24 — implemented.** Branch `claude/soundfont-node-03-voice-stealing`, cut from a
freshly fetched `origin/main` at `95a1a1a`. Before the push it was rebased onto `6f1d9eb`
(web build 04, the soundfont extract tool), which touches none of this ticket's files. The
rebase was clean.

- *What was built*, all in `nodes/src/SoundFontNode.cpp` and its header:
  - **Held notes, counted per key.** For each of the 16 × 128 keys, the node counts its
    note-ons and its note-offs that found a held note. Each voice carries which of its key's
    note-ons started it. A note-off ends the key's oldest unmatched note-on: it releases that
    note's voices still sounding, or, if they were taken or ended by themselves, nothing. With
    no held note on the key it does nothing and counts nothing, so it leaves no credit. This
    one count covers both the ticket's taken notes and the ruling's notes that end by
    themselves.
  - **Stream numbers.** Every note-on and note-off gets the next number in stream order. A
    voice keeps its note-on's number and, once released, its note-off's.
  - **Taking.** A note-on counts the slots whose voice is free. While too few are free, it
    takes the note a full pool gives up first: released before held, released ones by
    note-off number, held ones by note-on number. All of that note's voices go together.
  - **Tails.** Each slot holds its voice and one tail. A taken voice is copied into its slot's
    tail, which replaces any older tail at once, and the slot's voice is freed. The tail keeps
    playing as the voice would, times a gain that falls from 1 on the new note's frame by one
    step of 1 / N per frame, so it is silent from frame N. It ends sooner if it ends by
    itself. A note-off never reaches a tail, and free slots count only voices, so a tail does
    not count against the limit.
  - **The fade's length:** `prepare()` sets N to 6 ms rounded to whole frames, at least one:
    288 frames at 48 kHz, 265 at 44.1 kHz.
  - **Order and allocation.** Slots render in order, each slot's voice before its tail, so
    every frame sums the same voices in the same order at any block size. `prepare()`
    allocates the slots, with their tails, and the counts. Taking copies one plain `Voice`.
  - **Messages that are not MIDI.** A message with a data byte of 128 or more is ignored, so
    a key byte that large cannot index outside the counts. The review found this.
  - Ticket 01's interim pool rule, its code and its test are gone. `docs/node-io.md` records
    the policy and the `MidiBlockNode` limitation. The class comment states the policy.
- *Tests* (`tests/test_soundfont.cpp`, CTest `soundfont_tests`: 66 doctest cases and 9,659
  assertions). Every test drives the node's authoring face and reads its rendered output.
  Most compare a render against one in which the taken note was never struck: once its fade
  is over, the two agree bit for bit.
  - **New:**
    - *the held-note ruling*: a held note whose sample ran out, whose envelope reached the
      floor, or which started no voice still takes its key's next note-off;
    - *order*: a released note goes before an older held one, released notes by note-off and
      held notes by note-on. A check confirms the released note still sounds when taken;
    - *the fade*: frame by frame, the taken note's level times (N − j) / N over N frames
      within one frame of 6 ms, at 44.1 and 48 kHz. The new note's channel equals the note
      played alone, so the new note starts at its frame and the tail takes no voice;
    - a slot taken again within 6 ms stops its older tail on that frame;
    - every voice of a stereo note is taken together;
    - a taken note's note-off is dropped, and the key's next note holds until its own;
    - a unison on one channel both sounds and is taken note by note;
    - a note message whose data bytes are not MIDI changes nothing.
  - **Changed:**
    - The stray note-off test adds a note-off before its key's note-on, which leaves no
      credit.
    - The free-slot test now needs the slot freed on time. Otherwise its new note would take
      an older held note.
    - The block-size test runs at pools of 4 and 10 at both rates. It checks that the pool
      turned over, because the render differs from one with room for every note. The busy
      stream gains a released note to take.
    - The allocation test runs at both pool sizes.
- *Red.* The ruling's test, the order subcases, the fade, the re-take and the non-MIDI test
  were each seen failing first. The third order subcase first passed because two DC notes of
  equal level cannot be told apart, so each note got its own velocity. The tests that passed
  at once were checked by mutating the node; each mutation was caught, then restored:
  - a take that frees one voice: the stereo test;
  - note-offs matched to the oldest sounding held note, the old rule: the ruling's test in all
    three cases, the dropped note-off and the unison;
  - a credit kept for an unmatched note-off: the no-credit test;
  - a slot freed one silent frame late: the free-slot test. A first attempt also changed the
    voice's sound, so the test stayed consistent with itself and passed. That mutation was
    replaced;
  - released notes ranked by note-on: order subcase 2;
  - held notes newest first: order subcase 3 and the unison;
  - a cut instead of a fade: the fade, stereo and re-take tests;
  - an older tail kept: the re-take test;
  - events handled at the block's start: the block-size test, among many;
  - a fade that advances once per render call: the fade, re-take and block-size tests;
  - a note-on that matches no zone left uncounted: the ruling's test, at velocity 10.
- *Checks, on the final tree*, after the rebase and the review fixes:
  - Script focused check: `-Target soundfont_tests -TestRegex '^soundfont_tests$'`.
  - Full suite through the script: Debug 10/10 and Release 10/10 CTest entries. These include
    both `desktop` tests, kernel purity and `soundfont_extract`, which the rebase brought in.
    Before the rebase, Debug ran 9/9.
  - `python scripts/check_docs.py`: 0 problems.
  - `git diff --check`: clean.
  - Nothing under `kernel/` or `workspace/` changed.
- *Judgement calls.*
  - **The count generalises the ticket's.** The ticket asks for a count of taken notes, and
    the ruling for held notes tracked the same way. Counting note-ons and matched note-offs per
    key gives both, because a taken held note is always its key's oldest held note (D4).
  - **A note-on that matches no zone is a held note of its key.** Under the ruling it is a
    held note whose voices ended at once, so its note-off is spent on it. The spec does not
    say this. The alternative lets that note-off release a later note on the key, which is
    the problem the ruling fixes. It is tested, at velocity 10.
  - Every note-off gets a stream number, including one that matches nothing. Only released
    notes' numbers are compared.
  - The fade's gain starts at 1 on the new note's frame, so its first frame is full and its
    last is 1 / N. N is 6 ms rounded to whole frames, not cut down or rounded up.
  - A tail keeps its envelope, loop, filter and pitch running under the ramp, as the voice
    would have.
  - The new note takes the lowest free slots, which may be the ones it has just freed.
  - A message with a data byte of 128 or more is ignored whole. That includes a note-off
    whose velocity byte is out of range, so the note it names stays held.

**Review.** `/code-review` ran on both axes over `git diff origin/main...HEAD` at `ed94051`,
the three commits before the review fixes, with a clean working tree.

- **Standards.**
  - Fixed:
    - *A latent out-of-range write:* a key byte of 128 or more from another node would have
      indexed past the counts. Such messages are now ignored, with a test and a line in the
      docs.
    - An even frame offset, 2104, became 2105.
    - A 103-column line in `docs/node-io.md` was reflowed.
    - `Voice::fade` became `fadeLeft`.
    - `16 * 128` became one constant beside `keyIndex`.
    - The ruling's test reuses `agreeFrom` instead of its own `std::equal`.
    - "(ticket 06, D5)" became "(map ticket 06, D5)".
    - The end-of-fade comment was reworded.
  - Kept, as judgement calls:
    - The channel and key travel as two bytes, as they did before this ticket.
    - The `KeyNotes` field comments stay, since they say what each count counts.
- **Spec.** It found nothing missing or wrong.
  - Fixed:
    - The zone-less note-on rule was not asked for and was untested. It now has a test and is
      recorded above.
    - The ruling's envelope-floor case is now tested.
    - The order test now checks that the released note still sounds when taken.
    - The busy stream's comment claimed events the test does not check. It now says what each
      pool size does, checked by tracing the stream by hand. The test checks that the pool
      turned over.

**Open notes for the planning hub.**

- *A note-on that matches no zone counts as a held note of its key.* See the judgement calls.
  It follows the ruling, but no spec line states it.
- *`MidiBlockNode`'s held flag* is documented, not fixed, as the hub decided. The ticket that
  first gives the node a realtime transport fixes it.
