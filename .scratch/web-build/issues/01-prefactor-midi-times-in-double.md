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

**Status:** done

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

- [x] `MidiFile` keeps each entry's time and the file's duration in `double`, and parsing sums
  seconds in `double`. `fromEntries` takes `double`.
- [x] `MidiFileNode::prepare()` rounds `seconds × rate` to frames in `double`, with the same
  overflow and validity checks as today.
- [x] Persistence writes MIDI times with `double`'s `max_digits10` (17) digits and reads them
  back as `double`. A graph saved before this change opens unchanged. Saving it again gives the
  same bytes, checked against the committed fixture graph and a MIDI-file round trip.
- [x] No `long double` remains in the node library's MIDI code or in persistence.
- [x] A MIDI-file test pins the tie. A file at `Q:1/4=90` with an event at tick 15,000 lands on
  the frame `double` gives at 48 kHz; the probe found 1,000,001. The test passes natively.
  Ticket 05 runs it under Node, where `long double` would have put the event on 1,000,000.
- [x] The root build file adds `-ffp-contract=off` for GCC and Clang (Emscripten's Clang
  included) and nothing for MSVC, which keeps `/fp:precise`. Nothing adds `-ffast-math`,
  relaxed SIMD or flush-to-zero.
- [x] Local evidence with Emscripten 6.0.13, recorded under Comments:
  - an `emcmake` configure of this tree builds persistence and the authoring tests, so the
    libc++ break is gone;
  - the MIDI-file tests, the tie case included, pass under Node;
  - the compile commands carry `-ffp-contract=off`.

  No preset or CI job is added here; that is tickets 03 and 05.
- [x] Docs:
  - `docs/graph-persistence.md` says MIDI timestamps are stored with enough digits to recover
    the authored `double`;
  - the implementation conventions in `docs/project-foundations.md` record the floating-point
    flags: `-ffp-contract=off` for GCC and Clang, `/fp:precise` for MSVC, never fast maths or
    flush-to-zero.
- [x] Nothing changes under `kernel/` or `workspace/`. Kernel purity and the full suite pass
  (`docs/windows.md`), and `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/web-build-01-midi-times-in-double`, cut from a
freshly fetched `origin/main` at `4db75a1`.

- *What was built.*
  - `MidiFile` keeps entry times and the duration in `double`, `parse` sums seconds in
    `double`, and `fromEntries` takes `double`. A comment on the class says why `long double`
    is not used.
  - `MidiFileNode::prepare()` rounds `seconds × rate` in `double`. Its rate, finiteness,
    sign and overflow checks are unchanged.
  - The `modular.midi-file/1` codec writes `double`'s `max_digits10` (17) and reads `double`
    with `std::from_chars`. No `long double` remains in `nodes/`, `apps/`, `kernel/` or
    `workspace/` code.
  - The root `CMakeLists.txt` adds `-ffp-contract=off` for GCC and Clang. clang-cl, which
    defaults to contracting, gets the same option as `/clang:-ffp-contract=off`. Nothing is
    added for MSVC.
  - Docs:
    - `docs/graph-persistence.md`: the 17 digits, why older graphs keep their bytes, and the
      new coverage;
    - `docs/project-foundations.md`: the floating-point flags, as a convention.
  - Nothing under `kernel/` or `workspace/` changed.
- *Tests.* One doctest case in each of two existing suites.
  - `midi_file_tests`, "file times are summed and rounded in double so a half-frame tie lands
    alike in every build". The clip is map ticket 08's grid: `Q:1/4=90` as 666,667 µs, 480
    PPQ, a note every 40 ticks through tick 15,000. At 48 kHz, with a trigger at frame 101,
    the note at tick 15,000 lands on frame 101 + 1,000,001.
  - `authoring_tests`, "stored MIDI file times keep the digits that recover each double as
    earlier saves wrote them". A note from tick 40 to tick 15,000 at the same tempo gives
    times that need all 17 digits. The graph's saved text is pinned to what the earlier writer
    saved under MSVC. That text was captured from this test against the unchanged code. It
    opens to the same doubles bit for bit and saves the same bytes.
- *Red, then green.* Natively, both cases pass before and after the type change, as
  expected: MSVC's `long double` already is a 64-bit double. Their red is under
  Emscripten. Before the change:
  - under Node the tie case failed with `CHECK( 1000101 == 1000102 )`, frame 1,000,000;
  - persistence did not compile: `PrimitivePersistence.cpp:42:25: error: call to deleted
    function 'from_chars'`.

  After the change both pass under Node.
- *Local Emscripten evidence.* Emscripten 6.0.13 and its Node 24.19.0. The build was
  configured by hand, outside the repository, since the `web` preset is ticket 03's:

  ```powershell
  . "$env:USERPROFILE\emsdk\emsdk_env.ps1"
  emcmake cmake -S <checkout> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DCMAKE_CXX_FLAGS=-fwasm-exceptions" `
    "-DCMAKE_EXE_LINKER_FLAGS=-fwasm-exceptions -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1"
  cmake --build <build>
  ctest --test-dir <build> --output-on-failure
  ```

  - Every target builds, persistence and `authoring_tests` included: the libc++ break is
    gone.
  - `midi_file_tests` passes under Node, 16 of 16 cases, the tie case included.
    `kernel_tests`, `soundfont_tests` and `kernel_purity` pass too.
  - `authoring_tests` passes 24 of 28 cases under Node, the new case among them. Every
    MIDI-persistence case passes except one that fails on a file system call. The four
    failures, for ticket 05 to name:
    - `shared file save replaces complete graphs at Unicode paths and retains files on
      failure`: a temporary entry is left in the directory;
    - `recording graphs reopen with owned MIDI and settings but no transient take`:
      `remove` is not permitted on a Unicode directory;
    - `a client can run and release a graph on a worker before editing and running again`
      and `a worker reports a processing boundary failure and releases execution`:
      `thread constructor failed: Not supported`, since the build has no threads.
  - `node_tests` (two WAV-recorder file-system cases) and `midi_cli_smoke` (runs the `.js`
    directly) fail as map ticket 08 recorded.
  - `compile_commands.json`: all 19 compile commands carry `-ffp-contract=off`. None
    carries `-ffast-math`, `-mrelaxed-simd`, `-funsafe-math` or another `-ffp-contract`.
    CMake reports the compiler as Clang 24.0.0. The MSVC projects carry no such flag.
- *clang-cl.* None is installed here, so emsdk's `clang.exe` stood in for it, under that
  name, from a VS 2022 x64 developer environment. Its driver puts `-ffp-contract=on` on
  the cc1 line by default and `-ffp-contract=off` with `/clang:-ffp-contract=off`. Configured
  with Ninja, CMake reports "Clang 24.0.0 with MSVC-like command-line". All 24 compile
  commands carry `/clang:-ffp-contract=off`, and the kernel and MIDI libraries compile.
- *MSVC byte identity.* A throwaway harness outside the repository was built with MSVC,
  Release, against `origin/main` and against this branch. Each build saved four graphs:
  - the committed fixture `tests/fixtures/graph-version-1.modular`, opened and saved again;
  - a MIDI-file node holding `tests/fixtures/one-note.mid`;
  - one holding the tie grid above;
  - one holding a sweep of every whole-number BPM from 30 to 300, with 1,084 entries
    (63,118 bytes saved).

  The two builds saved byte-identical files. The fixture's resave equals the committed
  file, SHA-256 `d196995085a3fdabc564a1efea9c2faa95e8e6ca5ae567b29e8addbd343ebab6`. The
  branch build reopened every file `origin/main` saved and saved the same bytes. The same
  harness built with Emscripten saved, under Node, the same four files byte for byte.
- *Checks, on the final tree.*
  - Focused: `-Target ALL_BUILD -TestRegex '^(midi_file_tests|authoring_tests|kernel_purity)$'`.
  - Full suite through the script: Debug 9/9 and Release 9/9 CTest entries, both `desktop`
    tests and kernel purity included. Doctest cases: kernel 44, node 17, authoring 29,
    MIDI-file 16, soundfont 45.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at
  `8969454`, with a clean working tree. The fixes above are re-checked on the final tree.
  - Both axes: the review record was missing from this ticket. It is this entry.
  - Both axes: `NOT MSVC` left clang-cl, which contracts by default, without the option.
    Fixed with `/clang:-ffp-contract=off`, checked as recorded above.
  - Spec: the docs claimed that floating-point results are the same bits in every build.
    That is not true while library maths still differs (map ticket 08, D2). The convention
    and the CMake comment now say that no build fuses operations, so arithmetic gives the
    same bits.
  - Spec: `docs/project-foundations.md` added an undecided repo-wide rule against
    `long double`. Removed. The reason stays in `MidiFile.h`'s comment.
  - Standards: the new authoring case wrote out the same container text as the malformed-MIDI
    case's `encoded` lambda. Both now call one helper, `midiNodeGraph`. The case's `times`
    became `reopenedFile`, and its comment now says "a note from tick 40 to tick 15,000".
  - Kept, Standards: in `MidiFile::parse`, `static_cast<double>(ticks) * tempo /
    (static_cast<double>(division) * 1000000)` appears twice. A shared lambda would give the
    same bits, but the diff stays the probe's patch line for line. That patch is the code
    whose 107 million events map ticket 08 checked.
  - Kept, Standards: the repeated prose. The reason for `double` is in `MidiFile.h`'s
    comment, and each test's comment says only what that case pins. The flag rule's
    comment in `CMakeLists.txt` now points to the convention.
  - Kept, Spec: no in-repo test resaves `tests/fixtures/graph-version-1.modular` byte for
    byte. Its README promises that every later container version opens it, not that Save
    reproduces it, so such a test would tie the writer to version 1. The fixture also holds
    no MIDI-file node. The new authoring case is the in-repo guard for the changed codec,
    and the harness above checked the fixture.
  - Noted, not a defect: neither new case can fail natively, since MSVC computes the same
    bits before and after. Their red shows only under Emscripten, which CI runs from
    ticket 05.
