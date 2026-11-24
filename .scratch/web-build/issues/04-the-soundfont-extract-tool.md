# 04: The soundfont extract tool, and the default piano's extract

**What to build:** A site maintainer runs one command on a full SoundFont 2 file and gets a
SoundFont 2 file holding a single preset. The extract renders through the soundfont node
exactly as that preset does in the full file. It carries the licence notice and the provenance
inside it, and the same inputs always give the same bytes. The tool then makes the default
piano's extract: FluidR3_GM's "Yamaha Grand Piano", preset 0:0, every zone, about 7.45 MiB. See
the [spec](../spec.md) (The extract tool) and map ticket 03,
[Where does the web build live, and how does a built module reach the site?](../../midishare-playback/issues/03-where-the-web-build-lives.md)
(decisions 6 and 7).

**Blocked by:** None (can start immediately). It needs nothing from this spec's other tickets.
Spec A 01, which renders presets through the node, is done.

**Status:** done

**The `COPYING` download is approved.** In the planning hub on 2026-11-24 the user approved
downloading the `COPYING` text of Debian's `fluid-soundfont` 3.1-6, in their words: "Yes,
approve it". The approval covers:

- that one text file, from Debian's sources site;
- keeping it outside the repository;
- passing it to the tool as its notice argument.

It covers nothing else. The FluidSynth download for spec A's ticket 07 is still unapproved.

## Checklist

- [x] A Python 3 script under `scripts/`, standard library only, outside the CMake build. It
  takes a source file, a preset as bank and program, a notice file, the source's URL and
  version, and an output path.
- [x] It copies:
  - the preset with its zones, global zone included;
  - the instruments those zones name, with their zones;
  - the samples those zones play, each followed by 46 zero points.

  Generators and modulators are copied verbatim, and every index is relinked: instrument,
  sample, bag, generator, modulator and sample offsets. Each hydra list ends with its terminal
  record. A sample link to a sample left out is cleared. Nothing else from the source is kept.
- [x] Licence and provenance:
  - The full notice goes into the INFO list's comment field (`ICMT`, at most 65,536 bytes),
    followed by the provenance:
    - the source URL;
    - the source's version;
    - the source's SHA-256, computed by the tool;
    - the preset's bank, program and name;
    - the Modular commit the tool ran from, marked dirty if that checkout had uncommitted
      changes.
  - A short copyright line goes into the copyright field (`ICOP`, at most 256 bytes).
  - The extract's own SHA-256 is not inside it; the tool prints it.
- [x] Deterministic: two runs with the same inputs give identical bytes. No date or other
  changing value is written.
- [x] It refuses each of these with a message and a non-zero exit, and writes nothing:
  - a SoundFont 3 file, or one with a compressed sample;
  - a file with 24-bit sample data (`sm24`);
  - a preset the file lacks;
  - a malformed file;
  - a notice too long for `ICMT`.
- [x] A CTest script test (seam S3), native only, with Python found by CMake:
  - A native helper, built from spec A's test-side soundfont builder, writes a soundfont with
    several presets. They share samples, and one sample is used only by another preset.
  - The tool extracts a preset that is not the file's lowest, so every index is relinked. That
    preset has global zones at both levels and modulators.
  - The helper renders a clip with that preset selected in the source and with the extract's
    only preset. The two renders are bit-identical at two rates.
  - The tool's own reader compares the preset's zones, generators, modulators and sample points
    in both files: equal.
  - The extract holds one preset and no unused sample. Its INFO fields hold the notice and
    provenance, and a second run gives the same bytes.
  - Each refusal above is checked.
- [x] `docs/windows.md`'s description of the suite counts the new CTest entry. A short section
  in the tool's docstring, or in `docs/node-io.md`'s soundfont section, says how to run it.
- [x] **The default piano's extract** (acceptance, outside `tests/`):
  1. Download the `COPYING` text of Debian's `fluid-soundfont` 3.1-6 from Debian's sources
     site, as approved above. Keep it outside the repository and record its URL, size and
     SHA-256 here.
  2. Run the tool on `FluidR3_GM.sf2`, which is already on the development machine outside both
     repositories, for preset 0:0, with that notice. The source is
     `https://sources.debian.org/data/main/f/fluid-soundfont/3.1-6/FluidR3_GM.sf2`, Debian 3.1-6,
     148,398,306 bytes, SHA-256
     `74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0`. If the file is
     missing, fetching it again needs the user's yes at that time.
  3. With the test's helper, check that the extract renders preset 0:0 identically to the full
     file natively, at 44.1 and 48 kHz.
  4. Run the tool twice and check the bytes are identical.
  5. Record under Comments the extract's size and SHA-256, the tool's commit, and every
     provenance field. This is the spec's provenance record.
  6. Neither file is committed. The planning hub hands the extract and notice to midishare, or
     a midishare session reruns the tool and compares the hash.
- [x] Nothing changes under `kernel/` or `workspace/`. The full suite passes (`docs/windows.md`),
  and `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/web-build-04-soundfont-extract-tool`, cut from a
freshly fetched `origin/main` at `4db75a1` and rebased before its first push onto `95a1a1a`
(web build 01, then soundfont node 02, which plays loops).

- *What was built.*
  - `scripts/soundfont_extract.py`, Python 3.8 or later, standard library only, outside the
    CMake build. `extract` writes the extract and prints its size, SHA-256 and provenance.
    `preset` prints one preset's structure with every index resolved, and `contents` prints a
    file's version, counts, INFO fields, presets and sample links; the test reads both. Its
    docstring says how to run it.
  - `tests/soundfont_extract_helper.cpp`, built from `tests/SoundFontTestFiles.h`. `write`
    writes the test's soundfonts; `compare` renders a clip through `SoundFontNode` from a
    source with a preset selected and from an extract, at 44.1 and 48 kHz. The clip plays
    every key 0–127 at velocities across 1–127, with a voice limit of 4096 so no note is
    dropped. It fails if the clip is silent.
  - `tests/test_soundfont_extract.cmake`, the `soundfont_extract` CTest entry. It is added
    only when not cross-compiling, with `find_package(Python3 3.8)`; with no Python found it
    fails and says so.
  - `docs/windows.md` counts ten CTest entries, eight headless.
  - Nothing under `kernel/` or `workspace/` changed.
- *Rules the tool settles* (within the ticket; no architecture decision):
  - INFO holds, in order: `ifil`, the source's version; `isng`, the source's sound engine,
    else `EMU8000`; `INAM`, the preset's name; `ICOP`, the source's own copyright line, else
    the notice's first non-blank line; `ICMT`, the notice byte for byte, a blank line and the
    provenance. A file needs a version and an engine, and the source's are the faithful
    ones. FluidR3's own `ICOP` names both authors, where `COPYING`'s first line names one.
  - The first preset of a bank and program is copied, the one the node plays.
  - Instruments and samples keep their source order. A sample link to a kept sample is
    relinked; any other is cleared to 0. Terminal records are written fresh: `EOP`, `EOI`
    and `EOS`, and zero terminal generator and modulator records.
  - Refusals beyond the ticket's list, each before anything is written:
    - an output that already exists;
    - a preset that plays a sample in ROM, whose points no file holds;
    - an instrument or sample generator out of range anywhere in its list, even after a
      zone's terminal generator, where the node ignores it. The specification makes that
      generator the zone's last, so the file is malformed;
    - a loop so far before its sample that its relinked offset would fall below 0, which the
      node would play unlooped;
    - a notice that is empty or not UTF-8, a URL or version that is not one line, an
      `isng`, `INAM` or `ICOP` past 256 bytes, and a run outside a git checkout.
- *Tests (seam S3).* The helper's source has four presets: "low" 0:0, the extracted "target"
  1:5, "high" 2:7 and a later duplicate of 1:5. Six samples: one shared by 0:0 and 1:5, one
  used only by 0:0, a left/right pair linked to each other, a left sample linked to the
  sample only 0:0 uses, and one no preset uses. Each sample has its own loop, set through
  the builder's loops from soundfont node 02, and most zones loop (`sampleModes` 1 or 3).
  Two samples carry a pitch correction, and the source has its own `ICOP` and `isng`. The
  test checks:
  - the extract renders bit-identically to the source at 44.1 and 48 kHz, so the node reads
    every relinked loop;
  - the `preset` printouts of both files are equal, and hold the expected instrument, a
    sample's loop offsets and the global zone's generators and modulators;
  - the extract's whole `contents` printout: one preset, two instruments, four samples and
    9,992 points; the INFO fields in order, the notice then the provenance with the
    source's SHA-256 and the checkout's `HEAD`; the pair relinked and the outside link
    cleared;
  - a source with no `ICOP` or `isng` (`plain.sf2`) takes the notice's first line and
    `EMU8000`;
  - a second run gives the same bytes, and an existing output is refused and unchanged;
  - each refusal: SF3, a compressed sample no preset plays, `sm24`, a missing preset, a
    truncated file, a sample index out of range and a 65,536-byte notice. Each exits
    non-zero with its message and writes nothing.
- *Red, then green.* The tool and its test were written together, and the test passed on its
  first run. To show it can fail, a throwaway script outside the repository ran it against
  14 mutants of the tool. Each made it fail: no index relink, loops not shifted, modulators
  zeroed, bag modulator indexes not shifted, the source's `ICOP` ignored, a commit other than
  `HEAD`, no zero padding, links kept raw, a sample dropped, no `ICMT` limit, `sm24`
  accepted, a wrong source hash, the last preset of a number taken, and pitch correction
  zeroed. The duplicate 1:5 joined the fixture after that last-preset mutant first
  survived. After the rebase onto soundfont node 02 all 14 still fail, and the unshifted
  loops now fail the render comparison itself.
- *Checks, on the final tree.*
  - Focused: `-Target soundfont_extract_helper -TestRegex '^soundfont_extract$'`.
  - Full suite through the script, after the rebase: Debug 10/10 and Release 10/10 CTest
    entries, both `desktop` tests and kernel purity included. Doctest cases: kernel 44,
    node 17, authoring 29, MIDI-file 16, soundfont 59.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
- *The `COPYING` download*, as approved. The sources.debian.org API
  (`/api/src/fluid-soundfont/3.1-6/COPYING/`) names
  `https://sources.debian.org/data/main/f/fluid-soundfont/3.1-6/COPYING`, 1,086 bytes,
  SHA-256 `8ef830b65c97a976b86e34bb5fde08d99dfb1db13c4149b5b20bc837ac6c4568`. The download
  matched both. It is US-ASCII with LF line endings, kept in a new folder outside both
  repositories, `%USERPROFILE%\soundfonts\fluid-soundfont-3.1-6-copying\COPYING`.
- *The default piano's extract* (acceptance, outside `tests/`).
  - Source: `FluidR3_GM.sf2` in `%USERPROFILE%\soundfonts\fluidr3-3.1-6\`, read only, with
    `python -I`. 148,398,306 bytes, SHA-256
    `74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0`, as recorded.
  - Command, from a clean checkout at the tool's commit
    `a8b81a6b67ec6999613effe558b0d6bd90875c28`:

    ```powershell
    python -I scripts/soundfont_extract.py extract FluidR3_GM.sf2 --preset 0:0 `
      --notice COPYING `
      --source-url https://sources.debian.org/data/main/f/fluid-soundfont/3.1-6/FluidR3_GM.sf2 `
      --source-version "Debian fluid-soundfont 3.1-6" `
      --output fluidr3-gm-0-0-yamaha-grand-piano.sf2
    ```

  - The extract: `%USERPROFILE%\soundfonts\extracts\fluidr3-gm-0-0-yamaha-grand-piano.sf2`,
    **7,812,018 bytes** (7.45 MiB), SHA-256
    **`527971ff49306ced69c0aa5ec82e4a932f27d12fe831fc24cd35699cd9b05d95`**. Its side file
    of provenance and SHA-256, `fluidr3-gm-0-0-yamaha-grand-piano.provenance.txt`, is beside
    it.
  - What it holds: version 2.01; 1 preset, 1 instrument, 40 samples and 3,903,310 points;
    `isng` "E-mu 10K1"; `INAM` "Yamaha Grand Piano"; `ICOP` "Frank Wen 2000-2002, 2008; Toby
    Smithe 2008"; `ICMT` 1,506 bytes, the whole MIT notice then the provenance.
  - Every provenance field, as `ICMT` holds it:
    - Source URL: `https://sources.debian.org/data/main/f/fluid-soundfont/3.1-6/FluidR3_GM.sf2`
    - Source version: `Debian fluid-soundfont 3.1-6`
    - Source SHA-256: `74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0`
    - Preset: `bank 0, program 0, "Yamaha Grand Piano"`
    - Modular commit: `a8b81a6b67ec6999613effe558b0d6bd90875c28`, not dirty
  - Render, natively, with the test's helper built Release, on the node as soundfont 02
    left it, loops included: preset 0:0 from the full file and the extract's only preset
    are bit-identical.
    - 44.1 kHz: 441,000 frames, 579,368 non-zero samples, FNV-1a `b8d8c8d538bdad31` for
      both.
    - 48 kHz: 480,000 frames, 630,606 non-zero samples, FNV-1a `89ee1204924aa20d` for both.
  - The tool's `preset` printouts of the two files are identical (93 lines).
  - A second run gave identical bytes. Neither file is committed.
  - An extract made before the second rebase, from a commit that was never pushed, was
    deleted and made again from `a8b81a6`.
  - *For later tickets:* the helper built Debug renders the same 44.1 kHz clip as
    `c303097e64329788`, still equal between the two files, against Release's
    `b8d8c8d538bdad31`. At 48 kHz both builds agree. So MSVC Debug and Release differ on
    this node's output at 44.1 kHz today, which matters to golden values checked in both
    configurations (spec A 08, this spec's ticket 07). The same split showed before
    soundfont node 02 landed.
  - The squash merge gives `main` a new commit. `a8b81a6` stays reachable as the PR's head.
    A rerun from `main` changes only the commit line, and so the SHA-256.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at `26b522d`,
  before the rebase, with a clean working tree. The fixes are re-checked on the final tree.
  - Standards: lines past 100 columns. Wrapped; none passes 95.
  - Standards: hydra records were tuples read by index. They are named tuples now, and the
    bag, generator and modulator lists of a level travel as one `Level`.
  - Standards: `run(bags, records, zones, field)` hid what `field` meant, and "run" had
    three meanings. Now `generators()` and `modulators()`, and `args.handler`.
  - Standards: `refuse()` only raised, hiding that control never returns. Every refusal
    raises `Refusal` directly.
  - Standards: a bare 256 beside named limits. Now `SHORT_TEXT_LIMIT`, for `isng`, `INAM`
    and `ICOP`.
  - Standards: the CMake comment said the tool runs the helper. It says the test's script
    does.
  - Standards: the `preset` and `contents` runs' exit codes were unchecked. They must
    succeed, and a failure shows the tool's message.
  - Standards: the helper's bank and program were not range-checked. They are, 0 to 65535.
  - Kept, Standards: `Timed` and `MidiAt` repeat `test_soundfont.cpp`'s. The foundations
    keep helper nodes in test files, and `SoundFontTestFiles.h` is the shared file builder,
    which holds no node. Soundfont node 02 was also changing that header while this ran.
  - Spec: the commit was checked only for its form. It must now equal the checkout's
    `HEAD`.
  - Spec: the provenance's order inside `ICMT` was unchecked. One anchored expectation now
    covers the whole `contents` printout.
  - Spec: the source's own `ICOP` was untested, since the builder writes none. `source.sf2`
    now carries an `ICOP` and `isng`, and `plain.sf2` neither; both paths are checked.
  - Kept, Spec: INFO takes the source's version, engine and copyright line, where the
    ticket says "Nothing else from the source is kept". Recorded under the rules above.
  - Kept, Spec: the refusals beyond the ticket's list, two of which refuse a file the node
    plays. Both break the specification, and the tool refuses rather than write a file it
    cannot copy faithfully. Recorded under the rules above.
  - Spec: the acceptance steps were pending at review. They are recorded above.
