# 07: Golden clips: the glue renders bit for bit natively and under Node

**What to build:** A reviewer sees every change to what Modular renders through its web surface,
and a portability fault fails CI in the pull request that makes it. Three clips are rendered
through the render core at three rates, and their frame counts and per-block hashes are pinned
in a test: a timing tie, a full voice pool and a one-note preview. The same test passes in the
Windows jobs and under Node in the `web` job, so the browser's render equals the native one by
transitivity. The Node smoke test checks the preview's hash through the JavaScript surface. See
the [spec](../spec.md) (Testing Decisions) and map ticket 08,
[How is a browser render checked against a native one?](../../midishare-playback/issues/08-browser-render-checked-against-native.md)
(decisions 1, 5, 6 and 7).

**Blocked by:**

- 05: The `web` CI job under `CI gate`, its artifact, the version and the manifest. The Node
  half runs there.
- Spec A's
  [03: When every voice is busy](../../soundfont-node/issues/03-when-every-voice-is-busy.md).
  The pool clip pins notes taken with the 6 ms fade.
- Spec A's [08: portable maths](../../soundfont-node/issues/08-portable-maths.md). Without it the
  node's output may differ between compilers, so the claim would hold only by luck.

**Status:** done

## Checklist

- [x] The golden hash is defined once in a test header, and spec A 08's golden test uses the
  same one. If spec A 08 has landed, this ticket reuses its definition; otherwise it defines
  it there for spec A to share. The hash is 64 bits and computable exactly in JavaScript, for
  example FNV-1a over each sample's IEEE bits, frames in order, left before right.
- [x] Three clips, written by the test, each with its soundfont built in memory by spec A's
  test-side builder:
  1. **Timing:** an event every 40 ticks at `Q:1/4=90` through tick 15,000, the case that split
     under `long double`, then a tempo change.
  2. **Pool:** a voice limit of 4, set through the render core's C++ parameter, under a dense
     passage, so notes are taken with the 6 ms fade.
  3. **Preview:** one note held 0.25 s at velocity 105, then its release.

  The soundfont exercises what the node does when this ticket lands: a stereo pair panned
  ±500, a root key away from the notes played, and a sample whose rate is not the graph's.
- [x] Each clip renders through the render core (seam S1) at 44.1, 48 and 22.05 kHz.
  - The test checks the frame count and a hash of each 4,096-frame block, as values committed
    in it.
  - On a mismatch it prints the new values and the first differing block.
- [x] The same test passes natively in Debug and Release and under Node in the `web` job, with
  the same values.
- [x] The Node smoke test (seam S2) renders the preview clip through the loader at 48 kHz and
  checks the same frame count and block hashes. This covers copy-out, channel order and the
  trim. The fixture writer writes the preview's soundfont and MIDI bytes, so both sides render
  the same input.
- [x] Spec A 08's golden test passes under Node in the `web` job, which is the Node half that
  spec A 08 left to this spec.
- [x] Docs:
  - `docs/web.md` says a change that alters a rendered clip updates these values in the same
    commit.
  - A later spec A ticket that changes the node's output (02, 04 or 05, if any lands after this
    one) updates these values the same way. The tests' description in `docs/windows.md` is
    updated.
- [x] Nothing changes under `kernel/` or `workspace/`. The full native suite and the `web` job
  pass, and `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/web-build-07-golden-clips`, cut from a freshly
fetched `origin/main` at `3c0aad4` and rebased, before its first push, onto `1febc11` (web build
06, merged meanwhile). The rebase met 06 in two places, the smoke test's arguments in
`CMakeLists.txt` and the head of `tests/test_web_smoke.mjs`; both sides were kept. Nothing under
`kernel/`, `workspace/`, `nodes/` or `apps/` changes.

- *What was built.*
  - `tests/GoldenRender.h`, from spec A 08, is the one hash, unchanged; spec A 08's golden test
    and this ticket's both use it.
  - `tests/WebGoldenClips.h` holds the soundfont, the three clips and their golden values.
    - The soundfont, from the shared builder: a stereo pair of looping tones panned -500 and
      +500, recorded at 32 kHz and rooted at 55, under a full volume envelope and a cutoff the
      modulation envelope sweeps; from key 72 a third voice, a chime at 25 kHz rooted at 84,
      played once to its end. The clips play keys 57 to 80, so no note hits a root. The tones
      are made from integers only, so the bytes do not depend on a maths library.
    - **Timing:** Q:1/4=90 as 666,667 us a quarter at 480 ticks, a note every 40 ticks from
      tick 0 through tick 15,000, then the tempo moves to 140 a quarter (428,571 us) at tick
      15,040 and 24 more notes follow. Default voice limit.
    - **Pool:** 32 notes on four channels from tick 3, 22, 22, then 7 ticks apart, each held
      71 ticks, then a three-note chord held 206 ticks. Voice limit 4, through the
      `Instrument`'s C++ parameter. The test also checks that the limit changes the render,
      so notes are taken.
    - **Preview:** middle C held 0.25 s (240 ticks) at velocity 105; the file ends at the
      note-off, so the release is the rendering's tail.
  - `tests/MidiTestFiles.h`: a small Standard MIDI File writer, tempo events included, beside
    the soundfont builder.
  - `tests/test_web_golden.cpp`, compiled into `web_core_tests` (seam S1), so no CTest entry is
    added: three cases, one per clip, each rendering through a fresh `Instrument` at 44.1, 48
    and 22.05 kHz. A mismatch names each differing rate's frame count and first differing
    block, then prints every rate's values as C++ to paste.
  - `tests/web_smoke_fixture.cpp` also writes the preview's soundfont, MIDI bytes and 48 kHz
    values (from the header) into `build/web/web-smoke-golden/`. This replaces the spec's "The
    test builds its MIDI bytes itself" for the preview, as the ticket asks.
  - `tests/test_web_smoke.mjs` (seam S2) ports the hash with BigInt and renders the preview
    through the shipped loader at 48 kHz against those values; a mismatch names the first
    differing block and prints the JavaScript values.
  - Docs: `docs/web.md` gains **Golden clips**, with the rule that a change altering a rendered
    clip updates the values in the same commit; `docs/node-io.md` says a change to the node's
    output updates these values too, which covers a later spec A 04 or 05; `docs/windows.md`
    describes the tests.
- *The golden values* (frames, then block hashes):

  | Clip | 44.1 kHz | 48 kHz | 22.05 kHz |
  | --- | --- | --- | --- |
  | Timing | 964,044 (236) | 1,049,301 (257) | 482,023 (118) |
  | Pool | 45,804 (12) | 49,855 (13) | 22,903 (6) |
  | Preview | 15,058 (4) | 16,390 (5) | 7,530 (2) |

  The preview at 48 kHz, the one the smoke test checks, starts `0x7597ec3240543af8`.
- *Red, then green.*
  - The three cases first ran with no values, failed naming every rate's frame count, and
    printed the values pasted in.
  - A timing hash corrupted by hand made the C++ test report "48000 Hz: 1049301 frames (golden
    1049301), first differing block 4 (frames 16384 on)"; a preview hash corrupted in the
    fixture's JSON made the smoke test report "first differing block 2 (frames 8192 on)" with
    the JavaScript values. Both were restored.
  - After review the pool clip moved to tick 3 and its values were regenerated natively; the
    timing and preview values did not change, so moving the MIDI writer changed no byte.
- *Checks, on the final tree.*
  - Native, through the script: Debug 11/11 and Release 11/11 CTest entries, both `desktop`
    tests included. The golden values are the same in both.
  - `web`, Emscripten 6.0.13 and its Node on Windows: 9/9 CTest entries, with the same
    values. doctest cases: render core 21 (the three golden cases included), soundfont 67
    (spec A 08's golden test included); smoke test 14. Under Node the render core's tests
    take 0.7 s, the 21.9 s timing clip at three rates included.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
- *CI* (pull request #38, run `45466321587`, head `aafccc5`): every job green and `CI gate`
  passed. The `web` job, on `ubuntu-24.04` with Emscripten 6.0.13 and Node 24.19.0, took 67 s
  and passed 9/9 CTest entries: `web_core_tests` with the golden clips, `soundfont_tests` with
  spec A 08's golden test (its Node half), and `web_smoke` with the preview's hash.
  `windows (Debug)` and `windows (Release)` each passed 9/9 headless entries.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at `f639fcf`, with
  a clean working tree.
  - Standards, fixed:
    - odd offsets (`project-foundations.md`): the pool clip now starts at tick 3. The timing
      grid starts at tick 0, as the probe's did, and the preview at the file's start, as a
      client's preview does; their comments say so;
    - **possible Divergent Change** and **Duplicated Code**: the MIDI writer moved out of the
      golden header into `tests/MidiTestFiles.h`;
    - **possible Mysterious Name:** `variable` is `variableLength`, the fixture's `write` is
      `writeFile`;
    - an unclear comment on the soundfont, a 98-column line in the smoke test, a stray short
      line in `docs/windows.md`, and `docs/web.md` saying a mismatch names "each rate's"
      frame count (it names each differing rate's; the Spec axis found this too).
  - Standards, kept:
    - **possible Duplicated Code:** `tests/test_web_core.cpp` keeps its own MIDI writer, and
      the golden `tone` resembles `test_soundfont.cpp`'s `integerTone`. The planning hub
      asked for small, separate edits to the web test files, and soundfont 04 is extending
      `test_soundfont.cpp` now; `tone`'s constants differ, so sharing it needs parameters.
      Switching `test_web_core.cpp` to `MidiTestFiles.h` is a small follow-up.
    - **possible Data Clumps:** a clip's MIDI bytes, voice limit and values travel together.
      With three clips a `Clip` type would add more than it saves.
  - Spec: no missing or wrong requirement. The fixture writing the preview's MIDI bytes is the
    ticket's change to the spec's S2 wording, noted above. The pool check shows notes are
    taken; the 6 ms fade itself is pinned by the golden values, as the ticket asks.
