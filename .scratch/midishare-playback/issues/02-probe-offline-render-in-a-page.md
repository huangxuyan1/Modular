# Probe: does the offline MIDI render run in a page as WebAssembly?

Type: prototype
Status: resolved
Blocked by: 01

## Question

Every Modular build so far is native. This is a **spike**: try the cheapest thing that would
expose a web build's problems, and report numbers, not opinions.

- Build `modular_kernel`, the header-only nodes and `modular_midi` with Emscripten. Wrap
  `apps/midi_cli`'s graph (trigger → MIDI file → voice allocator → sine voices → mix → gain →
  capture) in one exported function that takes Standard MIDI File bytes and a sample rate and
  returns samples. The sine voice stands in for the soundfont node here and is never shipped.
- Call it from a plain local page, play the result through an `AudioBufferSourceNode`, and
  render a clip as long as midishare allows (5000 characters of ABC).
- Throwaway code under `.scratch/midishare-playback/prototypes/`, outside the build, as roadmap
  ticket 04's probe was. Nothing in the midishare repository is touched.

Report:

- which source files needed a change to compile, and why;
- the exception mode chosen (JavaScript-based or native WebAssembly exceptions), and whether an
  authoring error thrown inside the module arrives in JavaScript as a catchable error;
- module size, raw and compressed, with and without exceptions;
- time to instantiate the module, and time to render the longest clip, at 44.1 and 48 kHz;
- whether the browser's render equals the native `midi_cli` render of the same file, sample
  for sample, and if not, by how much it differs (feeds
  [How is a browser render checked against a native one?](08-browser-render-checked-against-native.md));
- whether `<filesystem>` or other unused library code ends up in the module;
- which browsers it ran in. midishare names no target browsers and has tested sound in
  Chromium only; the exception mode decides the oldest browser that can load the module.

Outcome: turns "the first WebAssembly build of a C++20 project usually needs fixes" from a risk
into a list, and gives the build-location and surface tickets their numbers.

## Answer

Resolved 2026-11-24. **Yes: the offline render runs in a page as WebAssembly, unchanged, at
1.1 to 1.25 times native time, and bit for bit equal to native.** The probe and every number are in
[the probe's results](../prototypes/02-wasm-render/results.md), under
`prototypes/02-wasm-render/`. Emscripten 6.0.13, `em++ -std=c++20 -O2`, Modular at
`origin/main` 86a8d12.

**What was built.** `kernel/src/*.cpp`, `nodes/src/MidiFile.cpp` and the header-only nodes,
behind one exported function that runs `apps/midi_cli`'s graph (trigger, MIDI file, voice
allocator, eight sine voices, mix, gain, capture) on Standard MIDI File bytes at a given sample
rate and leaves the samples in the module's memory. A plain page fetches a clip, renders it,
copies the samples into an `AudioBuffer` and starts an `AudioBufferSourceNode` (through a
silent gain). The same wrapper, compiled with MSVC's Release flags, is the native reference.

**Clips.** midishare was not run; the probe writes its own MIDI with a Python script. The
assumption for the worst case: 5000 characters of busy two-voice ABC in midishare's import
shape (`L:1/8`, `M:4/4`, `Q:1/4=90`) hold 98 bars: **1,644 notes, 261 s**, with chords,
sixteenths, ties, and climaxes where nine notes sound and the eight-voice allocator steals.
Duration has no ceiling in the character count: whole-note triads fit 522 bars, **23 minutes**,
in 4,994 characters, and a slower tempo stretches that further. So a second "long" clip keeps
the 597 s that fit under `midi_cli`'s ten-minute limit. Also a short ordinary clip (midishare's
example tune, 10.7 s) and a one-note preview (0.25 s).

- **Source changes needed to compile: none.** No file changed and no patch was needed. Clang
  (`-Wall`) adds two warnings that the MSVC build does not show: `VoiceAllocatorNode::release(note)`
  hides `Node::release()` (`-Woverloaded-virtual`, `MidiNodes.h`), and `Engine::maxBlockFrames_`
  is unused (`Engine.h`). One portability difference is real: `long double` is 64-bit under
  MSVC but a 128-bit software float on wasm32, in `MidiFile`, `MidiFileNode` and
  `PrimitivePersistence`. It runs only at parse and prepare time and changed no sample here.
  The committed `optional-long-double-to-double.patch` (not needed) makes it `double` in
  `MidiFile` and `MidiFileNode`. That saves 7.3 KB raw and 2.9 KB Brotli, and every render keeps
  its hash. Persistence writes `max_digits10` digits of it, so a graph saved by a web build would
  carry 36 digits where native writes 17. Persistence was not built here.
- **Exception mode: native WebAssembly exceptions, `-fwasm-exceptions`, in the legacy encoding
  (6.0.13's default).** Each exported function catches `std::exception` inside the module and
  returns a status and the message. With that, an authoring error (bad MIDI bytes, or a kernel
  `connect` type mismatch) reached JavaScript as a result with Modular's message, for example
  "midi file: missing MThd header", in all three exception modes. An error left uncaught is
  also catchable in JavaScript, but it is not an `Error`: under `-fexceptions` it is a
  `CppException`, under `-fwasm-exceptions` a `WebAssembly.Exception`. Its message needs
  `getExceptionMessage`, which links the C++ demangler: +74 to 77 KB raw, +23 KB Brotli. After
  either, the module rendered the next clip identically. With exceptions off (Emscripten's
  default), catch blocks never run: every authoring error aborts the module with a
  message-less `RuntimeError: Aborted(undefined)`, so that mode is unusable. Native exceptions
  over JavaScript-based ones: 14 KB smaller raw and 1.8 KB smaller in Brotli (module plus glue),
  2 imports instead of 25 (no `invoke_*` trampolines through JavaScript), and the same render
  speed. The cost is the oldest browser that loads the module, per Emscripten's own feature
  table:
  - JavaScript-based exceptions need only the toolchain's floor: Chrome 85, Firefox 79,
    Safari 15.0.
  - Legacy native exceptions: Chrome 95, Firefox 100, Safari 15.2, all from 2021 to 2022.
  - The newer exnref encoding: Chrome 137, Firefox 131, Safari 18.4. Moving to it later is one
    flag and 1.3 KB.
- **Sizes (bytes, raw / gzip -9 / Brotli 11),** `-O2`, `.wasm` then the `.mjs` glue:
  - no exceptions: 79,104 / 37,329 / 31,837, glue 8,621 / 2,988 / 2,713;
  - JavaScript-based: 98,856 / 42,795 / 36,014, glue 13,876 / 3,805 / 3,475;
  - native: 90,794 / 41,594 / 35,199, glue 7,668 / 2,701 / 2,456;
  - exnref: 92,093 / 42,112 / 35,635, glue the same as native.

  `-Oz` takes 10 to 11 KB raw (2.4 to 3.0 KB Brotli) off each and renders 0 to 9% slower. So
  the recommended module plus glue is **37.7 KB over the wire** with Brotli at `-O2`.
- **Timings (ms).** Instantiate: Node 1.7 to 1.9; headless Chrome 6.5 to 7.5 and Edge 7.4 to
  10.7, median of three fresh profiles, including the fetch from a local server. Render with
  native exceptions, one call each:

  | | worst 44.1 kHz | worst 48 kHz | long 44.1 kHz | long 48 kHz |
  | --- | --- | --- | --- | --- |
  | Native (MSVC) | 288 | 320 | 582 | 635 |
  | Node | 340 | 363 | 646 | 706 |
  | Chrome 157 | 351 | 355 | 683 | 714 |
  | Edge 158 | 360 | 384 | 661 | 711 |

  The module takes 1.1 to 1.25 times native time: 1.2 to 1.5 ms per second of audio at 48 kHz,
  700 to 850 times faster than real time. The short clip takes 12 to 15 ms, and a note preview
  0.3 ms (2 to 3 ms for a fresh module's first render). A render blocks the thread that calls
  it. Linear memory grows to 128 MiB for the worst clip and 256 MiB for ten minutes, and never
  shrinks: `CaptureSinkNode` grows its vector by appending.
- **Browser against native, sample for sample: equal.** Every render matched the native float
  render exactly: 0 differing samples and a maximum difference of 0, for 4 clips at 44.1 and
  48 kHz, every variant, in Node and in every browser. After the same 16-bit conversion, it also
  matched `midi_cli`'s WAV. The match is not by construction. `std::sin` differs between MSVC
  and Emscripten's musl in 1.56% of double values, each by 1 ulp, in a 4.32-million-value
  sample. `SineSynthNode` casts each value to float, which hid every one of them. The pitch
  increments match exactly. For
  [How is a browser render checked against a native one?](08-browser-render-checked-against-native.md):
  a node that works in float maths, or feeds a library result back into its own state, can
  differ.
- **Unused library code: none found.** No `std::filesystem`, `<fstream>`, iostream or locale
  code is in the module. The linker drops `MidiFile::read`, the glue carries no file-system
  library, and the native-exception module imports only `emscripten_resize_heap` and
  `_abort_js`. The one sizeable library piece the C++ pulls in is the 128-bit float routines
  that `long double` needs (above). The page needed no cross-origin isolation: no threads and no
  `SharedArrayBuffer`, served by a plain static server (for
  [Where does the web build live, and how does a built module reach the site?](03-where-the-web-build-lives.md)).
- **Browsers it ran in:**
  - Chrome 157 and Edge 158, headless, every variant;
  - the Claude desktop app's browser pane (Chromium 156, with an audio device), native
    exceptions only.

  In each, the `AudioContext` ran at 48 kHz, and creating the 261 s `AudioBuffer` and starting
  its source raised no error (4 to 8 ms). Firefox is not installed on the development machine,
  and Safari has no Windows build, so neither ran. Both engines support legacy native
  exceptions (Firefox 100, Safari 15.2), but that is untested here.

**Against the contract.**
[What does the page hand Modular, and what does it get back?](04-page-to-modular-contract.md)
was resolved while this probe ran. The probe's measurements support three of its decisions:

- Its error decision, J10, has the glue catch every `std::exception` at the boundary. That works
  in all three exception modes, needs no `getExceptionMessage` (a 23 KB Brotli saving), and
  cannot work with exceptions off.
- Its length cap, J7, is ten minutes, written into a preallocated sink. The probe's growing
  `CaptureSinkNode` reached 256 MiB of linear memory at ten minutes of mono, so the sink matters.
- Its threading decision, J12, renders on the main thread. The sine voice blocks it for 0.36 s
  on the worst clip and 0.7 s at the cap; the soundfont node will add its own cost.

One thing was not measured here: the one-note render with a soundfont loaded that its preview
decision, J8, asks for. No soundfont node exists yet. With the sine voice it takes 0.3 ms.

### Planning hub decisions (2026-11-24)

Decided by the planning hub under the judgment the user delegated for this map ("All, on your
best judgment").

1. **Exception mode: native WebAssembly exceptions** (`-fwasm-exceptions`, the legacy encoding
   Emscripten 6.0.13 uses by default), caught in C++ at every exported function and returned
   as a status and message. The oldest browsers it loads in are Chrome 95, Firefox 100 and
   Safari 15.2, well behind the map's "current desktop" browsers. It is smaller and simpler
   than JavaScript-based exceptions. Moving to exnref later is one flag.
2. **The `long double` patch is not taken now.** Nothing needs it, and every render was
   bit-exact without it. It goes to the web-build spec with one note: on wasm32, persistence
   writes `long double` values with 36 digits where native writes 17. That matters only if a
   saved graph ever crosses between the two builds, which the page contract does not do.
3. **The probe's numbers stand behind ticket 04's decisions 7, 10 and 12.**
   - A growing output vector reached 256 MiB of linear memory at the 10-minute cap, which
     justifies a preallocated or chunked sink.
   - Catching at the export boundary works in every mode.
   - A render on the main thread blocked for 0.36 s on the worst clip with the sine voice.
     The soundfont voice will cost more, so whether to render in a Web Worker is midishare's
     call, made with the soundfont node's timings when they exist.
