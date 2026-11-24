# 02: The render core: an instrument renders MIDI bytes to trimmed stereo, natively

**What to build:** A C++ client makes an *instrument* from a soundfont's bytes, once. It then
renders Standard MIDI File bytes at any sample rate into a left and a right channel, clip after
clip. Each rendering holds the whole release and ends at most two seconds after the file's end,
with the silent tail trimmed. This is the code the web module will export. It builds and is
tested on every platform, so the Windows jobs check it before any WebAssembly exists. See the
[spec](../spec.md) (The render core) and map ticket 04,
[What does the page hand Modular, and what does it get back?](../../midishare-playback/issues/04-page-to-modular-contract.md)
(decisions 4 to 6).

**Blocked by:** 01: Prefactor: MIDI times in `double`, and GCC and Clang never contract
floating-point. Both change `MidiFileNode`.

**Status:** ready-for-agent

**Left to later tickets:**

- the ten-minute limit, the `Engine::errorMessage()` check, and the full set of error paths
  (06);
- the exports and the loader (03);
- the golden clips (07).

The piano itself sounds right only after spec A 02 and 03, but this ticket needs only spec A
01, which is done.

## Checklist

- [ ] `MidiFileNode` imports from bytes as well as from a path. The import is edit-only. A
  failed import throws the parser's message, keeps the previous content and records the
  message, as the path import does. In the MIDI-file tests, a node imported from bytes renders
  the same frames as one imported from the same file's path, and a failed import keeps the
  content.
- [ ] A render-core library in `apps/web/` builds on every platform, MSVC included, with nothing
  from Emscripten. It links the kernel, the MIDI library and the soundfont library. It contains
  no midishare name, path or limit.
- [ ] An instrument is made from a soundfont's bytes. It loads them once into a soundfont node
  and keeps one graph: `TriggerAtNode(0)`, then `MidiFileNode`, then `SoundFontNode`, then the
  glue's stereo sink. Malformed bytes throw the node's `soundfont: …` message.
- [ ] In C++ only, the instrument takes a voice limit that defaults to the node's 256. A limit
  too small for one note of the preset fails with the node's own error. No other option exists.
- [ ] A render takes MIDI bytes and a rate:
  - A rate that is not finite and positive throws a `render: …` message before anything is
    allocated.
  - The bytes are imported into the kept `MidiFileNode`. Malformed bytes throw its message, and
    the instrument still renders the next clip.
  - Each render builds a fresh `Engine` on the kept graph and processes the file's frames plus
    the cap, two seconds of frames at the rate.
  - The result carries the requested rate, unchanged, and two channels of equal length.
- [ ] The trim: the length is the file's frames, plus the tail up to and including its last
  frame where either channel's magnitude reaches `1e-4f` (−80 dBFS), with a two-second cap.
  - The threshold is a float literal, not computed.
  - The file's own frames are never cut, even when they end in silence.
  - A file of zero duration gives zero frames.
  - Tests cover a release that ends inside the cap, one that is cut at the cap, a file ending
    in a silent stretch, a quiet note above and below the threshold, and an empty file.
- [ ] The glue's sink is private to `apps/web/`. It is preallocated and planar, sized before each
  render from the file's duration and the rate. It resets in `prepare()`, allocates nothing in
  `process()` and never writes past its capacity. `CaptureSinkNode` is not used.
- [ ] No gain is wired: a render's samples equal those of a hand-built graph of the same nodes
  rendered into a two-channel `CaptureSinkNode` over the same frames, sample for sample, at two
  rates and two block sizes.
- [ ] One instrument renders clip after clip. The same clip rendered twice gives the same
  samples, before and after a different clip and after a failed render.
- [ ] A new doctest executable joins CTest: the render core's tests (seam S1). It runs in the
  Windows Debug and Release jobs. `docs/windows.md`'s description of the suite is updated.
- [ ] `docs/node-io.md`'s `MidiFileNode` table lists the import from bytes.
- [ ] Nothing changes under `kernel/` or `workspace/`. Kernel purity and the full suite pass
  (`docs/windows.md`), and `/code-review` runs on the branch's diff.
