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

**Status:** done

**Left to later tickets:**

- the ten-minute limit, the `Engine::errorMessage()` check, and the full set of error paths
  (06);
- the exports and the loader (03);
- the golden clips (07).

The piano itself sounds right only after spec A 02 and 03, but this ticket needs only spec A
01, which is done.

## Checklist

- [x] `MidiFileNode` imports from bytes as well as from a path. The import is edit-only. A
  failed import throws the parser's message, keeps the previous content and records the
  message, as the path import does. In the MIDI-file tests, a node imported from bytes renders
  the same frames as one imported from the same file's path, and a failed import keeps the
  content.
- [x] A render-core library in `apps/web/` builds on every platform, MSVC included, with nothing
  from Emscripten. It links the kernel, the MIDI library and the soundfont library. It contains
  no midishare name, path or limit.
- [x] An instrument is made from a soundfont's bytes. It loads them once into a soundfont node
  and keeps one graph: `TriggerAtNode(0)`, then `MidiFileNode`, then `SoundFontNode`, then the
  glue's stereo sink. Malformed bytes throw the node's `soundfont: …` message.
- [x] In C++ only, the instrument takes a voice limit that defaults to the node's 256. A limit
  too small for one note of the preset fails with the node's own error. No other option exists.
- [x] A render takes MIDI bytes and a rate:
  - A rate that is not finite and positive throws a `render: …` message before anything is
    allocated.
  - The bytes are imported into the kept `MidiFileNode`. Malformed bytes throw its message, and
    the instrument still renders the next clip.
  - Each render builds a fresh `Engine` on the kept graph and processes the file's frames plus
    the cap, two seconds of frames at the rate.
  - The result carries the requested rate, unchanged, and two channels of equal length.
- [x] The trim: the length is the file's frames, plus the tail up to and including its last
  frame where either channel's magnitude reaches `1e-4f` (−80 dBFS), with a two-second cap.
  - The threshold is a float literal, not computed.
  - The file's own frames are never cut, even when they end in silence.
  - A file of zero duration gives zero frames.
  - Tests cover a release that ends inside the cap, one that is cut at the cap, a file ending
    in a silent stretch, a quiet note above and below the threshold, and an empty file.
- [x] The glue's sink is private to `apps/web/`. It is preallocated and planar, sized before each
  render from the file's duration and the rate. It resets in `prepare()`, allocates nothing in
  `process()` and never writes past its capacity. `CaptureSinkNode` is not used.
- [x] No gain is wired: a render's samples equal those of a hand-built graph of the same nodes
  rendered into a two-channel `CaptureSinkNode` over the same frames, sample for sample, at two
  rates and two block sizes.
- [x] One instrument renders clip after clip. The same clip rendered twice gives the same
  samples, before and after a different clip and after a failed render.
- [x] A new doctest executable joins CTest: the render core's tests (seam S1). It runs in the
  Windows Debug and Release jobs. `docs/windows.md`'s description of the suite is updated.
- [x] `docs/node-io.md`'s `MidiFileNode` table lists the import from bytes.
- [x] Nothing changes under `kernel/` or `workspace/`. Kernel purity and the full suite pass
  (`docs/windows.md`), and `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/web-build-02-render-core-native`, cut from a
freshly fetched `origin/main` at `95a1a1a`. Before its first push it was rebased onto
`6f1d9eb`, where ticket 04 had landed; `CMakeLists.txt` and `docs/windows.md` kept both sides.

- *What was built.*
  - `MidiFileNode::import(std::span<const std::uint8_t>)` beside `import(path)`. Both go
    through one private helper, `replaceFile`. A failed byte import throws the parser's
    message, records it in `importError()` and keeps the previous content, as the path
    import does.
  - `modular_web_core` in `apps/web/`. It links the kernel, the MIDI library and the
    soundfont library, and nothing from Emscripten.
    - `modular::web::Instrument(soundFontBytes, voiceLimit = 256)` loads the bytes once into
      a soundfont node. It keeps one graph: `TriggerAtNode(0)` → `MidiFileNode` →
      `SoundFontNode` → the render core's sink. No gain is wired.
    - `render(midiBytes, sampleRate)` returns a `Rendering`: the requested rate, and `left`
      and `right` of equal length. In order:
      1. it checks the rate (`render: sample rate must be finite and positive`);
      2. it imports the bytes into the kept node;
      3. it builds a fresh `Engine` with 512-frame blocks;
      4. it renders the file's frames plus the cap;
      5. it trims.
    - The trim keeps the file's frames whole. It then keeps the tail up to its last frame
      where either channel's magnitude is at least `1e-4f`. A file of zero duration gives
      two empty channels.
    - `StereoSinkNode` (`apps/web/StereoSinkNode.h`) has planar left and right buffers.
      - `setCapacity()` sizes them before the first block and restarts the write
        position, and `prepare()` resets that position.
      - `process()` copies at most the room left, so it never writes past capacity. It
        allocates nothing.
      - `take()` moves the written frames out. A rendering therefore takes the sink's
        buffers instead of copying them, and a render holds one set of buffers.
      - `CaptureSinkNode` is not used.
  - Docs:
    - `docs/node-io.md`: the byte import;
    - `docs/windows.md`: eleven CTest entries with ticket 04's, nine of them headless, and
      the CI list;
    - `docs/project-foundations.md`: the layer and test lists name `apps/web/`.
  - Nothing under `kernel/` or `workspace/` changed. No midishare name, path or limit is in
    the code.
- *Choices made here.*
  - **The sink is sized after the Engine's compile, before its first block.**
    - The spec sizes the sink in step 4, before a fresh Engine renders in step 5.
    - The file's frames come from `MidiFileNode::durationFrames()`, which the node sets in
      `prepare()`. So the render core reads them once the Engine is built, rather than
      repeat the node's rounding.
    - G2 holds: the sink is sized from the file's duration and the rate before any frame
      is rendered, and `prepare()` resets it.
    - Ticket 06's ten-minute check can still read `file().durationSeconds()` right after
      the import, before the Engine is built, and so refuse before anything is allocated.
  - **The cap is `floor(2 s × rate)` frames**, so a rendering never ends more than two
    seconds after the file's end. At whole-number rates it is exactly `2 × rate`.
  - **A file of zero duration still builds the Engine.** A node's prepare error, such as a
    voice limit that is too small, still surfaces, but no frame is rendered.
  - **`render: too many frames`.**
    - Frame counts are `size_t` because they size the sink, and `size_t` is 32 bits on
      wasm32.
    - When the file's frames plus the cap do not fit, the render throws rather than
      wrapping. A huge finite rate reaches this on any platform.
    - It is tested. Ticket 06 owns the error list and may add it there.
  - **The sink's own tests.**
    - Two cases drive `StereoSinkNode` in its own small graph. They cover: no allocation
      in `process()`, planar channels, the stop at capacity, and the reset in `prepare()`.
    - No instrument test can see these G2 properties. An instrument never fills its sink
      past capacity, and it sizes the sink before each render.
    - The Testing Decisions say a test never reaches into "the sink's buffers". These two
      cases are a named exception, like the golden tests. They never touch an
      instrument's graph, and every instrument test goes through `Instrument` alone.
    - The header sits beside `Instrument.h`. "Private to `apps/web/`" is read as: no
      node-library sink exists.
- *Tests.*
  - `midi_file_tests`, two cases:
    - "a node imported from bytes plays the same frames as one imported from the file's
      path", at 44.1 and 48 kHz on the `one-note.mid` fixture;
    - "a failed import from bytes throws the parser's message and keeps the previous
      content".
  - `web_core_tests` is a new executable in CTest (seam S1), with 14 cases. Its soundfonts
    come from `tests/SoundFontTestFiles.h` and its MIDI files from a writer in the test
    file, so it uses no licensed file.
    - **No gain.** Renders equal the same nodes wired by hand into a two-channel
      `CaptureSinkNode`, sample for sample. This holds at 44.1 and 48 kHz, with blocks of
      127 and 509.
    - **Clip after clip.** Renders are compared with a fresh instrument's, before and after
      another clip and after two failed renders.
    - **The trim:**
      - a release ending inside the cap;
      - one cut at the cap;
      - a file ending in a silent stretch;
      - a quiet note above the threshold, and one below it whose cut tail is non-zero;
      - zero-duration files, one empty and one with a note at tick 0.
    - **Errors:**
      - the soundfont reader's message for MIDI bytes;
      - the sf3 message;
      - the MIDI parser's message;
      - each bad rate, refused before the bytes are read;
      - the frame-count guard.
    - **The voice limit.**
      - The default equals 256 and differs from 257, with 257 notes at once.
      - A limit of 1 against a two-voice preset fails with the node's prepare error.
      - A limit of 0 fails with the node's range error.
    - The sink's own two cases, above.
- *Red, then green.*
  - The MIDI-file cases first failed to compile, because there was no byte import.
  - The render core's cases then ran against stub bodies that had the final interface.
    Every instrument case failed, and all pass with the implementation.
  - Writing them caught a most-vexing parse: `CHECK_THROWS_WITH_AS(Instrument(chord()), …)`
    declared a function, so it could never throw. It now uses braces.
- *Checks, on the final tree.*
  - Focused: `-Target ALL_BUILD -TestRegex '^(midi_file_tests|web_core_tests|authoring_tests|kernel_purity)$'`.
  - Full suite through the script:
    - Debug 11/11 and Release 11/11 CTest entries, including both `desktop` tests, the
      extract tool's test and kernel purity;
    - doctest cases, the same in both configurations: kernel 44, node 17, authoring 29,
      MIDI-file 18, soundfont 59, render core 14.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD`, at `6283213`
  before the rebase, with a clean working tree. The fixes were re-checked on the final
  tree.
  - Standards: no hard violation. Fixed:
    - lines over about 90 columns in the new code, now wrapped;
    - a bare "core", which `CONTEXT.md` reserves as a word to avoid for the kernel, now
      "render core";
    - names: `replace` became `replaceFile`, distinct from the inherited `replaceContent`.
      `kAudible` became `kTailThreshold`, `kMost` became `kMostFrames`, and the test's
      `cap()` became `capFrames()`.
  - Both axes: `setCapacity()` kept the write position. A capacity shrunk after frames were
    written would then have underflowed the room left. It now restarts the position.
  - Standards, kept as judgement calls:
    - **Possible Data Clumps.** `Rendering`, the sink's `Channels` and the test's `Stereo`
      each carry left and right. `Rendering` is the client's result, with the rate.
      `Channels` is the sink's private hand-off. `Stereo` also carries the file's frames.
    - **Possible Duplicated Code.** The allocation-counting `operator new` and the byte
      helpers repeat code from other test files.
      - Helpers stay in each test file, as `docs/project-foundations.md` asks, and each
        executable replaces `operator new` itself.
      - A shared header would reach into the soundfont suite, which another session is
        editing.
    - **Frame counts in `size_t`** rather than a fixed width, because they size vectors.
  - Spec. Fixed:
    - the ticket's record was missing; this entry is it;
    - `render: too many frames` was untested; it is now tested.
  - Spec, kept and recorded above: the sizing order and the two sink cases.
    - The reviewer suggested dropping the fresh-Engine sink case, as it repeats the
      clip-after-clip test.
    - `setCapacity()` now restarts the position too, so that case is the only check of
      G2's reset in `prepare()`. It stays.
  - Spec, kept: the `docs/project-foundations.md` edit. Its layer list would otherwise
    leave out `apps/web/`.
