# 08: Acceptance: the default piano in the module, the preview's browser timing, Firefox and Safari

**What to build:** Evidence that the shipped module plays the default piano bit for bit as the
native build does, and that a note preview is fast enough to judge where midishare renders. It
also brings the two browsers the development machine lacks into the check once, by hand. This
ticket changes no product code. A failure is a finding to fix in its own ticket, not here. See
the [spec](../spec.md) (Further Notes, Acceptance), map ticket 04's decision 8 and map ticket
08's decisions 6 and 8.

**Blocked by:**

- 04: The soundfont extract tool, and the default piano's extract;
- 07: Golden clips: the glue renders bit for bit natively and under Node;
- spec A's [02: The piano's tone](../../soundfont-node/issues/02-the-pianos-tone.md) and
  [03: When every voice is busy](../../soundfont-node/issues/03-when-every-voice-is-busy.md),
  so the piano sounds right: loops, filter, velocity and the full pool;
- the user's steps below.

**Status:** ready-for-human

The agent's part is done. One item waits for the user: Firefox and Safari, checked by hand on
the check page (below, and [web.md](../../../docs/web.md#checking-a-browser-by-hand)).

## Checklist

- [x] **The default piano, module against native.** Use the extract from ticket 04, kept
  outside the repository. A throwaway harness under `.scratch/web-build/prototypes/`, outside
  the build, renders a clip covering the piano's key ranges and velocities at 44.1 and 48 kHz,
  twice:
  - natively, through the render core;
  - under Node, through the loader.

  Frame counts and per-block hashes are equal. Rendered audio and licensed files are never
  committed.
- [x] **The preview's timing in a browser** (map ticket 04, decision 8). With the extract
  loaded, a local page times one-note renders in Chrome or Edge, headless, served by a local
  static server:
  - the first render after `createInstrument`;
  - the median of later renders.

  Also time `loadModular` and `createInstrument`, and record everything under Comments for
  midishare's choice between the main thread and a Web Worker.
- [x] **The check page** (ruling G10):
  - It is committed with the tests and built into the build tree with its fixtures: the
    preview clip's soundfont and MIDI bytes.
  - It renders the clip through the loader and shows the frame count, each block's hash and
    whether they equal the golden values.
  - The `web` job uploads it, with its fixtures and the module, as a separate check artifact,
    not inside the module artifact midishare commits.
  - It needs any static server, and nothing else installed.
- [ ] **Firefox and Safari, by hand (the user).** The user opens the check page in current
  desktop Firefox and in Safari on an Apple device, and records the result here: browser,
  version, operating system, and whether every hash matched.
  - Installing Firefox on the development machine, if the user checks it there, is the user's
    own step.
  - How the user reaches the files (the check artifact or a local build) and serves them is
    agreed with the user through the planning hub before this item starts.
  - The agent checks the same page headless in Chrome or Edge first.

  **Waiting for the user.** The agent checked the page headless in Chrome and Edge: every hash
  matched (Comments). The proposed steps are under Comments, **For the user**; they await the
  planning hub's agreement with the user, then the user's results.
- [x] Any mismatch or failure is recorded as a finding with its own follow-up ticket proposed to
  the planning hub, not fixed here.
- [x] Nothing changes under `kernel/` or `workspace/`, and no product code changes. The full
  native suite and the `web` job pass, and `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — the agent's part.** Branch `claude/web-build-08-acceptance`, cut from a freshly
fetched `origin/main` at `4241375` (web build 07) and rebased, before its first push, onto
`bc33cf8` (soundfont node 04, merged meanwhile). The rebase met no conflict. Every
measurement below was made again on the rebased tree, and those are the numbers given,
except where one says otherwise.
Nothing under `kernel/`, `workspace/`, `nodes/` or `apps/` changes.

- *What was built.*
  - **The check page**, under `tests/web/`: `index.html` and `check.mjs`, and
    `golden_hash.mjs`, the golden hash of `tests/GoldenRender.h` in JavaScript, moved out of
    the smoke test so the page and the test share one port. The page loads the module from
    `modular/`, renders the golden preview of `tests/WebGoldenClips.h` at 44.1, 48 and
    22.05 kHz, and shows each rate's frame count and block hashes beside the golden ones, with
    a verdict, the browser's user agent and the module's `version`. Opened from the file
    system, where it cannot load its modules, it says so.
  - **Its fixtures, built.** `web_smoke_fixture` now runs in the build, not as a CTest
    fixture: it writes the smoke test's soundfont as before, and the golden preview's
    `preview.sf2`, `preview.mid` and `preview.json`, now with every rate's values, into
    `build/web/web-check/`. The build also copies the page and the module's five files from
    `dist/` into that folder (`modular/`), so it can be served alone. The smoke test reads its
    golden preview from there and checks 48 kHz, as before. The web build registers one
    CTest entry fewer, since the writer is no longer one.
  - **CI.** The `web` job uploads `build/web/web-check/` as a second artifact,
    `modular-web-check-<commit>`. The module's artifact, `build/web/dist/`, is unchanged.
  - **Docs.** `docs/web.md` gains **Checking a browser by hand**, the user's steps, and the
    second artifact; `docs/windows.md` says CI checks no browser and uploads the page.
  - **The throwaway harness**, `.scratch/web-build/prototypes/08-default-piano/`:
    `PROTOTYPE_piano_native.cpp`, built by its own `CMakeLists.txt` against the render core;
    `PROTOTYPE_piano_node.mjs`; and `PROTOTYPE_browsers.py` with `preview_timing.*` and
    `check_driver.html`. Its build, the clip and every rendering stay outside the repository.
- *The default piano, module against native.* The extract read in place, never copied:
  `%USERPROFILE%\soundfonts\extracts\fluidr3-gm-0-0-yamaha-grand-piano.sf2`, 7,812,018 bytes,
  SHA-256 `9609e512c25098284c6c0d402da0a5c85be77656e148262a2b93b834a5be0d08`, the extract
  regenerated from `main` after ticket 04 merged (its side file says so).
  - **The clip** (the harness's `clip`), 8,117 bytes, SHA-256
    `131a0d16026ab4aedf4934d4e5fd961dc1bc6c4b3d93d6773694d8330e87fe92`, 33.52 s:
    - nine sweeps of every key the piano plays, 0 to 108, one per velocity layer of its preset
      (1–64, then eight of eight, to 121–127), even keys at the layer's lowest velocity and
      odd ones at its highest, so all twenty key ranges at both edges of all nine layers;
    - then the root key of each key range held 8 s, so every sample plays into its loop;
    - last, a ten-note chord at velocity 127, released as the file ends, so the trim cuts its
      release inside the two-second tail (0.71 s of it is kept).
  - **Natively**, through `modular::web::Instrument`, MSVC Release and Debug, and **under
    Node**, through the shipped loader: `dist/` from a clean build of the rebased branch
    (`c0fb317`), Emscripten 6.0.13, Node 24.19.0. Each rate renders on a fresh instrument.

    | Rate | Frames | Blocks | Block 0 | Last block | Peak | Release, Debug, Node |
    | --- | --- | --- | --- | --- | --- | --- |
    | 44.1 kHz | 1,509,493 | 369 | `0xd1e321ea91487253` | `0xbf4f85810eefe6b7` | 0.418573 | 1,085, 4,882, 1,193 ms |
    | 48 kHz | 1,642,986 | 402 | `0x4865e999688f5b75` | `0x267a6b82316969a8` | 0.425973 | 1,174, 5,385, 1,317 ms |

    - **Every frame count and block hash is equal** between native Release, native Debug and
      Node, at both rates. 3,018,256 and 3,285,178 samples are non-zero.
    - MSVC Debug and Release now agree on the piano, where ticket 04 saw them differ at
      44.1 kHz before spec A 08's portable maths.
    - Node took 1.10 to 1.12 times native Release time, inside the 1.1 to 1.25 the probe
      measured.
    - Before the rebase, on `4241375`, every value was the same, so soundfont node 04 leaves
      this preset's render unchanged. Each render took about 15 % less time there.
  - **Red**, before the rebase: with one 48 kHz block hash of the native values corrupted,
    the Node half reported
    `48000 Hz: 1642986 frames (golden 1642986), first differing block 200 (frames 819200 on)`
    and exited 1.
- *The preview's timing in a browser* (map ticket 04, decision 8).
  - **Setup.**
    - The page: `preview_timing.html`, served by `PROTOTYPE_browsers.py`, Python's
      `ThreadingHTTPServer` on 127.0.0.1, with `/dist/` the module from a clean build of
      `c0fb317`, and `/piano.sf2` the extract, read in place.
    - The browsers: Chrome 157.0.8103.79 and Edge 158.0.4370.33, each `--headless` with a
      fresh profile folder per page load, five loads each.
    - The machine: Windows 11 Pro, AMD Ryzen 7 9800X3D, 16 logical cores.
    - The renders, on the page's main thread, as the loader runs them today. The preview
      has midishare's shape: one key held 0.25 s at velocity 105, then its release; the file
      ends as the key is let go. Its 35 bytes are written by the page, as the golden
      preview's are. They lack the controller and tempo events that abcjs writes into
      midishare's own preview (`getMidiFile(previewAbc(pitch))`, map ticket 04,
      decision 8).
  - **Medians over the five loads**, in ms:

    | Browser | Import loader and glue | `loadModular` | `createInstrument` | First render | Later renders |
    | --- | --- | --- | --- | --- | --- |
    | Chrome | 4.3 | 3.3 | 13.7 | 4.7 | 1.9 |
    | Edge | 8.9 | 4.4 | 13.4 | 5.2 | 1.9 |

    - The first render is middle C at 48 kHz, 35,435 frames (0.74 s), right after
      `createInstrument`.
    - Later renders are the median of 25 more of the same.
    - The ranges over the loads:
      - Chrome: `loadModular` 3.0–4.7, `createInstrument` 12.6–16.1, first render 4.0–5.1,
        later medians 1.6–1.9;
      - Edge: `loadModular` 3.9–5.7, `createInstrument` 12.4–16.6, first render 4.9–5.6,
        later medians 1.8–1.9.
    - Other previews, medians of 11 per load:
      - A0 (key 21), 37,956 frames: Chrome 1.7–1.8, Edge 1.6–1.8;
      - C8 (key 108), 16,334 frames: Chrome 1.5–1.6, Edge 1.5–1.6;
      - middle C at 44.1 kHz, 32,556 frames: Chrome 1.5–1.6, Edge 1.5–1.6.
    - The slowest single later render in any load: Chrome 3.4, Edge 3.1.
    - Fetching the 7.8 MB extract from 127.0.0.1 took 7.7–23.7. That is a local disk's time,
      not a network's.
    - Before the rebase, on `e7ba836`, the same run's medians, in the table's order, were
      Chrome 4.5, 3.3, 12.8, 4.3 and 1.6, and Edge 7.5, 4.8, 13.7, 4.9 and 1.7.
  - **For midishare's choice of thread.**
    - A render runs on the calling thread, so its time is how long it blocks the page.
    - On this machine a preview blocks for about 1.5–2 ms, the first after
      `createInstrument` about 4–5 ms. Making the instrument, a one-off, blocks for about
      13 ms, and `loadModular` for 3–5 ms once its files are fetched.
    - Each is under one 60 Hz frame (16.7 ms), even on a device several times slower.
    - A whole piece renders in proportion to its length and voices: the 33.5 s acceptance
      clip, with up to about a hundred voices, took about 1.2–1.3 s under Node, which ran
      within 1.12 times native here.
    - The numbers are from one fast desktop, headless, and a phone may be several times
      slower.
- *The check page, headless first.* Through `check_driver.html`, which opens the page in a
  same-origin frame and posts its outcome, so the page itself posts nothing. Served from
  `build/web/web-check/` as built at `c0fb317`, and again after the review's fixes at
  `c3df9b5`, not dirty either time:
  - Chrome 157 and Edge 158, headless: **every hash matched**, at 44.1 kHz (15,058 frames,
    4 blocks), 48 kHz (16,390 frames, 5 blocks) and 22.05 kHz (7,530 frames, 2 blocks).
  - The user's own command, `python -m http.server 8765 --bind 127.0.0.1 --directory
    build/web/web-check` (Python 3.13.1), opened in the desktop app's built-in Chromium 156
    browser, before the rebase (`e7ba836`): every hash matched, with the page as the user
    will see it.
  - Opened from the file system, in headless Chrome before the rebase, the page said "The
    check could not load. Serve this folder over HTTP, with .mjs files sent as JavaScript
    (text/javascript)."
  - **Red**, before the rebase: with block 2 of the 48 kHz values corrupted in the folder's
    `preview.json`, the smoke test failed with `48000 Hz: 16390 frames (golden 16390), first
    differing block 2 (frames 8192 on)` and printed the render's values. The build then
    wrote the file again.
- *Processes and servers.*
  - Every browser started under a profile folder named for the run (`wb08-<random>-…`), and
    was stopped by its process tree and then by every process whose command line named its
    folder.
  - At the end, no process named any of them: 0 left, checked by the script and again by
    hand.
  - The servers, the script's and the static one, were stopped, and nothing listens on
    their ports.
  - The browsers' other processes were not touched.
- *Findings.* No mismatch and no failure, so no follow-up ticket is proposed.
- *Checks, on the rebased tree, and again after the review's fixes (`c3df9b5`).*
  - Native, through the script: Debug 12/12 and Release 12/12 CTest entries, both `desktop`
    tests and soundfont node 04's `soundfont_portable_maths` included.
  - `ctest --preset web`, Emscripten 6.0.13 and its Node on Windows: 9/9 entries, the smoke
    test reading its golden preview from `web-check/`.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at `82cff4d`,
  with a clean working tree.
  - Standards, fixed:
    - the page rendered every rate on one instrument, against the spec's "a fresh graph or
      instrument for each independent render". It now makes one per rate;
    - **possible Speculative Generality:** `golden_hash.mjs` no longer exports
      `kBlockFrames` or `firstDifferingBlock`, which nothing imports, and the page's outcome
      drops the fields only the throwaway driver read;
    - **possible Mysterious Name:** the page's `describe()` is `aboutLine()`. In
      `CMakeLists.txt`, one list held the module's file names and then their copies' paths;
      now there are three lists, with a comment on why the copy depends on the manifest
      alone;
    - the smoke test now fails with "no golden preview at 48 kHz" when the fixture lacks
      that rate, as the fixture writer did before;
    - in `docs/web.md`, "the same bits in every engine" now reads "in every browser and in
      Node", so "engine" keeps the meaning `CONTEXT.md` gives it. The page is copied when
      the preset is configured and the fixtures are built, as the docs now say. They no
      longer claim more than was tried on the file system: Chrome was tried. On Python,
      `mimetypes` was checked in 3.10, 3.11 and 3.13, and 3.13 served the page.
  - Standards, kept:
    - **possible Speculative Generality:** `globalThis.modularCheck`. It is how the agent
      checks the same page headless, which this ticket asks, without the page posting
      anything to a server;
    - **possible Mysterious Name:** `web_smoke_fixture` now writes the check page's fixtures
      too. Its header says so, and a rename would touch the docs and the earlier tickets'
      evidence for no change in behaviour;
    - **possible Duplicated Code:** the throwaway native harness repeats the fixture's JSON
      loop. It is a prototype outside the build.
  - Spec, fixed:
    - the timed preview's bytes are written by the page, not by abcjs. Setup now says so;
    - the last checklist item's CI and review evidence is recorded here before the pull
      request merges.
  - Spec, kept:
    - The fixture writer moved from CTest into the build. The ticket's "built into the build
      tree with its fixtures" needs the folder complete after a build alone, which is what
      the CI artifact uploads and what a local build serves.
    - `docs/web.md` describes the hand check as the general way to check a browser.
      The logistics for this ticket's Firefox and Safari check are still a proposal: below,
      under **For the user**, which awaits the planning hub's agreement with the user.
  - Public content: every checkpoint was read for paths, account or machine names, emails
    and secrets. It holds none. The extract's path is given as `%USERPROFILE%\…`, as ticket
    04's evidence on `main` gives it. Every commit carries the noreply identity.
- *Kept from the spec, on purpose.*
  - The page renders the golden preview of `tests/WebGoldenClips.h` built by the test-side
    builder, never the extract. No soundfont file enters `tests/` (map ticket 08,
    decision 6).
  - The golden values in the page's column come from the commit it was built with, so a
    later change to the node's output that updates them needs no change to the page.
    Soundfont node 04, merged before this branch's first push, changed none of them.

**For the user: Firefox and Safari** (the proposal for the planning hub to agree with the
user; `docs/web.md`, *Checking a browser by hand*, has the same steps).

1. **Take the folder.** Once this pull request merges, open the `CI` run of the push to `main`
   on GitHub and download the artifact `modular-web-check-<that commit>` under **Artifacts**.
   Or use GitHub's command line,
   `gh run download <run id> --name modular-web-check-<commit> --dir modular-web-check`. Unzip
   it into an empty folder. It holds `index.html`, `check.mjs`, `golden_hash.mjs`,
   `preview.sf2`, `preview.mid`, `preview.json` and `modular/`.
2. **Serve it.** In that folder, `python3 -m http.server 8000 --bind 127.0.0.1` (Python 3.10
   or later; on Windows, `python`), then open `http://127.0.0.1:8000/`.
   - **Firefox:** current desktop Firefox, on any computer. Installing it on the development
     machine is the user's own step.
   - **Safari on a Mac:** the same command in Terminal; macOS's own `python3` may be older
     than 3.10.
   - **Safari on an iPhone or iPad**, or a Mac without Python 3.10: serve from another computer
     on the same network with `--bind 0.0.0.0`, and open
     `http://<that computer's local network address>:8000/`. Windows may ask to let Python
     through its firewall. Stop the server with Ctrl+C afterwards.
3. **Read the page.** It should say, in green, "Every hash matched, at 44100, 48000, 22050 Hz",
   with a ✓ on every row. At this ticket's commit the 48 kHz preview's golden block 0 is
   `0x7597ec3240543af8`, and the five 48 kHz blocks are `0x7597ec3240543af8`,
   `0x550bb0ded79d1774`, `0x187ad7db25192b84`, `0xf73b149c78a2ad3f` and
   `0x907119e1db7fd826`. If a later change updates the golden values before the check, the
   golden column shows the new ones; the verdict is what counts.
4. **Record here** for each browser: its name and version, the operating system, and whether
   every hash matched. A mismatch or failure is a finding for its own ticket.
