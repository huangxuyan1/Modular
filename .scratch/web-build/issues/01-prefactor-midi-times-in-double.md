# 01: Prefactor: MIDI times in `double`, and GCC and Clang never contract floating-point

**What to build:** A MIDI event lands on the same frame in every build of Modular, and
persistence compiles with any standard library. MIDI times are kept and converted in `double`
instead of `long double`, and GCC and Clang are told never to fuse floating-point operations.
Under MSVC, whose `long double` already is a 64-bit double, nothing a client can observe
changes: every render and every saved graph stays as it is. The change makes the web build's
bit-for-bit claim possible and removes the libc++ persistence break. See the
[spec](../spec.md) and map ticket 08,
[How is a browser render checked against a native one?](../../midishare-playback/issues/08-browser-render-checked-against-native.md)
(decisions 3 and 4).

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

**Why.**

- **Frame ties.** Map ticket 08's probe replayed MIDI timing over every whole-number tempo from
  30 to 300 at four rates. MSVC's 64-bit and wasm32's 128-bit `long double` put 9,881 of
  21.5 million events on different frames, each on an exact half-frame tie. For example, at
  `Q:1/4=90` and 48 kHz, tick 15,000 is frame 1,000,000.5; MSVC places it at 1,000,001 and
  WebAssembly at 1,000,000. The same code in `double` agreed on all 107 million events tried.
- **The persistence break.** Persistence reads stored MIDI times with `std::from_chars` on a
  `long double`, which libc++ does not provide (map ticket 03, decision 2).
- **A starting point.** Map ticket 02's probe committed an optional patch that makes the change
  in `MidiFile` and `MidiFileNode` (under its prototype's `patches/`). This ticket extends it to
  persistence.

## Checklist

- [ ] `MidiFile` keeps each entry's time and the file's duration in `double`, and parsing sums
  seconds in `double`. `fromEntries` takes `double`.
- [ ] `MidiFileNode::prepare()` rounds `seconds × rate` to frames in `double`, with the same
  overflow and validity checks as today.
- [ ] Persistence writes MIDI times with `double`'s `max_digits10` (17) digits and reads them
  back as `double`. A graph saved before this change opens unchanged. Saving it again gives the
  same bytes, checked against the committed fixture graph and a MIDI-file round trip.
- [ ] No `long double` remains in the node library's MIDI code or in persistence.
- [ ] A MIDI-file test pins the tie. A file at `Q:1/4=90` with an event at tick 15,000 lands on
  the frame `double` gives at 48 kHz; the probe found 1,000,001. The test passes natively.
  Ticket 05 runs it under Node, where `long double` would have put the event on 1,000,000.
- [ ] The root build file adds `-ffp-contract=off` for GCC and Clang (Emscripten's Clang
  included) and nothing for MSVC, which keeps `/fp:precise`. Nothing adds `-ffast-math`,
  relaxed SIMD or flush-to-zero.
- [ ] Local evidence with Emscripten 6.0.13, recorded under Comments:
  - an `emcmake` configure of this tree builds persistence and the authoring tests, so the
    libc++ break is gone;
  - the MIDI-file tests, the tie case included, pass under Node;
  - the compile commands carry `-ffp-contract=off`.

  No preset or CI job is added here; that is tickets 03 and 05.
- [ ] Docs:
  - `docs/graph-persistence.md` says MIDI timestamps are stored with enough digits to recover
    the authored `double`;
  - the implementation conventions in `docs/project-foundations.md` record the floating-point
    flags: `-ffp-contract=off` for GCC and Clang, `/fp:precise` for MSVC, never fast maths or
    flush-to-zero.
- [ ] Nothing changes under `kernel/` or `workspace/`. Kernel purity and the full suite pass
  (`docs/windows.md`), and `/code-review` runs on the branch's diff.
