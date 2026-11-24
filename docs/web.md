# The web

The web is a supported Modular platform. Emscripten 6.0.13 builds a WebAssembly module from
`apps/web/`, and a hand-written loader gives a web page, or Node, one surface: hand in a
soundfont's bytes once, then render Standard MIDI File bytes to stereo samples, clip after
clip. The surface knows nothing of the page that calls it: no ABC, no player and no Web
Audio. The page is a *client*; *instrument*, *render* and *rendering* are this surface's
words, defined below. The decisions behind it are in the
[web build spec](../.scratch/web-build/spec.md).

## What the web build covers

The `web` CMake presets configure with Emscripten's toolchain, build every target and run
tests under Node, Emscripten's emulator for CMake. Today they cover:

- the render core, `modular_web_core`: the C++ behind the module, which also builds and is
  tested natively on Windows ([windows.md](windows.md));
- the module, `modular_web`, built only under Emscripten, and its loader and declarations;
- under Node, the render core's tests (`web_core_tests`) and the module's smoke test
  (`web_smoke`, with `web_smoke_fixture`, which writes its soundfont first).

Every other target builds under the `web` preset, but its tests do not run there yet. The
graph editor and the Windows audio node are Windows only, and the soundfont extract tool's
test is native only. There is no web CI job yet; the build is checked locally, as below.

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
smoke test's fixture writer links Node's raw file system (`-sNODERAWFS=1`) to write a file.

## The surface

[`apps/web/modular.d.mts`](../apps/web/modular.d.mts) declares it:

```ts
export function loadModular(options?: {
  locateFile?: (path: string, prefix: string) => string;
}): Promise<Modular>;

export interface Modular {
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

**Errors.** Every failure rejects with an `Error` whose message is Modular's, short and
lowercase, for example:

- `midi file: missing MThd header`, from the MIDI parser;
- `soundfont: compressed samples (sf3) are not supported`, from the soundfont reader;
- `render: sample rate must be finite and positive`, from the render itself;
- `out of memory`, when the module's memory cannot grow further.

No C++ exception reaches JavaScript. A failed call leaves the module and its instruments
usable, and the next call runs as usual. A WebAssembly trap or abort is not handled yet: it
rejects its call with the WebAssembly error, and the page should load a new module rather
than use that one again.

**Call order.** Every call returns a promise, even though a render runs on the calling
thread today, so moving the render into a Web Worker later changes no page code. One queue
per loaded module runs its calls one at a time, in the order they were made, whichever
instrument they name, and a call still runs after an earlier one failed.

**Memory.** The module holds at most one rendering, which it releases as soon as the loader
has copied the channels out. WebAssembly memory grows as a render needs it and never
shrinks, so a long render keeps its peak for the module's life.

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

4. A client takes the four files in `build/web/dist/` and keeps them together:

   | File | What it is |
   | --- | --- |
   | `modular.mjs` | The loader, which the page imports |
   | `modular.d.mts` | Its TypeScript declarations |
   | `modular_web.mjs` | Emscripten's JavaScript glue, which the loader imports |
   | `modular_web.wasm` | The module, which the glue fetches |

   Record the commit beside them. The module does not report it yet.
