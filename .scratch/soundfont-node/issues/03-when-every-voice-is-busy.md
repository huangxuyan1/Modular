# 03: When every voice is busy: whole notes taken, released first, oldest first, with a fade

**What to build:** When the pool is full, a new note still sounds. The node takes whole notes,
released ones before held ones and oldest first, and fades each taken voice out over 6 ms, so
taking a voice does not click. A note that was taken later gets its note-off, and that note-off
releases nothing else. With this ticket, the default piano and the whole voice policy are
complete: **tickets 01 to 03 complete the website's route**. See the [spec](../spec.md)
(ticket 06's decisions).

**Blocked by:** 01: Tracer: a loaded soundfont plays notes at their pitch through the volume
envelope.

**Status:** ready-for-agent

## Checklist

- [ ] Stealing replaces ticket 01's interim rule. A note-on that finds too few free voices
  takes whole notes, every voice one note-on started, until it has the voices it needs. A
  stereo pair or a layered note never loses one side.
- [ ] Released notes go first, the one whose note-off came first first. Only when none is left
  are held notes taken, the one whose note-on came first first. Every note-on and note-off has
  a number in stream order, so there are no ties.
- [ ] A taken voice ramps linearly to zero over 6 ms from the new note's onset. The new note
  starts at its event's offset, with no delay.
- [ ] Each slot carries one fading tail beside its sample. A slot taken again within 6 ms stops
  its older tail at once, and a tail does not count against the limit. `prepare()` derives the
  fade's length in samples from the sample rate.
- [ ] Taken notes:
  - A count per channel and key tracks taken notes still awaiting their note-off.
  - When that note-off arrives, it is dropped and releases no other note.
  - A note-off that matches nothing still does nothing. The node keeps no credit for a later
    note-on.
- [ ] Tests at a small voice limit show:
  - releases taken before held notes;
  - oldest first;
  - whole stereo notes taken together;
  - the fade's length and shape;
  - a dropped note-off after a take;
  - a unison that both sounds and is taken note by note.
- [ ] `prepare()` allocates the voices, their tails and the 16 × 128 counts. `process()`
  allocates nothing. Allocation and taking depend only on the event stream, so output is
  bit-identical across block sizes even while the pool turns over.
- [ ] `docs/node-io.md`:
  - records the full voice policy and drops ticket 01's pool interim;
  - lists the known `MidiBlockNode` limitation. It keeps one held flag per channel and key, so
    at Stop, retrigger or a file's end, a second held note on the same key and channel keeps
    sounding. That bites only under a realtime transport. The ticket that first gives the node
    one, such as the editor's, counts held notes per channel and key in `MidiBlockNode`.
- [ ] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.
