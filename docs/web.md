# The web

The web is a supported Modular platform. Emscripten 6.0.13 builds a WebAssembly module from
`apps/web/`, and a hand-written loader gives a web page, or Node, one surface: hand in a
soundfont's bytes once, then render Standard MIDI File bytes to stereo samples, clip after
clip. The surface knows nothing of the page that calls it: no notation, no player and no
Web Audio. The page is a *client*; *instrument*, *render* and *rendering* are this surface's
words, defined below. The decisions behind it are in the
[web build spec](../.scratch/web-build/spec.md).

## What the web build covers

The `web` CMake presets configure with Emscripten's toolchain and build every target the
web build defines, which leaves out the Windows-only and native-only ones below. The test
preset then runs every test the web build registers, under emsdk's own Node, which the
configure preset names as CMake's emulator:

- the kernel, node, authoring, MIDI-file and soundfont tests, graph persistence included;
  the soundfont tests' golden clips give the same bits under Node as natively;
- kernel purity and the soundfont node's portable-maths check
  (`soundfont_portable_maths`), which read the source;
- the render core's tests (`web_core_tests`), the [golden clips](#golden-clips) among them,
  and the module's smoke test (`web_smoke`). Its fixtures, its soundfont and the golden
  preview, are written by the build, with `web_smoke_fixture`, which the build runs under
  Node; the golden preview goes into the [check page](#checking-a-browser-by-hand)'s folder.

The render core, `modular_web_core`, is the C++ behind the module. It also builds and is
tested natively on Windows ([windows.md](windows.md)). The module, `modular_web`, its loader
and its declarations build only under Emscripten. The [`web` CI job](#continuous-integration)
runs all of this on every pull request that takes the build path.

### What it leaves out

Each exclusion is named, with its reason:

| Left out | Why |
| --- | --- |
| The graph editor, the Windows audio node and their tests | Windows only. |
| `midi_cli_smoke` | Its CMake script runs the CLI directly, with no emulator, so it is registered only natively. |
| `soundfont_extract` | A check of a native tool: its script runs a native helper. Registered only natively. |
| In `authoring_tests`, `a client can run and release a graph on a worker before editing and running again` and `a worker reports a processing boundary failure and releases execution` | The web build has no threads, so a graph cannot run on a worker: starting one throws `thread constructor failed: Not supported`. |
| On a Windows host only, in `node_tests`, `duplicate triggers and Stop ties preserve distinct numbered takes across blocks` and `a partial external write is cleaned and Retry preserves existing output`; in `authoring_tests`, `shared file save replaces complete graphs at Unicode paths and retains files on failure` and `recording graphs reopen with owned MIDI and settings but no transient take` | Each removes a directory, and Node on Windows cannot. The C library's `remove()` unlinks first and falls back to `rmdir` only when the unlink fails with `EISDIR`, but Node on Windows fails it with `EPERM`. So a directory the case expects gone stays, or the removal throws `Operation not permitted`. CI runs these cases on Linux, where they pass. |

The cases are left out with doctest's `--test-case-exclude`, which the root `CMakeLists.txt`
passes only under Emscripten, so the native suite runs every case. The table was measured
on the Linux runner and on a Windows host; on Linux only the two worker cases are left out.

### The test module

The smoke test also loads the *test module*, `modular_web_test`, a variant built only for it
into `build/web/web-test-module/`, never into `dist/`, which holds only the files the
manifest lists. It is built from the module's own sources, version and link options, and
the same loader, with two additions:

- [`tests/web_test_module.cpp`](../tests/web_test_module.cpp), in which any allocation of
  64 MiB or more traps and an export reports the heap in use;
- `tests/web_test_module.js`, which hands each instance to the test.

Through it the test induces a trap and reads the module's memory, so the module a client
takes carries neither.

### Build flags

Under Emscripten, the root `CMakeLists.txt` gives every target, at compile and link:

- `-fwasm-exceptions`: native WebAssembly exceptions, in the legacy encoding that 6.0.13
  uses by default. MIDI parsing and the nodes throw inside the module, and every exported
  function catches.
- `-O2` for Release, the level the WebAssembly probe measured, in place of CMake's `-O3`;
- `-ffp-contract=off`, as for every GCC and Clang build
  ([project-foundations.md](project-foundations.md)).

No threads, no fast maths and no relaxed SIMD: none is turned on. The module also links
with `-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1`. The
manifest records the module's flags as CMake passes them ([below](#version-and-manifest)).

The tests link differently from the module:

- every doctest executable links with `-sALLOW_MEMORY_GROWTH=1`, as the module does, since
  the soundfont tests need more than the default 16 MiB;
- those that read or write files link Node's raw file system (`-sNODERAWFS=1`), so they see
  the host's files as the native tests do: the node, authoring, MIDI-file and soundfont
  tests, and the smoke test's fixture writer.

### Golden clips

A browser's render equals the native one bit for bit, and golden values check it. Three
clips, each rendered through the render core at 44.1, 48 and 22.05 kHz, are pinned by
their frame count and a 64-bit hash of every 4,096 frames (`tests/GoldenRender.h`):

- **the timing tie:** a note every 40 ticks at `Q:1/4=90` through tick 15,000, which falls
  on exactly frame 1,000,000.5 at 48 kHz, then a tempo change;
- **the pool:** a dense passage through a pool of four voices, so notes are taken with the
  soundfont node's 6 ms fade;
- **the preview:** one note held 0.25 s at velocity 105, then its release.

The clips, their soundfont and the values live in `tests/WebGoldenClips.h`, written in
memory with the test-side soundfont builder and MIDI writer (`tests/MidiTestFiles.h`). `web_core_tests` checks every clip at every
rate, natively in Debug and Release and under Node here, against the same values, so the
module's render equals the native one by transitivity. The smoke test renders the preview
at 48 kHz through the loader and checks the same values with the hash ported to
JavaScript (`tests/web/golden_hash.mjs`); the build's `web_smoke_fixture` writes the
preview's soundfont, MIDI bytes and values at every rate into the
[check page](#checking-a-browser-by-hand)'s folder, which renders all three rates in a
browser. The soundfont node's own golden test, in `soundfont_tests`, runs here too.

**A change that alters a rendered clip updates the values in the same commit,** whether it
changes the render core or the soundfont node ([node-io.md](node-io.md)). On a mismatch
the test names each differing rate's frame count and first differing block, then prints
every rate's values to paste over the golden ones. The smoke test reads its values from
the same header, so one update covers both, and on a mismatch it prints its own.

## The surface

[`apps/web/modular.d.mts`](../apps/web/modular.d.mts) declares it:

```ts
export function loadModular(options?: {
  locateFile?: (path: string, prefix: string) => string;
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
  channels: [Float32Array<ArrayBuffer>, Float32Array<ArrayBuffer>]; // left, right
}
```

The channels are of equal length, which may be 0. They are typed `Float32Array<ArrayBuffer>`,
since each owns a plain `ArrayBuffer`, so a strict type check accepts them where Web Audio's
declarations want that type, as `AudioBuffer.copyToChannel` does. The generic form needs
TypeScript 5.7 or later.

A page uses it like this:

```js
import { loadModular } from './modular.mjs';

const modular = await loadModular();
const piano = await modular.createInstrument(soundfontBytes);
const { sampleRate, channels } = await piano.render(midiBytes, context.sampleRate);
```

- **`loadModular(options?)`** instantiates one module. A page loads it once and keeps it.
- **`version`** says what built the module: the commit, whether the tree was dirty, and the
  Emscripten version, as [Version and manifest](#version-and-manifest) describes.
- **`createInstrument(soundfont)`** parses a SoundFont 2 file once. The instrument plays
  the file's lowest preset (lowest bank, then lowest program) with the soundfont node's
  default of 256 voices ([node-io.md](node-io.md)). There are no options.
- **`render(midi, sampleRate)`** renders Standard MIDI File bytes at a sample rate, with no
  resampling and no gain: the samples are the soundfont node's own, so the level is the
  page's to set. The result is always two channels, left then right, of equal length.

**Copies.** Each call copies the bytes it is given when it is made, so a page may change,
reuse or transfer its array as soon as the call returns. An instrument keeps its copy of the
soundfont's bytes for its life. A rendering's channels are copied out of the module's
memory into arrays that JavaScript owns, so a later render, or the memory growing, never
changes them.

**The tail and the length.** A render runs two seconds past the file's end, then trims the
silent tail. A rendering's length is therefore:

- at least the file's own frames, its duration times the rate rounded to a whole frame,
  which are never cut, even when they end in silence;
- then the release, kept through its last frame where either channel's magnitude reaches
  `1e-4` (−80 dBFS);
- at most the file's frames plus two seconds of frames (`floor(2 × rate)`).

A file's duration runs to its end-of-track time. A file of zero duration renders zero
frames, and an `AudioBuffer` cannot be empty, so a page handles that case before it builds
one. A page that wants silence after the file pads the buffer itself.

**The length limit.** A file longer than ten minutes is refused with
`render: file is longer than 10 minutes`, read from the file's duration as soon as its bytes
are parsed, before anything is allocated for the rendering. A file of exactly ten minutes
renders. Ten minutes of stereo floats at 48 kHz take about 230 MB.

**Errors.** Every failure rejects with an `Error` whose message is Modular's, short and
lowercase. These are the messages a page can see:

- from the loader, which checks its arguments before the module sees them:
  - `soundfont: bytes must be a Uint8Array`, from `createInstrument`;
  - `midi file: bytes must be a Uint8Array` and `render: sample rate must be a number`, from
    `render`;
- from the soundfont reader, `soundfont: ...`: malformed bytes, such as
  `soundfont: not a soundfont` or `soundfont: truncated chunk` (empty bytes give this one),
  and `soundfont: compressed samples (sf3) are not supported` for a SoundFont 3 file;
- from the MIDI parser, `midi file: ...`, such as `midi file: missing MThd header`;
- from the render:
  - `render: sample rate must be finite and positive`, for 0, a negative rate, NaN or
    either infinity;
  - `render: file is longer than 10 minutes`, the length limit above;
  - `render: too many frames`, for a rate at which the rendering's frames could not be
    counted or held, such as a gigahertz rate;
  - any message one of the instrument's nodes reports while rendering. None reports one
    today;
- `out of memory`, when the module's memory cannot grow to hold a rendering. Its memory
  stops at 2 GiB;
- `module stopped: ...`, after a WebAssembly trap or abort, below.

No C++ exception reaches JavaScript. A failed call leaves the module and its instruments
usable, and the next call runs as usual.

**A trap.** The exported functions catch everything C++ throws, so an exception out of one
can only be a WebAssembly trap or an abort. Nothing in that instance of the module can be
trusted after one, so:

- the call that hit it rejects with an `Error` whose message is `module stopped: ` and the
  trap's own message, which also stays on the error as its `cause`;
- the loader discards the instance and never calls it again;
- the next call that needs the module instantiates a fresh one, with the same `locateFile`.
  The calls after it wait their turns as usual;
- an instrument made before the trap makes itself again on the fresh instance, at its next
  call, from its own copy of the soundfont's bytes, never from the page's array. It renders
  as it did before.

So a trap costs only the call that hit it. If the fresh instance cannot load, for example
when the `.wasm` cannot be fetched, that call rejects with the error, and the next call
tries again.

**Call order.** Every call returns a promise, even though a render runs on the calling
thread today, so moving the render into a Web Worker later changes no page code. One queue
per loaded module runs its calls one at a time, in the order they were made, whichever
instrument they name. A call that fails, its argument checks included, rejects in its own
turn and only its own promise, and the calls after it run as usual.

**Memory.** The module holds at most one rendering, which it releases as soon as the loader
has copied the channels out. WebAssembly memory grows as a render needs it and never
shrinks, so a long render keeps its peak for the module's life. In the smoke test, a minute's
rendering grew memory from 16.1 to 23.1 MiB. The test checks that no frames remain in the
module after each copy and that none of twenty repeat renders grows memory. The heap in use
dropped by 8 bytes on the second render and occasionally returned to the first count, as
the allocator counts a remainder too small to split as part of an allocated block. The
check applies after every render: the highest and lowest heap counts seen after copying
must be at most 8 bytes apart.

## Browsers and hosting

- **Browsers.** The module needs native WebAssembly exceptions in the legacy encoding:
  Chrome 95, Firefox 100 or Safari 15.2, or later. It also runs in Node.
- **No cross-origin isolation.** The module uses no threads and no `SharedArrayBuffer`, so a
  page needs no cross-origin isolation headers.
- **Content Security Policy.** A page whose policy sets `script-src` or `default-src` must
  allow `'wasm-unsafe-eval'` there, or the module cannot compile.
- **Caching and `locateFile`.** No file assumes a fixed URL. The loader imports the glue
  beside it, and the glue finds its `.wasm` beside itself, with
  `new URL('modular_web.wasm', import.meta.url)`. A bundler that serves every file under a
  content-hashed name follows both, so each file can be cached forever. When the `.wasm` is
  served from elsewhere, `locateFile` maps its name to the URL to fetch. A server should
  send the `.wasm` as `application/wasm` for streaming compilation; otherwise the glue falls
  back to compiling from a downloaded copy.

### Checking a browser by hand

Node is the golden check's runtime, since WebAssembly computes the same bits in every
browser and in Node, so no browser is checked on every change. The *check page* lets a
person confirm that once in a browser, such as Firefox or Safari, which the development
machine lacks. It renders the golden preview through the loader at 44.1, 48 and 22.05 kHz,
each on a fresh instrument, and shows each rate's frame count and block hashes beside the
[golden values](#golden-clips).

- **Where it is.** `tests/web/` holds the page, `index.html` and `check.mjs`, and
  `golden_hash.mjs`, the golden hash in JavaScript, which the smoke test shares. Configuring
  the `web` preset copies the page into `build/web/web-check/`, and building adds its
  fixtures, the golden preview's `preview.sf2`, `preview.mid` and `preview.json`, and a copy
  of the module's files in `modular/`. The [`web` CI job](#continuous-integration) uploads
  that folder as an artifact of its own, `modular-web-check-<commit>`, apart from the
  module's.
- **What it needs.** Any static server that sends `.mjs` files with a JavaScript type, and
  nothing else. Python's `http.server` maps them to one from Python 3.10. Opened from the
  file system, the page cannot run: Chrome refuses its modules, and the page then says it
  could not load.

The steps:

1. Take the folder of the commit to check, either:
   - from CI: on GitHub, open that commit's `CI` run, download the artifact
     `modular-web-check-<commit>` under **Artifacts**, and unzip it into an empty folder.
     GitHub's command line does the same:
     `gh run download <run id> --name modular-web-check-<commit> --dir <folder>`;
   - or from a local build: `build/web/web-check/`, after `cmake --build --preset web`.
2. In that folder, start a server, `python3 -m http.server 8000 --bind 127.0.0.1` (on
   Windows, `python`), and open `http://127.0.0.1:8000/` in the browser to check. For a
   browser on another device, such as Safari on an iPhone, serve with `--bind 0.0.0.0`
   instead and open `http://<the serving computer's local network address>:8000/` on the
   device; the folder is then open to the local network until the server stops. Stop it
   with Ctrl+C.
3. The page should say, in green, **Every hash matched, at 44100, 48000, 22050 Hz**, with
   every row marked ✓. Its golden column holds the preview's values in
   `tests/WebGoldenClips.h` at the folder's commit; when the page was added, the 48 kHz
   preview's block 0 was `0x7597ec3240543af8`. A mismatch shows in red with the first
   differing block. The page also shows the browser's user agent and the module's commit.

Record the browser and its version, the operating system, and whether every hash matched.

## Version and manifest

The build reads the checkout's commit each time it builds the module, not only when CMake
configures, and bakes three facts into the module. `loadModular()` returns them as
`version`:

- `commit`: the commit of the tree the module was built from, 40 hexadecimal digits;
- `dirty`: true when that tree had uncommitted changes to tracked files. Untracked files do
  not count, as for the soundfont extract tool, since the build names every source it
  compiles;
- `emscripten`: the Emscripten version that built it, such as `6.0.13`.

A build outside a Git checkout stops with a message, since the module could not say what
it was built from.

The build also writes `modular.manifest.json` beside the files a client takes, so a local
build and the CI artifact carry the same manifest. For example:

```json
{
  "commit": "<40 hexadecimal digits>",
  "dirty": false,
  "emscripten": "6.0.13",
  "flags": {
    "compile": "-O2 -DNDEBUG -std=gnu++20 -ffp-contract=off -fwasm-exceptions",
    "link": "-O2 -DNDEBUG -fwasm-exceptions -sMODULARIZE=1 -sEXPORT_ES6=1 ..."
  },
  "files": [
    { "name": "modular.d.mts", "size": 2123, "sha256": "<64 hexadecimal digits>" },
    ...
  ]
}
```

- `commit`, `dirty` and `emscripten` are the module's own `version`.
- `flags.compile` holds the flags every source in the module compiles with, and
  `flags.link` the module's link flags, as CMake passes them.
- `files` lists the declarations, the loader, the glue and the `.wasm`, by name, with each
  file's size in bytes and SHA-256.

The manifest holds no date and no path. `.gitattributes` checks the loader and its
declarations out with LF line endings on every host, so their hashes do not depend on Git's
`core.autocrlf`. The smoke test checks that `version` is well formed and that the manifest
matches it and the files beside it.

## Continuous integration

The `web` job in `.github/workflows/ci.yml` runs beside the Windows jobs on every pull
request and every push to `main` that takes the build path, and is skipped, as they are, on
a documentation-only change. **CI gate** needs it as it needs them
([windows.md](windows.md#continuous-integration)): a skipped or cancelled `web` job on the
build path fails the gate. On `ubuntu-24.04`, the job:

1. clones `emscripten-core/emsdk` at the tag `6.0.13`, outside the checkout so the tree
   stays clean, and checks that the tag is commit
   `35659964bc2762fffec0d257997fb61475f765b7`;
2. runs `emsdk install 6.0.13` and `emsdk activate 6.0.13`. Nothing is cached;
3. configures, builds and runs CTest with the `web` presets, each step in emsdk's
   environment (`emsdk_env.sh`), which sets the `EMSDK` and `EMSDK_NODE` the presets read;
4. shows the manifest in the run's summary;
5. uploads `build/web/dist/`, which holds only the four files and the manifest, as one
   workflow artifact, `modular-web-<commit>`, kept for GitHub's default retention;
6. uploads `build/web/web-check/`, the [check page](#checking-a-browser-by-hand) with its
   fixtures and a copy of the module, as a second artifact, `modular-web-check-<commit>`,
   so the module's artifact holds only what a client takes.

**The pin.** Emscripten 6.0.13 is pinned in one place in the workflow, the `web` job's
`EMSCRIPTEN_VERSION`, beside `EMSDK_COMMIT`, the commit its emsdk tag must resolve to, and
on this page. Moving to another version is one pull request that changes both values and
this page, and rechecks what the root `CMakeLists.txt` says of 6.0.13: that its default
exception encoding is the legacy one, which the browser floor above depends on.

**Which artifact a client takes.** A pull request's run builds GitHub's merge of the branch
into `main`, so its artifact, its manifest and its `version` name that merge commit, which no
branch keeps. A client takes the artifact of a push to `main`, whose commit is on `main`, and
keeps the manifest beside the files.

## Building it locally

From a clean `main` checkout, with emsdk 6.0.13, CMake 3.20 or later and Ninja on `PATH`.

1. Install and activate emsdk 6.0.13, once, anywhere outside the repository:

   ```sh
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install 6.0.13
   ./emsdk activate 6.0.13
   ```

   On Windows, run `emsdk.bat` with the same arguments.

2. In each new shell, set up its environment from the emsdk folder: `. ./emsdk_env.sh`, or
   in PowerShell `. .\emsdk_env.ps1`. The `web` presets read `EMSDK` for the toolchain file
   and `EMSDK_NODE` for the Node that runs the tests.

3. In the Modular checkout, confirm the tree is clean, note its commit, then configure,
   build and test:

   ```sh
   git status --porcelain   # prints nothing
   git rev-parse HEAD       # the commit the files are built from
   cmake --preset web
   cmake --build --preset web
   ctest --preset web
   ```

   The build goes into `build/web/`, which Git ignores.

4. A client takes the five files in `build/web/dist/` and keeps them together:

   | File | What it is |
   | --- | --- |
   | `modular.mjs` | The loader, which the page imports |
   | `modular.d.mts` | Its TypeScript declarations |
   | `modular_web.mjs` | Emscripten's JavaScript glue, which the loader imports |
   | `modular_web.wasm` | The module, which the glue fetches |
   | `modular.manifest.json` | What built them, and each file's size and SHA-256 |

   The manifest names the commit. `"dirty": true` there means the files were built from
   uncommitted changes, so they are not a `main` commit's files.
