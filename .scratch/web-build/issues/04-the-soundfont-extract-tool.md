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

**Status:** ready-for-agent

**The `COPYING` download is approved.** In the planning hub on 2026-11-24 the user approved
downloading the `COPYING` text of Debian's `fluid-soundfont` 3.1-6, in their words: "Yes,
approve it". The approval covers:

- that one text file, from Debian's sources site;
- keeping it outside the repository;
- passing it to the tool as its notice argument.

It covers nothing else. The FluidSynth download for spec A's ticket 07 is still unapproved.

## Checklist

- [ ] A Python 3 script under `scripts/`, standard library only, outside the CMake build. It
  takes a source file, a preset as bank and program, a notice file, the source's URL and
  version, and an output path.
- [ ] It copies:
  - the preset with its zones, global zone included;
  - the instruments those zones name, with their zones;
  - the samples those zones play, each followed by 46 zero points.

  Generators and modulators are copied verbatim, and every index is relinked: instrument,
  sample, bag, generator, modulator and sample offsets. Each hydra list ends with its terminal
  record. A sample link to a sample left out is cleared. Nothing else from the source is kept.
- [ ] Licence and provenance:
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
- [ ] Deterministic: two runs with the same inputs give identical bytes. No date or other
  changing value is written.
- [ ] It refuses each of these with a message and a non-zero exit, and writes nothing:
  - a SoundFont 3 file, or one with a compressed sample;
  - a file with 24-bit sample data (`sm24`);
  - a preset the file lacks;
  - a malformed file;
  - a notice too long for `ICMT`.
- [ ] A CTest script test (seam S3), native only, with Python found by CMake:
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
- [ ] `docs/windows.md`'s description of the suite counts the new CTest entry. A short section
  in the tool's docstring, or in `docs/node-io.md`'s soundfont section, says how to run it.
- [ ] **The default piano's extract** (acceptance, outside `tests/`):
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
- [ ] Nothing changes under `kernel/` or `workspace/`. The full suite passes (`docs/windows.md`),
  and `/code-review` runs on the branch's diff.
