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

**Status:** done

**Left to later tickets:**

- `version`, the manifest, the CI job and the web build's full test scope (05);
- traps, the length limit and the remaining error paths (06);
- golden hashes (07).

## Checklist

- [x] A module target in `apps/web/`, built only under Emscripten. It exports C functions over
  the render core: create an instrument, render, read the rendering's frame count and
  channels, release the rendering, and read the last error.
  - Every export catches `std::exception` and anything else, and returns a status.
  - Its message is readable from the module.
  - Nothing links `getExceptionMessage` or the C++ demangler.
- [x] Build flags under Emscripten, for every target at compile and link:
  - `-fwasm-exceptions`, the legacy encoding that 6.0.13 uses by default;
  - for the module, `-sMODULARIZE` with `-sEXPORT_ES6`, `-sENVIRONMENT=web,node`,
    `-sALLOW_MEMORY_GROWTH` and `-O2`;
  - no threads, no fast maths, no relaxed SIMD.

  The native build does not change.
- [x] A hand-written ES module loader:
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
- [x] A TypeScript declaration file states the surface as the spec names it, including that a
  rendering's length may be 0. The review checks it against the loader. Type-checking it with
  `tsc` needs TypeScript installed, a download that needs the user's yes, so it is not
  required here.
- [x] `CMakePresets.json` stays at version 2 and gains a `web` configure preset:
  - Ninja, `build/web`, Release;
  - Emscripten's toolchain file from the `EMSDK` environment variable, as a cache variable.

  It also gains `web` build and test presets. Until ticket 05, the test preset runs only the
  render core's tests and the smoke test.
- [x] Under the `web` preset, the render core's tests (seam S1) pass under Node.
- [x] A Node smoke test (seam S2) runs in CTest under the `web` preset. A fixture writer, built
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
- [x] `docs/web.md`:
  - the web as a platform, with what this ticket's build covers;
  - the surface: names, types, the tail and trim, lengths, errors and call order;
  - the browser floor (Chrome 95, Firefox 100, Safari 15.2);
  - no cross-origin isolation, and the CSP condition (`'wasm-unsafe-eval'` if a page sets
    `script-src` or `default-src`);
  - caching under hashed names, and `locateFile`;
  - the local build recipe from a clean `main` checkout with emsdk 6.0.13, and which files a
    client takes.
- [x] The module's and glue's sizes (raw, gzip, Brotli) are measured and recorded under
  Comments.
- [x] Nothing changes under `kernel/` or `workspace/`. Kernel purity and the full native suite
  pass (`docs/windows.md`), the `web` preset's tests pass, and `/code-review` runs on the
  branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/web-build-03-module-and-loader`, cut from a
freshly fetched `origin/main` at `5952479`.

- *What was built.*
  - `apps/web/Exports.cpp`, the module's C functions, built only under Emscripten:
    `modular_create_instrument`, `modular_render`, `modular_rendering_frames`,
    `modular_rendering_left`, `modular_rendering_right`, `modular_release_rendering` and
    `modular_error`.
    - Each fallible export runs its work through one `guarded` helper. It catches
      `std::bad_alloc` as `out of memory`, any other `std::exception` with its `what()`, and
      anything else as `unknown error`, and returns 0.
    - The message is copied into module memory. When that copy itself runs out of memory,
      the message becomes `out of memory`.
    - An instrument's handle is its index plus one, so 0 means a failure. A render first
      releases the last rendering, so the module holds at most one.
  - `CMakeLists.txt`, under `if(EMSCRIPTEN)`:
    - every target compiles and links with `-fwasm-exceptions`;
    - Release's `-O3` becomes `-O2` for every target, so the libraries in the module compile
      at the level the probe measured, not only its own file;
    - the `modular_web` executable is written as `dist/modular_web.mjs` and `.wasm`, linked
      with `-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1`,
      exporting `_malloc` and `_free`, and the runtime's `HEAPU8`, `HEAPF32` and
      `UTF8ToString`;
    - the loader and its declarations are copied into `dist/` beside it.
    - The native build is unchanged: the new lines are all under `if(EMSCRIPTEN)`.
  - `apps/web/modular.mjs`, the hand-written loader.
    - `loadModular` passes `locateFile` to the factory only when given.
    - One promise chain per module is the queue.
    - `createInstrument` and `render` copy their bytes with `new Uint8Array(...)` when they
      are called, not when the queue reaches them.
    - Both methods are `async`, so a call that throws before it is queued still returns a
      rejected promise.
    - A render copies each channel with `HEAPF32.slice`, then releases the rendering in a
      `finally`.
  - `apps/web/modular.d.mts`. `.d.mts` is the declaration file TypeScript resolves for an
    import of `modular.mjs`.
  - `CMakePresets.json`, still version 2:
    - the `web` configure preset: Ninja, `build/web`, Release, the toolchain file from
      `$env{EMSDK}`, as a cache variable;
    - `web` build and test presets. The test preset runs only `web_core_tests`,
      `web_smoke_fixture` and `web_smoke` until ticket 05.
  - `docs/web.md`, new; `docs/project-foundations.md` names the module and links it.
  - Nothing under `kernel/` or `workspace/` changed. No midishare name, path or limit is in
    the code or the docs.
- *Choices made here.*
  - **The emulator is emsdk's own Node.** The `web` preset sets `CMAKE_CROSSCOMPILING_EMULATOR`
    to `$env{EMSDK_NODE}`. Without it, the toolchain's `find_program` picked a system Node
    found first on `PATH` (24.14.0 here, where emsdk's is 24.19.0), so a test run depended on
    the machine.
  - **`render` copies its MIDI bytes at call time too.** The ticket asks only
    `createInstrument` to copy. But calls are queued, so a page that changed its MIDI array
    before the queue reached its render would change what is rendered. MIDI files are small,
    so the copy is cheap. A Web Worker's `postMessage` would copy at call time as well.
  - **The instrument keeps its soundfont copy for its life** (ruling G5). Nothing reads it
    again until ticket 06 re-creates instruments after a trap. It is held on the
    instrument's own state, which its `render` reads, so the engine cannot drop it.
  - **The JavaScript side refuses an allocation that fails.** When `_malloc` returns 0 for a
    non-empty copy, the call rejects with `out of memory`, the message the exports use.
    Ticket 06 owns the out-of-memory test.
  - **Trap handling is left to ticket 06.** Today a trap rejects its call with the
    WebAssembly error. `docs/web.md` says so and tells a page to load a new module.
  - **Build output.** `build/web/dist/` holds the four files a client takes. The CTest
    fixture writes `build/web/web-smoke.sf2`.
- *Tests.*
  - `web_core_tests` (seam S1) runs unchanged under Node: 14 cases, 85 assertions.
  - `web_smoke` (seam S2), `tests/test_web_smoke.mjs`, uses `node:test` with 6 cases. It
    runs after the `web_smoke_fixture` CTest fixture, which writes its soundfont.
    - The writer is `tests/web_smoke_fixture.cpp`, built from `tests/SoundFontTestFiles.h` and
      linked with `-sNODERAWFS=1`. Its soundfont is a looping sine, panned full left, with a
      half-second release.
    - The test builds its MIDI bytes itself. The cases:
      - load, create and render at 44.1 and 48 kHz: two equal-length `Float32Array`s, the
        rate echoed, a length between the file's frames and those plus two seconds, a note
        that sounds on the left, and an exactly silent right channel, which shows the order.
        Each channel owns its own buffer, so it is a copy, not a view of module memory.
      - The same bytes render equal arrays. A first result is unchanged after a 60-second
        render. That render's 23 MB rendering is more than the module's initial 16 MiB;
        memory grew from 16.1 to 22.9 MiB, measured once outside the test.
      - Bad MIDI bytes reject with an `Error` whose message starts `midi file:`, and the next
        render equals the one before.
      - Three calls made without awaiting, across two instruments, settle in call order, each
        with its own result.
      - Changing the page's soundfont array right after `createInstrument`, and its MIDI array
        right after `render`, changes nothing.
      - `locateFile` is called for `modular_web.wasm`, and the module it located renders.
  - *Red, then green.* The build plumbing came first, so each smoke case was shown to fail
    against a mutated copy of the built loader instead:
    - a lazy soundfont copy and a lazy MIDI copy each fail the copy case;
    - a `subarray` view in place of a `slice` fails the first case, and the next two;
    - swapped channels fail the first case;
    - a failed render that resolves, and an `Error` without Modular's message, each fail the
      bad-MIDI case;
    - dropping `locateFile` fails its case.

    The call-order case could not be made to fail. Every call runs synchronously inside the
    queue, so no plausible mutation reorders them; the case guards the contract for when a
    render moves into a Worker.
- *Not linked.* The glue has no `getExceptionMessage`, and the module exports nothing beyond
  the seven functions, `_malloc`, `_free` and the three runtime helpers. The `.wasm` holds
  none of the demangler's strings (`(anonymous namespace)`, `decltype(`, `typeinfo for`,
  `vtable for`).
- *Sizes* (bytes, raw / gzip -9 / Brotli 11), `-O2`, measured on the final tree:
  - `modular_web.wasm`: 107,728 / 49,783 / 42,241;
  - `modular_web.mjs` (the glue): 7,698 / 2,710 / 2,461;
  - `modular.mjs` (the loader): 3,486 / 1,406 / 1,171;
  - `modular.d.mts`: 1,593 / 756 / 622, never loaded by a page;
  - loaded by a page, `.wasm`, glue and loader: 118,912 / 53,899 / 45,873.

  The module and glue are 44.7 KB with Brotli, 7 KB more than the probe's sine-voice module
  (37.7 KB), and the loader adds 1.2 KB.
- *Checks, on the final tree.*
  - Native, through the script: Debug 11/11 and Release 11/11 CTest entries, both `desktop`
    tests, the extract tool's test and kernel purity included. doctest cases, the same in
    both: kernel 44, node 17, authoring 29, MIDI-file 18, soundfont 59, render core 14.
  - `web`, under Emscripten 6.0.13 and emsdk's Node 24.19.0:
    - `cmake --preset web` and `cmake --build --preset web` build all 40 steps, every target
      included. The only warnings are two existing `u8path` deprecations in
      `nodes/src/WavRecorderNode.cpp` and `nodes/src/PrimitivePersistence.cpp`.
    - `ctest --preset web`: 3/3.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
