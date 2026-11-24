# Spec: The web build and its render surface

Status: ready-for-agent
Date: 2026-11-24

This spec turns the decided parts of the wayfinder map
[midishare makes its sound with Modular](../midishare-playback/map.md) into buildable work for
the second of its two Modular-side specs, spec B. The first, spec A, is
[the soundfont node](../soundfont-node/spec.md). This spec synthesizes these resolved map
tickets, which hold the evidence and the reasons:

- [Probe: does the offline MIDI render run in a page as WebAssembly?](../midishare-playback/issues/02-probe-offline-render-in-a-page.md)
  (the exception mode, sizes, timings, the first bit-for-bit result);
- [Where does the web build live, and how does a built module reach the site?](../midishare-playback/issues/03-where-the-web-build-lives.md)
  (the supported platform, the `web` preset and CI job, `apps/web/`, the artifact and manifest,
  the version, the extract tool, licence, provenance, CSP and caching);
- [What does the page hand Modular, and what does it get back?](../midishare-playback/issues/04-page-to-modular-contract.md)
  (the surface, the instrument handle, the tail, the length limit, the sink, errors, call
  order, and the midishare session's answers);
- [How is a browser render checked against a native one?](../midishare-playback/issues/08-browser-render-checked-against-native.md)
  (bit for bit by construction, the `double` prefactor, the flags, the golden check, the
  clips, `CI gate`, the browsers);
- for the soundfont facts:
  [Which soundfonts ship?](../midishare-playback/issues/07-which-samples-ship.md),
  [Which part of the SoundFont format does the node read?](../midishare-playback/issues/09-soundfont-format-subset.md)
  and [Measure the FluidR3_GM piano](../midishare-playback/issues/10-measure-the-fluidr3-piano.md),
  with the planning hub's default-soundfont decision.

References such as "map 04, D6" are decision 6 of map ticket 04. "Spec A 03" is ticket 03 of
the soundfont node spec. "Ticket 03" alone is a ticket of this spec. This spec decides nothing
those tickets left open, except the rulings recorded under
[Decided while writing this spec](#decided-while-writing-this-spec).

## Problem Statement

midishare's Play and note preview should sound through Modular: a sampled piano computed in
the browser, with no sample download from a third-party host. Spec A builds the instrument, a
soundfont node. But no Modular code runs in a browser today.

- Every Modular build is native, and CI builds only with MSVC. A second compiler already found
  a defect that MSVC hides: persistence reads a `long double` with `std::from_chars`, which any
  libc++ build refuses (map 03, D2).
- The same `long double` splits MIDI event frames between builds. MSVC's is a 64-bit double and
  wasm32's is a 128-bit software float, so an event on an exact half-frame tie lands one frame
  apart (map 08, D3). A browser render cannot then equal the native render that tests check.
- There is no surface a page can call. `MidiFileNode` imports only from a filesystem path, and a
  page has bytes, not files. `CaptureSinkNode`, the only sink, grows a vector inside
  `process()` and never clears it between engines, so it suits neither a ten-minute render nor
  an instrument that renders clip after clip.
- A page needs a module it can load and type-check, the commit it was built from, and a soundfont
  small enough for its host. The default piano lives in a 141.5 MiB General MIDI bank; the host
  serves at most 25 MiB per file. No tool cuts one preset out of a soundfont.

## Solution

**The web is a supported Modular platform** (map 03, D1). A `web` configure preset builds the
web surface and every test that passes under Node with Emscripten 6.0.13. A `web` CI job under
`CI gate` builds and tests it on every pull request that takes the build path, and
`docs/web.md` states what it covers and what it leaves out.

**MIDI times move to `double`** first, as a prefactor (map 08, D3). Events land on the same frame
in every build, and persistence compiles on libc++. Under MSVC nothing changes, because its
`long double` already is a double. GCC and Clang build with `-ffp-contract=off` (map 08, D4).

**A render surface, in `apps/web/`** (map 03, D3; map 04). A client loads Modular once, hands it a
soundfont's bytes once to get an *instrument*, and renders Standard MIDI File bytes at a sample
rate into two channels it owns:

- The instrument keeps one graph: a trigger, a `MidiFileNode`, a soundfont node and the glue's
  own preallocated stereo sink. Each render imports the MIDI bytes into the kept `MidiFileNode`
  and builds a fresh `Engine`, so every node starts from scratch (map 04, D4).
- The render runs two seconds past the file's end, then trims the silent tail, so the buffer
  holds the whole release (map 04, D6).
- A file longer than ten minutes is refused with an explicit error (map 04, D7).
- No C++ exception crosses into JavaScript. Every failure rejects with an `Error` carrying
  Modular's message, and a WebAssembly trap discards the instance, which the next call reloads
  (map 04, D10).
- Calls return promises and run one at a time (map 04, D12).

The render's C++ core builds on every platform and is tested natively in the Windows jobs and
under Node in the web job. The Emscripten module wraps it in exported functions, and a
hand-written ES module loader and a TypeScript declaration file wrap the module.

**The module reaches the site as a CI artifact** (map 03, D4, D5). The `web` job uploads the
module, its loader, its declarations and a manifest on every run. The manifest names the Modular
commit, the toolchain, the flags and each file's SHA-256, and `loadModular()` reports the commit
and toolchain it was built from. A midishare session commits the artifact of a merged `main`
commit. Until the job exists, a documented local build from a clean `main` checkout is the
stopgap.

**A browser render equals the native one bit for bit** (map 08, D1). Golden per-block hashes,
committed in a test, pin the glue's clips at three rates. The same test passes natively and
under Node, and a Node smoke test checks the same hash through the JavaScript surface.

**An extract tool** (map 03, D6, D7), a standard-library Python script under `scripts/`, copies one
preset of a soundfont into its own SoundFont 2 file. The extract renders exactly as the preset
does in the full file, carries its licence notice and provenance, and comes out byte for byte
the same from the same inputs. The default piano's extract, about 7.45 MiB, is made with it and
kept only in midishare.

## User Stories

1. As a web page author, I want to load Modular once with one call, so that every later render
   reuses one instantiated module.
2. As a web page author, I want to hand Modular a soundfont's bytes once and get an instrument
   back, so that the soundfont is parsed once for every clip and preview.
3. As a web page author, I want to render Standard MIDI File bytes at my `AudioContext`'s sample
   rate, so that I can build an `AudioBuffer` with no resampling.
4. As a web page author, I want exactly two channels back, left and right, of equal length, so
   that the buffer's shape never depends on the clip.
5. As a web page author, I want the channels as ordinary `Float32Array`s that JavaScript owns,
   so that a later render or memory growth cannot change or invalidate them.
6. As a web page author, I want the sample rate I asked for echoed back, so that I can build the
   buffer from the result alone.
7. As a web page author, I want the whole release of the last notes inside the buffer, so that
   a transport that ends a clip at its buffer's end never cuts a ringing note.
8. As a web page author, I want the buffer to end at most two seconds after the file's end, so
   that a clip's length stays predictable.
9. As a web page author, I want a silent tail trimmed, so that a clip whose notes have died away
   ends promptly.
10. As a web page author, I want the buffer never shorter than the file itself, so that the
    file's own end-of-track time is kept.
11. As a web page author, I want to know that a file of zero duration gives zero frames, so that
    I handle it before building an `AudioBuffer`, which cannot be empty.
12. As a web page author, I want a file longer than the limit refused with an explicit error, so
    that I fall back instead of exhausting memory.
13. As a web page author, I want every failure to reject with an `Error` whose message is
    Modular's, so that I can log it and fall back.
14. As a web page author, I want a failed render to leave the instrument usable, so that one bad
    clip does not end Modular for the page.
15. As a web page author, I want the module to recover on my next call after a WebAssembly trap,
    so that a crash does not end Modular for the page's life.
16. As a web page author, I want my instruments to keep working after that recovery, so that I
    need not build them again.
17. As a web page author, I want the instrument to keep its own copy of the soundfont's bytes,
    so that transferring or reusing my array cannot break it.
18. As a web page author, I want calls to run one at a time, in the order I made them, so that a
    preview and a Play started together cannot interleave.
19. As a web page author, I want every call to return a promise, so that moving the render into
    a Web Worker later changes no page code.
20. As a web page author, I want a note preview to go through the same render as a clip, so that
    there is one entry point and one sound.
21. As a web page author, I want TypeScript declarations for the surface, so that my strict type
    check passes.
22. As a web page author, I want to tell the loader where its `.wasm` lives, so that my bundler
    can serve it under a hashed name.
23. As a web page author, I want files I can cache forever under a hashed name, so that a
    returning visitor downloads nothing.
24. As a web page author, I want the module to need no cross-origin isolation, so that my site's
    headers stay as they are.
25. As a web page author, I want to know the oldest browsers the module loads in, so that I can
    say what is supported.
26. As a web page author, I want to read which Modular commit and Emscripten version built the
    module, so that my site can state what it plays.
27. As a web page author, I want the surface to know nothing of ABC, abcjs or Web Audio, so that
    any page that produces MIDI bytes can use it.
28. As the midishare session, I want a module built by Modular's CI from a merged `main` commit,
    with a manifest of its commit, toolchain, flags and file hashes, so that I commit reviewed,
    checkable bytes.
29. As the midishare session, until that job exists, I want a documented local build from a
    clean `main` checkout, so that I can integrate the surface early.
30. As a listener, I want what I hear to be what Download .mid contains, so that the file and
    the sound never disagree.
31. As a listener, I want the browser's render to equal the native render bit for bit, so that
    what was checked natively is what plays.
32. As a listener, I want an event on a frame tie to land on the same frame in every build, so
    that timing never differs between builds.
33. As a listener, I want the samples to be the soundfont node's own, with no gain added, so that
    the level is set in one place, the page.
34. As a maintainer, I want MIDI times in `double` everywhere they are stored or converted, so
    that MSVC and WebAssembly place every event on the same frame.
35. As a maintainer, I want persistence to compile with libc++, so that a web or macOS build no
    longer breaks on it.
36. As a maintainer, I want every saved graph to stay byte-identical under MSVC after the type
    change, so that no existing file changes.
37. As a maintainer, I want GCC and Clang never to contract floating-point operations, so that a
    native ARM64 build computes the same bits.
38. As a maintainer, I want a `web` CI job under `CI gate` on every pull request that takes the
    build path, so that a change that breaks the web build fails in the pull request that makes
    it.
39. As a maintainer, I want every target and test the web build leaves out listed with its
    reason, so that the web build's coverage is stated, not assumed.
40. As a maintainer, I want the render core built and tested natively too, so that the same code
    is checked in the Windows jobs.
41. As a maintainer, I want golden per-block hashes for the glue's clips, so that every change to
    what Modular renders is visible in review.
42. As a maintainer, I want a golden mismatch to print the new values and the first differing
    block, so that an intended change is easy to record and an unintended one easy to find.
43. As a maintainer, I want the web glue in `apps/`, with no change under `kernel/` or
    `workspace/`, so that the layering Modular exists to keep holds.
44. As a maintainer, I want no midishare name, path or limit in Modular's code, so that the
    surface serves any client.
45. As a maintainer, I want the Emscripten version pinned and verified in one place, so that
    moving it is one reviewed change.
46. As a site maintainer, I want a tool that copies one preset of a soundfont into its own SF2
    file, so that an instrument fits under my host's file-size limit.
47. As a site maintainer, I want the extract to render exactly as the preset does in the full
    file, so that cutting the file changes no sound.
48. As a site maintainer, I want the same inputs to give the same bytes, so that anyone can rerun
    the tool and check the extract's hash.
49. As a site maintainer, I want the licence notice and the provenance inside the extract, so
    that they travel with every copy.
50. As a site maintainer, I want the tool to refuse a file it cannot copy faithfully, so that it
    never writes a damaged extract.
51. As the user, I want Firefox and Safari checked by hand once, so that the browsers the
    development machine lacks are covered.
52. As the midishare session, I want a one-note preview timed in a browser with the piano
    loaded, so that I can decide where to render.

## Implementation Decisions

### Modules

- **The node library** (`nodes/`):
  - `MidiFile`, `MidiFileNode` and `PrimitivePersistence` keep MIDI times in `double` instead of
    `long double`: parsing sums seconds in `double`, `prepare()` rounds `seconds × rate` in
    `double`, and persistence writes and reads `double` with `max_digits10` digits (map 08, D3).
  - `MidiFileNode` gains an import from bytes beside its import from a path. It is edit-only;
    a failed import throws, keeps the previous content and records its message, as the path
    import does (map 04, D4).
- **The root build file:** `-ffp-contract=off` for GCC and Clang, Emscripten's Clang included.
  MSVC keeps its default `/fp:precise`. Never `-ffast-math`, relaxed SIMD or flush-to-zero
  (map 08, D4).
- **The render core** (`apps/web/`, working name `modular_web_core`): a C++ library that builds
  on every platform, MSVC included. It links the kernel, the MIDI library and the soundfont
  library, and nothing Emscripten. It holds the instrument and the glue's sink.
- **The module** (`apps/web/`, built only under Emscripten): exported C functions over the core,
  linked into a modularized ES6 module and its `.wasm`.
- **The loader and declarations** (`apps/web/`): a hand-written ES module that wraps the
  Emscripten module in the surface below, and a TypeScript declaration file for it.
- **The extract tool** (`scripts/`): a Python 3 script using only the standard library, outside
  the CMake build (map 03, D6).
- **CI:** a `web` job and its artifact in the existing workflow, and `CI gate` waiting for it.
- **Nothing changes under `kernel/` or `workspace/`.** There is no ADR and no `CONTEXT.md` term
  (map 03, map 04, map 08). The surface's words (instrument, render, rendering) are client
  words, documented in `docs/web.md`.

### The surface

These are the names the declaration file states. Map 04's sketch left them to this spec.

```ts
export function loadModular(options?: {
  locateFile?: (path: string, prefix: string) => string; // passed to Emscripten
}): Promise<Modular>;

export interface Modular {
  readonly version: { commit: string; dirty: boolean; emscripten: string };
  createInstrument(soundfont: Uint8Array): Promise<Instrument>;
}

export interface Instrument {
  render(midi: Uint8Array, sampleRate: number): Promise<Rendering>;
}

export interface Rendering {
  sampleRate: number;                        // the rate requested, unchanged
  channels: [Float32Array, Float32Array];    // left, right; equal length, may be 0
}
```

- **`loadModular`** instantiates one module. Each loaded module runs its calls one at a time, in
  call order, whichever instrument they name, and every call returns a promise (map 04, D12).
  `version` arrives with ticket 05.
- **`createInstrument`** copies the bytes it is given into JavaScript once and hands them to the
  module, which parses the soundfont once. The instrument plays the file's lowest preset with
  the node's default voice limit of 256. No preset or voice-limit option is offered until a
  client needs one (map 03, D3).
- **`render`** returns `{ sampleRate, channels }`: always two channels, copied out of
  WebAssembly memory into arrays JavaScript owns, at the requested rate, with no resampling
  (map 04, D5). The page builds the `AudioBuffer`, and a zero-length result is the page's to
  handle, since an `AudioBuffer` cannot be empty.
- **Errors:** every failure rejects with an `Error` whose message is Modular's, for example
  `midi file: missing MThd header` or `soundfont: compressed samples (sf3) are not supported`.
  A failed call leaves the module and its instruments usable.
- **Traps:** an exception that escapes an exported call can only be a WebAssembly trap or an
  abort, since the exports catch everything else. The loader rejects that call with an `Error`,
  discards the instance, and instantiates a fresh one on the next call. Each instrument then
  re-creates itself on the fresh instance from its own copy of the soundfont's bytes, never
  from the page's array, which the page may have transferred or changed (map 04, D10).
- **Files:** the loader, the Emscripten JavaScript glue, the `.wasm`, the declaration file, and
  from ticket 05 the manifest. They never assume a fixed URL: the glue finds its `.wasm` beside
  itself (`new URL(…, import.meta.url)`) unless `locateFile` says otherwise, so a bundler can
  serve every file under a content-hashed name and cache it forever (map 03, D9).
- **Browsers:** the module needs native WebAssembly exceptions in the legacy encoding: Chrome 95,
  Firefox 100, Safari 15.2 or later (map 02, planning hub decision 1). It uses no threads and no
  `SharedArrayBuffer`, so it needs no cross-origin isolation. A page whose CSP sets
  `script-src` or `default-src` must allow `'wasm-unsafe-eval'` (map 03, D8).

### The render core

- **The instrument** (working names `modular::web::Instrument` and `Rendering`) is made from a
  soundfont's bytes and, in C++ only, a voice limit that defaults to the node's 256. The golden
  pool clip sets it; the JavaScript export passes none (this spec's ruling C2).
- **It keeps one graph:** `TriggerAtNode(0)` → `MidiFileNode` → `SoundFontNode` → the glue's
  stereo sink. The soundfont node is loaded once, from the bytes. Nothing is wired after the
  node but the sink: no gain, so the output is the node's own bits and the level is the page's
  (ruling G3).
- **A render** takes MIDI bytes and a sample rate:
  1. A rate that is not finite and positive throws before anything is allocated.
  2. The MIDI bytes are imported into the kept `MidiFileNode`. Malformed bytes throw its
     message, and the instrument keeps working.
  3. A file whose duration exceeds the limit, ten minutes, throws an explicit error before the
     sink is sized (map 04, D7; ticket 06). The limit is a named default in one place.
  4. The sink is sized for the file's frames plus the cap, two seconds of frames at the rate.
  5. A fresh `Engine` renders every one of those frames. Afterwards the core checks
     `Engine::errorMessage()` and throws it if set (map 04, D10; ticket 06).
  6. The trim keeps the file's frames whole and cuts the tail after its last frame where
     either channel's magnitude reaches `1e-4f` (−80 dBFS). The threshold is a float literal,
     not computed, so both builds compare the same value. The length is therefore at least the
     file's frames and at most the file's frames plus the cap (map 04, D6; ruling G1).
  7. The result is the requested rate and the trimmed left and right channels.

  The block size is the core's choice. The output does not depend on it, as for every node.
- **The sink** (ruling G2) is private to the glue. It is preallocated, not chunked: sized
  before each render from the file's duration and the rate, with planar left and right
  buffers. It resets in `prepare()`, allocates nothing in `process()` and never writes past its
  capacity. `CaptureSinkNode` is not used: it grows in `process()` and keeps its samples across
  engines, so a kept graph would append each render to the last.
- **Errors** are authoring errors and throw `std::invalid_argument` with short lowercase
  messages. The nodes' own messages pass through unchanged. The core's own use the context
  `render:`.

### The module and its loader

- **Exports** wrap every core call in a `catch` of `std::exception`, plus a catch-all. Each
  returns a status, with the message readable from the module. `std::bad_alloc` becomes an
  out-of-memory message. No C++ exception leaves the module, and `getExceptionMessage` and the
  C++ demangler are not linked (map 02, planning hub decision 1).
- **After a render**, the module keeps one rendering until the loader has copied its channels
  out and released it, so WebAssembly memory holds at most one rendering at a time.
- **Build flags under Emscripten:** `-fwasm-exceptions`, the legacy encoding that 6.0.13 uses by
  default, at compile and link for every target, `-sMODULARIZE` with `-sEXPORT_ES6`,
  `-sENVIRONMENT=web,node`, `-sALLOW_MEMORY_GROWTH`, and `-O2`, with no threads. Test
  executables that read or write files also link Node's raw file system.
- **The loader** is hand-written, not generated:
  - it runs calls through one queue per module;
  - it turns a failed status into a rejected `Error`;
  - it copies the channels out with a slice;
  - it handles traps as above;
  - it passes `locateFile` through to the Emscripten factory.
- **The `web` presets.** `CMakePresets.json` stays at version 2. Its `web` configure preset uses
  Ninja, `build/web`, Release, and the toolchain file from the `EMSDK` environment variable, as
  a cache variable. Its build preset builds everything, and its test preset runs the web build's
  test scope.

### Version and manifest

These are rulings G6, under map 03, D5.

- The build bakes into the module three facts, which `loadModular()` exposes as `version`:
  - the commit of the tree it was built from;
  - whether that tree had uncommitted changes (`dirty`);
  - the Emscripten version.
- The build itself writes the manifest, so the local stopgap and the CI artifact carry the same
  files and the same manifest. It records:
  - the commit, and whether the tree was dirty;
  - the Emscripten version;
  - the build flags;
  - each shipped file's name, size and SHA-256.
- The output is reproducible from the same commit and toolchain, so anyone can rebuild a commit
  and compare hashes (map 03, J4 facts).

### The web build's scope and CI

This is map 03, D1, D2 and D10, with ruling G7.

- **The scope.** The `web` preset builds the render core, the module and the loader. Under Node
  it runs these tests:
  - the kernel, node, MIDI-file, soundfont and authoring tests, with persistence;
  - kernel purity;
  - the render core's tests and the Node smoke test.

  Persistence and the authoring tests join once the prefactor removes the libc++ break. A test
  case that fails under Node only because of the file system is excluded by name, with its
  reason, in `docs/web.md`. Excluded with reasons:
  - the graph editor and its tests (Windows only, as today);
  - `midi_cli_smoke`, whose script runs the CLI directly;
  - the extract tool's test, a native tool check.
- **The job.** A `web` job in the existing workflow:
  - it needs `classify`, and is skipped on the documentation-only path as `windows` is;
  - it runs on a pinned `ubuntu-24.04`;
  - it clones emsdk at tag 6.0.13, checks the tag's commit, then installs and activates it;
  - it configures, builds and runs CTest with the `web` presets;
  - it uploads the module's files and manifest as a workflow artifact named for its commit, on
    every run, with an action pinned by SHA;
  - it has no cache to start with.
- **The gate.** `CI gate` adds `web` to its needs and checks it as it checks `windows`: success on
  the build path, success or skipped on the documentation-only path. Permissions stay
  `contents: read`, and no ruleset changes.
- **The pin.** The Emscripten version lives in one place in the workflow and in `docs/web.md`, and
  moves in one pull request.

### The extract tool

This is map 03, D6 and D7, with rulings C1 and G8.

- **What it copies:** one preset per run, named by bank and program. It copies:
  - the preset with its zones, global zone included;
  - the instruments those zones name, with their zones;
  - the samples those zones play, each followed by 46 zero points.

  Generators and modulators are copied verbatim. Every index (instrument, sample, bag,
  generator, modulator, sample offsets) is relinked to the new file, and each hydra list ends
  with its terminal record. A sample link to a sample left out of the extract is cleared; the
  node never reads links (map 09, D6). Nothing else from the source is kept.
- **What it refuses**, with a message and a non-zero exit, writing nothing:
  - a SoundFont 3 file, or one with a compressed sample;
  - a file with 24-bit sample data (`sm24`);
  - a preset the file lacks;
  - a malformed file.
- **Licence and provenance in the INFO list.** The full licence notice, read from a file given
  on the command line, goes into the comment field (`ICMT`, at most 65,536 bytes), followed by
  the provenance. A short copyright line goes into the copyright field (`ICOP`, at most 256
  bytes). The provenance is:
  - the source URL;
  - the source's version;
  - the source's SHA-256, computed by the tool;
  - the preset's bank, program and name;
  - the Modular commit the tool ran from, marked dirty if that checkout had uncommitted
    changes.

  **The extract's own SHA-256 is not inside it**, since a file cannot carry its own hash. It
  lives in the file midishare keeps beside the extract and in ticket 04's evidence (ruling C1).
- **Deterministic:** the same source, preset, notice, provenance and commit give the same bytes.
  No date or other changing value is written.
- **Where the extract lives:** only in midishare, under a content-hashed name, with the full
  notice in a file beside it (map 03, D7). Neither the full soundfont nor the extract is ever
  committed to this repository.

### Decided while writing this spec

Decided by the planning hub under the judgment the user delegated for the map ("All, on your
best judgment"), 2026-11-24, on this spec's step-one check-in.

- **The seams** under Testing Decisions, and the reuse of the existing suites.
- **The slices:** the first end-to-end slice is split in two. Ticket 02 builds the render core
  natively, and ticket 03 the module and its loader. After 03, midishare can take a local
  build. The extract tool comes before CI, because midishare cannot sound the piano without the
  extract.
- **C1:** the extract carries every provenance field except its own SHA-256, which lives in
  midishare's side file and ticket 04's evidence.
- **C2:** the render core's instrument takes a voice limit in C++, defaulting to 256. The
  JavaScript export passes none.
- **G1:** the trim never cuts into the file's own frames; the threshold is the float literal
  `1e-4f` on either channel; a file of zero duration gives zero frames, and the declarations say
  so.
- **G2:** the sink is preallocated, planar, private to the glue, reset in `prepare()`, and never
  allocates in `process()`.
- **G3:** the glue adds no gain.
- **G5:** after a trap, an instrument re-creates itself on the fresh instance from its own
  JavaScript copy of the soundfont's bytes, never from the page's array.
- **G6:** the version is the commit, a dirty flag and the Emscripten version. The build writes
  the manifest.
- **G7:** persistence and the authoring tests join the web build in ticket 05. Only named
  file-system cases are excluded, each with its reason.
- **G8:** the extract tool's rules above. Its test also compares the preset's zones and sample
  points structurally.
- **G10:** the Firefox and Safari check page and its fixtures live with the tests, and the `web`
  job uploads them as a separate check artifact, apart from the module artifact. Ticket 08
  settles the logistics with the user.
- **The `COPYING` download is approved.** In the planning hub on 2026-11-24 the user approved
  downloading the `COPYING` text of Debian's `fluid-soundfont` 3.1-6, in their words: "Yes,
  approve it". It covers that one text file, from Debian's sources site, kept outside the
  repository and passed to the extract tool as its notice. The FluidSynth download for spec A's
  ticket 07 is not covered and stays unapproved.

## Testing Decisions

A good test here drives Modular only through what a client can do: hand in bytes and a rate,
read the samples, catch the errors. It never reaches into the graph the instrument keeps, the
sink's buffers or the loader's queue. The golden tests are the deliberate exception: they pin
rendered bits, so that every change to them is reviewed (map 08, D5).

**Three seams:**

1. **S1: the render core's C++ function**, the code behind the module's exports. A doctest
   executable in CTest. It runs natively in the Windows Debug and Release jobs, and under Node
   in the web job: Emscripten's toolchain sets Node as CMake's cross-compiling emulator, so the
   same `add_test` runs there. It covers the graph, the sink, the tail and trim, the length
   limit, errors, the voice-limit parameter and the golden clips. Prior art: the MIDI-file tests
   and the soundfont tests, which render through `Engine` into a sink at several rates.
2. **S2: the JavaScript surface**, `loadModular` → `createInstrument` → `render`, through the
   shipped loader and module. One Node smoke test, which CTest runs under the `web` preset.
   It covers only what C++ cannot:
   - rejection as an `Error` with Modular's message;
   - copy-out and channel order;
   - serialized calls;
   - the trap path;
   - `version`;
   - one golden hash.

   A small fixture writer, built from spec A's test-side soundfont builder, writes its soundfont
   into the build tree. The test builds its MIDI bytes itself.
3. **S3: the extract tool's command line.** A CTest script test, native only. Prior art:
   `midi_cli_smoke`'s CMake script.
   - A native helper writes a self-made soundfont with several presets.
   - The test runs the tool on one of them, and the helper renders that preset from both files
     through `SoundFontNode`. The renders must be bit-identical.
   - The tool's own reader also compares the preset's zones, generators, modulators and sample
     points in both files, so a copy fault in something the node does not read yet still fails.

**Existing suites reused:**

- The MIDI-file tests for the prefactor's tie frame and for `MidiFileNode`'s import from bytes.
- The authoring tests for the persistence round trip, whose saved bytes do not change.
- The soundfont tests, which carry spec A 08's golden test, run under Node once ticket 05
  widens the web build's scope.

**The golden check** (map 08, D5, D6):

- **Per clip and rate:** the frame count and a 64-bit hash of each 4,096-frame block, as values
  committed in the test.
- **The hash:** defined once in a test header and shared with spec A 08's golden test, whichever
  lands first. It must be computable exactly in JavaScript, for example FNV-1a over each
  sample's IEEE bits, frames in order, left before right. The smoke test ports it.
- **On a mismatch:** the test prints the new values and the first differing block. A change that
  alters what Modular renders updates the values in the same commit.
- **Spec A's clip:** every maths path, in spec A 08.
- **This spec's clips**, through S1:
  - the timing tie: an event every 40 ticks at `Q:1/4=90` through tick 15,000, which split
    under `long double`, then a tempo change;
  - the pool, at a voice limit of 4, under a dense passage, so that notes are taken with spec
    A's 6 ms fade;
  - the one-note preview.
- **Rates and sources:** 44.1, 48 and 22.05 kHz. Every soundfont comes from the shared
  test-side builder, and no soundfont file enters `tests/`.

**Conventions:** doctest cases named as sentences that state invariants, odd or prime frame
offsets, and a fresh graph or instrument for each independent render. The focused checks and
full suite are those `docs/windows.md` names for native work and those `docs/web.md` names for
the web build. Each ticket that adds a CTest entry updates the description of the suite.

**Not tests, but acceptance steps** (ticket 08), because each needs a browser, a licensed file
kept outside the repository, or the user:

- the default piano's extract rendered by the module under Node and natively, with equal
  hashes;
- a one-note preview, with the extract loaded, timed in a browser;
- Firefox and Safari, checked by hand.

## Out of Scope

- **midishare's page:** its Modular engine file, its fallback, its padding of trailing rests,
  where its render runs (main thread or a Web Worker), and its tests. Sessions here do not edit
  the midishare repository.
- **Delivering files into midishare.** A midishare session commits the CI artifact, and the
  extract with its notice and provenance file. The extract tool's fixed output lets that session
  rerun it and compare the hash.
- **Releases, tags and npm packages** (map 03, D4, option 2). They need the user's approvals and
  can replace the artifact later without changing the files.
- **Spec A's node work:** the soundfont node, its portable maths, its codec and its FluidSynth
  comparison. This spec adds the Node half of spec A 08's golden test only by running the
  soundfont tests in the web job.
- **Options on the JavaScript surface,** such as a preset or a voice limit, until a client needs
  one. The map's stretch goal, a choice among a few soundfonts, works through more instruments,
  one per file, with no surface change.
- **A multi-preset extract,** until the stretch goal is decided.
- **A saved-graph instrument and a JavaScript authoring API** (map 04, D4). The first can arrive
  later behind the same instrument handle; the second would decide roadmap ticket 07 by the
  back door.
- **Persistence in the module.** The module links no persistence. Persistence joins the web
  build only as code that compiles and is tested.
- **Realtime playback, an AudioWorklet, streaming, and rendering on a server** (the map's Out of
  scope).
- **A node-reported tail length** (map 04, D6), and the newer exnref exception encoding (one flag
  later, map 02).
- **Any change under `kernel/` or `workspace/`.**

## Further Notes

- **Order of work.**
  - 01 first; 02 after it, since both edit `MidiFileNode`; then 03.
  - 04, the extract tool, needs nothing from this spec and may run beside them.
  - 05 and 06 each need 03.
  - 07 needs 05, spec A 03 and spec A 08.
  - 08 needs 04, 07, spec A 02, spec A 03 and the user's steps.

  midishare's path:
  - after 03, a local build (map 03, D4, option 1a);
  - after 04, the default piano's extract;
  - after 05, the CI artifact (option 1b).
- **Cross-spec facts.**
  - **The piano sounds right only after spec A 02 and 03.** Before 02, the default piano's
    looped samples (`sampleModes` 1) play once and stop. Before 03, a full pool drops new notes
    instead of taking old ones. Tickets 02 and 03 here need only spec A 01, which is done.
  - **A later spec A ticket that lands after ticket 07 updates 07's golden values in the same
    commit**, as spec A 08 already requires for its own golden test. Spec A 02, 04 and 05 all
    change the node's output.
  - **Ticket 01 and spec A 06 both edit persistence's source,** in different codecs. Whichever
    lands second merges `main`.
- **Acceptance.**
  1. Each ticket's own checks: its focused checks, the full suite, natively and under the `web`
     preset where it applies, and the code review.
  2. Ticket 04 makes the default piano's extract and checks it renders as the full file does.
  3. Ticket 08: the extract in the module under Node equals native; a one-note preview is timed
     in a browser (map 04, D8); Firefox and Safari are checked by hand (map 08, D8).
  4. Last, outside this spec: the user's judgment by ear on the live site, which is spec A's
     sound-parity item.
- **Provenance of the default piano** (map 03, D7; map 10). The fixed parts, recorded now:
  - source: `https://sources.debian.org/data/main/f/fluid-soundfont/3.1-6/FluidR3_GM.sf2`;
  - Debian `fluid-soundfont` 3.1-6;
  - 148,398,306 bytes, SHA-256
    `74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0`;
  - preset 0:0, "Yamaha Grand Piano", every zone;
  - licence: MIT, with the full notice from the same source's `COPYING`.

  The tool's commit and the extract's size and SHA-256 are recorded in
  [ticket 04](issues/04-the-soundfont-extract-tool.md)'s evidence when it runs.
  `FluidR3_GM.sf2` is already on the development machine, outside both repositories. Fetching it
  again, if it is ever missing, needs the user's yes at that time.
- **User approvals.**
  - The `COPYING` download is approved (see
    [Decided while writing this spec](#decided-while-writing-this-spec)).
  - Firefox's install, if the user chooses to check it on the development machine, is the
    user's own step.
  - The Safari check is the user's, on an Apple device.
  - Nothing here approves the FluidSynth download for spec A's ticket 07.
- **Numbers to expect** (map 02): about 37.7 KB over the wire for module and glue with Brotli,
  before the soundfont node. A render takes 1.1 to 1.25 times native time. Linear memory never
  shrinks, which is why the sink is preallocated and a rendering is released once copied out.
- **Vocabulary.** Use `CONTEXT.md`'s terms: Modular (never "the engine" for the product),
  `Engine`, primitive node, stored node content, authored graph, compile, stream time. The page
  is a *client*. Soundfont, preset and voice are node-library words (`docs/node-io.md`);
  instrument, render and rendering are this surface's words (`docs/web.md`).
