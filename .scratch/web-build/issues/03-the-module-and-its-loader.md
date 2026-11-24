# 03: The module and its loader: a page renders through `loadModular`

**What to build:** A web page, or Node, imports Modular's loader and calls `loadModular()`, then
`createInstrument(soundfontBytes)`, then `render(midiBytes, sampleRate)`. It gets back
`{ sampleRate, channels: [Float32Array, Float32Array] }`, arrays that JavaScript owns. A failure
rejects with an `Error` carrying Modular's message, and no C++ exception ever reaches
JavaScript. A documented local build from a clean `main` checkout produces the files, so the
midishare session can integrate the surface before CI builds it (map ticket 03, decision 4,
option 1a). See the [spec](../spec.md) (The surface; The module and its loader).

**Blocked by:** 02: The render core: an instrument renders MIDI bytes to trimmed stereo,
natively.

**Status:** ready-for-agent

**Left to later tickets:**

- `version`, the manifest, the CI job and the web build's full test scope (05);
- traps, the length limit and the remaining error paths (06);
- golden hashes (07).

## Checklist

- [ ] A module target in `apps/web/`, built only under Emscripten. It exports C functions over
  the render core: create an instrument, render, read the rendering's frame count and
  channels, release the rendering, and read the last error.
  - Every export catches `std::exception` and anything else, and returns a status.
  - Its message is readable from the module.
  - Nothing links `getExceptionMessage` or the C++ demangler.
- [ ] Build flags under Emscripten, for every target at compile and link:
  - `-fwasm-exceptions`, the legacy encoding that 6.0.13 uses by default;
  - for the module, `-sMODULARIZE` with `-sEXPORT_ES6`, `-sENVIRONMENT=web,node`,
    `-sALLOW_MEMORY_GROWTH` and `-O2`;
  - no threads, no fast maths, no relaxed SIMD.

  The native build does not change.
- [ ] A hand-written ES module loader:
  - `loadModular(options?)` instantiates the module and passes `locateFile` through to it. The
    glue otherwise finds its `.wasm` beside itself.
  - `createInstrument(bytes)` copies the bytes into JavaScript once, then hands them to the
    module.
  - `render(midi, sampleRate)` copies both channels out of WebAssembly memory, then releases the
    rendering, so the module holds at most one rendering.
  - Every call returns a promise. One queue per module runs calls one at a time, in call
    order, across instruments.
  - A failed status rejects with an `Error` whose message is Modular's, and the next call still
    works.
- [ ] A TypeScript declaration file states the surface as the spec names it, including that a
  rendering's length may be 0. The review checks it against the loader. Type-checking it with
  `tsc` needs TypeScript installed, a download that needs the user's yes, so it is not
  required here.
- [ ] `CMakePresets.json` stays at version 2 and gains a `web` configure preset:
  - Ninja, `build/web`, Release;
  - Emscripten's toolchain file from the `EMSDK` environment variable, as a cache variable.

  It also gains `web` build and test presets. Until ticket 05, the test preset runs only the
  render core's tests and the smoke test.
- [ ] Under the `web` preset, the render core's tests (seam S1) pass under Node.
- [ ] A Node smoke test (seam S2) runs in CTest under the `web` preset. A fixture writer, built
  from spec A's test-side soundfont builder and linked with Node's raw file system, writes its
  soundfont into the build tree first. The test builds its MIDI bytes itself. Through the
  loader it shows:
  - `loadModular` resolves, and `createInstrument` and `render` give two equal-length
    `Float32Array`s at the requested rate, with a note that sounds;
  - the same bytes rendered twice give equal arrays, and a first result is unchanged after a
    longer render grows the module's memory;
  - bad MIDI bytes reject with an `Error` whose message starts `midi file:`, and the next
    render succeeds;
  - three calls made without awaiting resolve in call order;
  - changing the page's soundfont array after `createInstrument` changes nothing.
- [ ] `docs/web.md`:
  - the web as a platform, with what this ticket's build covers;
  - the surface: names, types, the tail and trim, lengths, errors and call order;
  - the browser floor (Chrome 95, Firefox 100, Safari 15.2);
  - no cross-origin isolation, and the CSP condition (`'wasm-unsafe-eval'` if a page sets
    `script-src` or `default-src`);
  - caching under hashed names, and `locateFile`;
  - the local build recipe from a clean `main` checkout with emsdk 6.0.13, and which files a
    client takes.
- [ ] The module's and glue's sizes (raw, gzip, Brotli) are measured and recorded under
  Comments.
- [ ] Nothing changes under `kernel/` or `workspace/`. Kernel purity and the full native suite
  pass (`docs/windows.md`), the `web` preset's tests pass, and `/code-review` runs on the
  branch's diff.
