# Probe results: offline MIDI render as WebAssembly

PROTOTYPE, throwaway. The measurements behind
[Probe: does the offline MIDI render run in a page as WebAssembly?](../../issues/02-probe-offline-render-in-a-page.md),
taken on 2026-11-24 on the Windows development machine, at `origin/main` 86a8d12. The answer
and its recommendations are in that ticket; this file holds the numbers and how to rerun them.

## How to rerun

From the repository root, with Emscripten 6.0.13 installed as ticket 01 records, and an output
folder outside the repository:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1 -Configuration Release -Target midi_cli -TestRegex '^midi_cli_smoke$'
powershell -NoProfile -ExecutionPolicy Bypass -File .scratch/midishare-playback/prototypes/02-wasm-render/build.ps1 -Out <out>
. "$env:USERPROFILE\emsdk\emsdk_env.ps1"
& $env:EMSDK_NODE .scratch/midishare-playback/prototypes/02-wasm-render/node_run.mjs <out>
python .scratch/midishare-playback/prototypes/02-wasm-render/browsers.py <out>
```

`build.ps1 -Patch` builds from a patched copy instead (see the optional patch below).

| File | What it is |
| --- | --- |
| `PROTOTYPE_make_clips.py` | Writes the test clips and the ABC text that sizes them. |
| `PROTOTYPE_render.cpp` | `apps/midi_cli`'s graph behind exported C functions; also the native reference `render_native`. |
| `PROTOTYPE_libm.cpp` | The sine voice's maths alone, to explain the comparison. |
| `build.ps1` | Builds ten module variants with `em++`, and the native reference with MSVC's Release flags. |
| `node_run.mjs` | Native references, then each variant in fresh Node processes. |
| `index.html`, `page.mjs`, `browsers.py` | The page, and headless Chrome and Edge runs with fresh profiles. |
| `patches/optional-long-double-to-double.patch` | Not needed to compile; measured below. |

All modules: `em++ -std=c++20 -DNDEBUG -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node
-sALLOW_MEMORY_GROWTH=1`, compiling `kernel/src/*.cpp`, `nodes/src/MidiFile.cpp` and the
header-only nodes as they are.

## Clips

| Clip | Assumption | Notes | Length | Most at once |
| --- | --- | --- | --- | --- |
| worst | 4,997 characters of two-voice ABC in midishare's import shape (`L:1/8`, `M:4/4`, `Q:1/4=90`): eighths, sixteenth pairs, dyads, ties over bar lines, chords; every 16th bar a six-note chord under melody triads | 1,644 | 261.3 s (98 bars) | 9 (the 8-voice allocator steals) |
| long | Whole-note triads, the fewest characters per second: 4,994 characters hold 522 bars, 1,392 s. The MIDI keeps the 224 bars under `midi_cli`'s ten-minute limit | 672 | 597.3 s | 3 |
| short | midishare's example tune, its chord symbols as bass and chord | 48 | 10.7 s | 4 |
| preview | One eighth note at 120 BPM, as note preview renders | 1 | 0.25 s | 1 |

The MIDI files are format 1, 480 ticks per quarter, written from the same note list as the
ABC, not by abcjs.

## Sizes (bytes)

`-O2` unless named. Compressed with gzip level 9 and Brotli quality 11.

| Variant | Flags | .wasm raw | gzip | Brotli | .mjs raw | gzip | Brotli |
| --- | --- | --- | --- | --- | --- | --- | --- |
| noexc | (none: catching disabled) | 79,104 | 37,329 | 31,837 | 8,621 | 2,988 | 2,713 |
| jsexc | `-fexceptions` | 98,856 | 42,795 | 36,014 | 13,876 | 3,805 | 3,475 |
| wasmexc | `-fwasm-exceptions` (legacy encoding, the default) | 90,794 | 41,594 | 35,199 | 7,668 | 2,701 | 2,456 |
| exnref | `-fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0` | 92,093 | 42,112 | 35,635 | 7,668 | 2,701 | 2,456 |
| jsexc-msg | jsexc + `getExceptionMessage` exported | 175,727 | 70,068 | 59,012 | 14,716 | 3,993 | 3,648 |
| wasmexc-msg | wasmexc + `getExceptionMessage` exported | 164,836 | 68,344 | 58,008 | 9,510 | 3,131 | 2,860 |
| noexc-Oz | | 68,768 | 33,664 | 29,185 | 7,582 | 2,658 | 2,414 |
| jsexc-Oz | | 87,503 | 38,646 | 33,034 | 12,658 | 3,431 | 3,125 |
| wasmexc-Oz | | 79,690 | 38,316 | 32,803 | 6,743 | 2,400 | 2,178 |
| exnref-Oz | | 80,985 | 38,573 | 32,983 | 6,743 | 2,400 | 2,178 |

Imports: noexc 3, wasmexc and exnref 2 (`emscripten_resize_heap`, `_abort_js`), jsexc 25 (17
`invoke_*` trampolines plus the C++ exception runtime in JavaScript). No file-system import in
any variant.

## Timings (milliseconds)

Instantiate: from the factory call to a ready module; in a browser that includes fetching the
glue and the `.wasm` from 127.0.0.1. Node: median of 5 fresh processes. Browsers: 3 fresh
headless processes, each with a new profile. Renders: one call of the exported function, Node
the median of 3, browsers one run. Native: `render_native`, MSVC with CMake's Release flags,
median of 3.

| Where | Variant | Instantiate | worst 44.1k | worst 48k | long 44.1k | long 48k | short 48k | preview 48k |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| native | | | 288 | 320 | 582 | 635 | 12 | 0.3 |
| Node 24.19 | noexc | 1.7 | 339 | 358 | 638 | 696 | 12.0 | 0.3 |
| Node 24.19 | jsexc | 1.9 | 339 | 357 | 641 | 704 | 12.9 | 0.5 |
| Node 24.19 | wasmexc | 1.7 | 340 | 363 | 646 | 706 | 12.0 | 0.3 |
| Node 24.19 | exnref | 1.7 | 332 | 358 | 643 | 711 | 12.0 | 0.3 |
| Chrome 157 | noexc | 6.5 | 353 | 361 | 674 | 695 | 13.6 | 0.3 |
| Chrome 157 | jsexc | 7.5 | 359 | 373 | 650 | 704 | 14.5 | 0.5 |
| Chrome 157 | wasmexc | 6.9 | 351 | 355 | 683 | 714 | 14.2 | 0.4 |
| Chrome 157 | exnref | 7.2 | 367 | 370 | 656 | 751 | 13.9 | 0.3 |
| Edge 158 | noexc | 7.4 | 382 | 389 | 722 | 761 | 14.5 | 0.3 |
| Edge 158 | jsexc | 10.7 | 362 | 381 | 664 | 708 | 13.4 | 0.5 |
| Edge 158 | wasmexc | 8.8 | 360 | 384 | 661 | 711 | 15.4 | 0.3 |
| Edge 158 | exnref | 9.9 | 359 | 383 | 663 | 714 | 13.7 | 0.5 |
| Claude desktop browser pane (Chromium 156) | wasmexc | 17 | 350 | 375 | 660 | 690 | 12.7 | 0.3 |

The first render in a fresh module (preview at 44.1 kHz) takes 2 to 3 ms; later ones are
warm. In Node, the `-Oz` variants render the worst and long clips 0 to 9% slower than `-O2`
(single runs; wasmexc-Oz: 342, 361, 672 and 719). Creating the worst clip's
`AudioBuffer` from the module's memory and starting an `AudioBufferSourceNode` took 4 to 8 ms,
with no error, in every browser run; the `AudioContext` ran at 48 kHz.

Linear memory after a render: 16 MiB (preview, short), 128 MiB (worst), 256 MiB (long). It
never shrinks.

## Comparison with the native render

Every module render (4 clips, 44.1 and 48 kHz, every variant, in Node and in every browser)
equals the native float render bit for bit: 0 differing samples, maximum absolute difference
0. The native wrapper equals `midi_cli`'s WAV after `writeWav16`'s 16-bit conversion (0
differing of 12,544,007 worst-case samples), and so do the modules.

Why (`PROTOTYPE_libm.cpp`, 60 notes x 2 rates x 30 s): the pitch increments (`std::pow`, which
Clang turns into `exp2`) match bit for bit for all 120 runs. The double `std::sin` values
differ in all 120 runs: for notes 36, 69 and 95 at 48 kHz, 67,370 of 4,320,000 values (1.56%)
differ, each by 1 ulp. The cast to float hides every one: 0 float outputs differ.

## Optional patch: `long double` to `double`

`MidiFile` and `MidiFileNode` use `long double`, which MSVC makes 64-bit and wasm32 makes a
128-bit software float (compiler-rt `__addtf3`, `__multf3`, `__divtf3`, `__extenddftf2`,
`__fixunstfdi`, `__floatunditf`, `__floatunsitf`, `__getf2`, and musl `roundl`). It compiles and
runs at parse and prepare time only. The patch replaces it with `double` in those three files:
wasmexc shrinks by 7,306 bytes raw and 2,948 Brotli (90,794 to 83,488 raw), every render keeps
the same hash, and the soft-float routines leave the module.
