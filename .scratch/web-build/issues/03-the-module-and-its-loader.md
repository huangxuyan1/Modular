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
freshly fetched `origin/main` at `5952479`. Before its first push it was rebased onto
`df4d78d`, where soundfont node 03 had landed; no file overlapped.

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
    - An instrument's handle is an `std::int32_t`, its index plus one, so 0 means a failure.
      A render first releases the last rendering, so the module holds at most one. A frame
      count crosses the boundary as an `std::uint32_t`.
  - `CMakeLists.txt`, under `if(EMSCRIPTEN)`:
    - every target compiles and links with `-fwasm-exceptions`;
    - Release's `-O3` becomes `-O2` for every target, so the libraries in the module compile
      at the level the probe measured, not only its own file;
    - the `modular_web` executable is written as `dist/modular_web.mjs` and `.wasm`, linked
      with `-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1`,
      exporting `_malloc` and `_free`, and the runtime's `HEAPU8`, `HEAPF32` and
      `UTF8ToString`;
    - the loader and its declarations are copied into `dist/` beside it;
    - configuring stops with a message when no Node is set to run the tests.
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
    import of `modular.mjs`. It states the spec's surface, with one refinement below: the
    channels are `Float32Array<ArrayBuffer>`.
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
  - **The channels are declared `Float32Array<ArrayBuffer>`**, where the spec writes
    `Float32Array`.
    - Since TypeScript 5.7 a bare `Float32Array` means `Float32Array<ArrayBufferLike>`, which
      may be shared memory. TypeScript 6.0's `lib.dom.d.ts` declares
      `AudioBuffer.copyToChannel(source: Float32Array<ArrayBuffer>, ...)`. So the spec's form
      fails a strict page's type check at exactly the call map ticket 04 sketches. That is
      user story 21.
    - The check used the TypeScript 6.0.3 already bundled with the local VS Code install, so
      nothing was downloaded. A strict page, under both `NodeNext` and `Bundler` resolution,
      gave two errors with the spec's form and none with the refined one.
    - The arrays really are `Float32Array<ArrayBuffer>`, since `HEAPF32.slice` makes a fresh,
      unshared buffer. The generic form needs TypeScript 5.7 or later; the declaration file
      and `docs/web.md` say so.
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
    - dropping `locateFile` fails its case;
    - in the module, an export that rethrows instead of catching fails the bad-MIDI case:
      the page then receives a `WebAssembly.Exception`, not an `Error`.

    The call-order case could not be made to fail. Every call runs synchronously inside the
    queue, so no plausible mutation reorders them; the case guards the contract for when a
    render moves into a Worker. Ticket 06's call-order work can make the queue observable,
    since re-instantiating after a trap is asynchronous.
- *Not linked.* The glue has no `getExceptionMessage`, and the module exports nothing beyond
  the seven functions, `_malloc`, `_free` and the three runtime helpers. The `.wasm` holds
  none of the demangler's strings (`(anonymous namespace)`, `decltype(`, `typeinfo for`,
  `vtable for`).
- *Sizes* (bytes, raw / gzip -9 / Brotli 11), `-O2`, measured on the final tree, with
  soundfont node 03 in it:
  - `modular_web.wasm`: 109,013 / 50,325 / 42,712;
  - `modular_web.mjs` (the glue): 7,698 / 2,710 / 2,461;
  - `modular.mjs` (the loader): 3,489 / 1,408 / 1,173;
  - `modular.d.mts`: 1,847 / 866 / 701, never loaded by a page;
  - loaded by a page, `.wasm`, glue and loader: 120,200 / 54,443 / 46,346.

  The module and glue are 45.2 KB with Brotli, 7.5 KB more than the probe's sine-voice
  module (37.7 KB), and the loader adds 1.2 KB.

  The loader and declarations are copied byte for byte from the checkout. With Git's
  `core.autocrlf` on Windows they get CRLF line endings, and a Linux checkout gets LF, so
  their hashes differ between hosts. Ticket 05's manifest and reproducibility work should
  pin their line endings, for example in `.gitattributes`.
- *Checks, on the final tree.*
  - Native, through the script: Debug 11/11 and Release 11/11 CTest entries, both `desktop`
    tests, the extract tool's test and kernel purity included. doctest cases, the same in
    both: kernel 44, node 17, authoring 29, MIDI-file 18, soundfont 66 (with soundfont node
    03), render core 14.
  - `web`, under Emscripten 6.0.13 and emsdk's Node 24.19.0:
    - `cmake --preset web` and `cmake --build --preset web` build all 40 steps, every target
      included. The only warnings are two existing `u8path` deprecations in
      `nodes/src/WavRecorderNode.cpp` and `nodes/src/PrimitivePersistence.cpp`.
    - `ctest --preset web`: 3/3.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at `424056b`,
  after the rebase, with a clean working tree. The fixes were re-checked on the final tree.
  - Standards. Fixed:
    - **Fixed-width integers at the boundary** (`docs/project-foundations.md`: fixed-width
      integers for contractual ids and frames). The handle is an `std::int32_t`, and the
      frame count an `std::uint32_t`.
    - a 92-column accessor, now wrapped like its twin;
    - `<utility>` included for `std::move`;
    - names: the loader's `made` became `state`, `modular_render`'s parameter is `handle` as
      in its neighbour, and the test's `variable` became `variableLength` and its `piano`
      became `instrument`;
    - `docs/web.md` no longer says every message starts with its context, since
      `out of memory` does not.
  - Both axes: **an unset `EMSDK_NODE`** leaves the emulator defined but empty. The
    toolchain then skips its own search, and CTest would run the `.mjs` directly.
    Configuring now stops with a message instead.
  - Spec, fixed: **the channels' type**, above. The reviewer suspected the defect; the
    TypeScript check confirmed it.
  - Kept as judgement calls:
    - **Possible Duplicated Code.** The fixture's looping sine repeats the shape of
      `sine()` in `tests/test_web_core.cpp` and in the soundfont tests. Helpers stay in
      their test files, as `docs/project-foundations.md` asks. Moving one into
      `tests/SoundFontTestFiles.h` would edit the soundfont suite's shared builder, which
      another session works on.
    - **Possible Duplicated Code**, weaker: `createInstrument` and `render` both copy,
      queue, call and check. They are two calls of six lines, and a shared helper would
      hide which export each makes.
    - **`-O2` for every Release target**, not only the module. The libraries compile into
      the module, so this is the level the probe measured.
    - **The kept, unread soundfont copy** is ruling G5's.
  - Left to ticket 06, which owns argument checks:
    - **an `ArrayBuffer` argument**, outside the declared `Uint8Array`, makes
      `new Uint8Array(...)` a view, not a copy. Ticket 06 rejects any argument that is not
      a `Uint8Array`.
    - **The call-order case cannot fail today** (above).
