# Probe results: library maths, MIDI frame times and CTest under Node

PROTOTYPE, throwaway. The measurements behind the comments on
[How is a browser render checked against a native one?](../../issues/08-browser-render-checked-against-native.md),
taken on 2026-11-24 on the Windows development machine (an x64 CPU with FMA3), at
`origin/main` 0d0db07. Emscripten 6.0.13 with its bundled Node 24.19.0; MSVC 19.43 (Visual
Studio 2022) with CMake's Release flags. The comments hold the conclusions; this file holds the
numbers and how to rerun them.

## How to rerun

From the repository root, with an output folder outside the repository:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .scratch/midishare-playback/prototypes/08-libm-and-timing/build_and_run.ps1 -Out <out>
```

It builds both probes with `em++ -O2` and with `cl /O2`, runs them (the sweep twice natively,
the second time with the CRT's FMA3 paths off), and runs `compare.py`. About 40 seconds.

| File | What it is |
| --- | --- |
| `PROTOTYPE_libm_sweep.cpp` | Calls each library function a SoundFont 2 voice could use on fixed inputs, and two portable routines written with `+ - * /`, `floor` and `ldexp` only |
| `PROTOTYPE_frames.cpp` | `MidiFile::parse`'s time sum and `MidiFileNode::prepare`'s rounding, as written, in `long double` and in `double` |
| `build_and_run.ps1` | Builds and runs both ways |
| `compare.py` | Compares value by value and event by event |

## Library calls: MSVC against Emscripten's musl

Inputs: integer cents and timecents from −12,000 to 12,000 (as `x/1200`), centibels 0 to 1,440,
the concave velocity curve's argument for velocities 1 to 127, the 1,001 SoundFont pan angles,
and 1,000,000 seeded uniform values per function over its working range. Both builds read the
same inputs (equal input hashes). "Float casts differ" counts values whose `float` rounding
differs.

| Call | Values | Differ | % | Max | Float casts differ |
| --- | --- | --- | --- | --- | --- |
| `std::exp2`, integer cents | 24,001 | 8,975 | 37.4 | 2 ulp | 0 |
| `std::exp2`, random | 1,000,000 | 382,044 | 38.2 | 2 ulp | 0 |
| `std::pow(2, x)`, random | 1,000,000 | 1,888 | 0.19 | 1 ulp | 0 |
| `std::pow(10, x)`, integer centibels | 1,441 | 1 | 0.07 | 1 ulp | 0 |
| `std::pow(10, x)`, random | 1,000,000 | 1,902 | 0.19 | 1 ulp | 0 |
| `std::exp`, random | 1,000,000 | 4,802 | 0.48 | 1 ulp | 0 |
| `std::log10`, velocity curve | 127 | 4 | 3.15 | 1 ulp | 0 |
| `std::log10`, random | 1,000,000 | 12,972 | 1.30 | 1 ulp | 0 |
| `std::sin`, pan angles | 1,001 | 23 | 2.30 | 1 ulp | 0 |
| `std::cos`, pan angles | 1,001 | 30 | 3.00 | 1 ulp | 0 |
| `std::sin`, 0 to π | 1,000,000 | 16,863 | 1.69 | 1 ulp | 0 |
| `std::cos`, 0 to π | 1,000,000 | 20,889 | 2.09 | 1 ulp | 0 |
| `std::tan`, 0 to π/2 | 1,000,000 | 39,943 | 3.99 | 1 ulp | 0 |
| `std::exp2(float)` | 1,000,000 | 383,308 | 38.3 | 2 float ulp | 383,308 |
| `std::pow(10.0f, float)` | 1,000,000 | 658 | 0.07 | 1 float ulp | 658 |
| `std::sin(float)` | 1,000,000 | 1,484 | 0.15 | 1 float ulp | 1,484 |
| portable `exp2`, both input sets | 1,024,001 | 0 | 0 | | 0 |
| portable `sin`, both input sets | 1,001,001 | 0 | 0 | | 0 |

Clang turns `pow(2, x)` into `exp2(x)`, so the WebAssembly build's two rows are the same
function. MSVC's `exp2` is the outlier: 38% of its values differ from musl's, by up to 2 ulp,
where its own `pow(2, x)` differs in 0.19%.

**MSVC against itself.** With the CRT's FMA3 code paths turned off (`_set_FMA3_enable(0)`),
MSVC's own results change: `pow` in 0.12% of values, `exp` 0.15%, `sin` 1.13%, `cos` 1.55%,
`tan` 2.85%, each by 1 ulp (`exp2`, `log10` and the float functions did not change). Microsoft
documents that the CRT picks the FMA3 implementations at startup when the CPU has FMA3, and
that results can differ "between computers that do or don't support FMA3"
([`_set_FMA3_enable`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/get-fma3-enable-set-fma3-enable)).

**The portable routines** agree bit for bit across all three runs. Against musl they are within
1 ulp: `exp2` is 1 ulp off in 10.5% of the random values, `sin` in 20.4%.

**Cost per call** (ns, 1,000,000-value sets; the small sets are dominated by warm-up):

| Call | MSVC | MSVC, no FMA3 | WebAssembly in Node |
| --- | --- | --- | --- |
| `std::exp2` | 34.5 | 36.4 | 4.1 |
| `std::pow(2, x)` | 11.1 | 14.2 | 4.0 |
| portable `exp2` | 27.3 | 27.1 | 6.3 |
| `std::sin` | 7.0 | 7.7 | 11.1 |
| portable `sin` | 8.9 | 8.4 | 16.8 |

The portable routines are untuned; for `exp2`, setting the exponent bits directly instead of
calling `ldexp` is the obvious first step. Where this probe spends its MSVC time was not
measured.

## MIDI event frames: `long double` against `double`

`MidiFile::parse` sums `Δtick × tempo / (division × 10⁶)` in `long double` seconds, and
`MidiFileNode::prepare` takes `std::round(seconds × rate)` in `long double`. `sizeof(long
double)` is 8 under MSVC and 16 on wasm32. The probe runs that code for every tempo of a
whole-number quarter-note BPM from 30 to 300 (tempo in whole microseconds, as abcjs writes),
480 ticks per quarter, at 22,050, 44,100, 48,000 and 96,000 Hz: one event every 40 ticks for
ten minutes, and one every tick for one minute.

| Type | Grid | Events | On different frames, MSVC against WebAssembly | Of which exact ties | Tempos |
| --- | --- | --- | --- | --- | --- |
| `long double` | 40 ticks, 10 min | 21,462,680 | 9,881 | 9,881 | 216 of 271 |
| `long double` | 1 tick, 1 min | 85,852,280 | 19,659 | 19,638 | 245 of 271 |
| `double` | 40 ticks, 10 min | 21,462,680 | 0 | | |
| `double` | 1 tick, 1 min | 85,852,280 | 0 | | |

An exact tie is an event whose exact frame is a whole number plus one half; the two builds then
round the sum's last bits to opposite sides. Restricted to sixteenth positions at 44.1 and
48 kHz, the 40-tick grid still has 3,219 such events at 168 tempos. Examples, on the 40-tick
grid:

- `Q:1/4=90` at 48 kHz, tick 15,000 (bar 8, beat 4, second sixteenth): exactly frame
  1,000,000.5; MSVC 1,000,001, WebAssembly 1,000,000.
- `Q:1/4=116` at 48 kHz, tick 75,000: MSVC 3,879,308, WebAssembly 3,879,307.
- `Q:1/4=132` at 44.1 kHz, tick 480,000: MSVC 20,045,435, WebAssembly 20,045,434.

Whether a tie splits depends on the rounding the sum has gathered by then, so the same tick can
agree in one clip and split in another; ticket 02's four clips hit none. In `double`, both builds
compute the same bits by construction, and every one of 107 million events agreed. On MSVC the
change is no change at all: its `long double` already is a 64-bit double.

## CTest under Node, with no browser

The unchanged `CMakeLists.txt`, configured with
`emcmake cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-fwasm-exceptions
"-DCMAKE_EXE_LINKER_FLAGS=-fwasm-exceptions -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1"`, in a
build folder outside the repository:

- Configure: 1.8 s. `emcmake` sets `CMAKE_CROSSCOMPILING_EMULATOR` to Emscripten's Node, so
  every `add_test(NAME x COMMAND x)` runs as `node x.js` with no change to the test.
- A full build stops at `nodes/src/PrimitivePersistence.cpp`: Emscripten's libc++ has no
  `std::from_chars` for `long double` (`ContentReader::number<long double>`), so
  `authoring_tests` and every target linking persistence cannot build for WebAssembly as the
  source stands.
- `kernel_tests`, `node_tests`, `midi_file_tests`, `midi_cli` and `render_cli` build from clean
  in 9.7 s (16 threads).
- Under Node: `kernel_tests` 44 of 44 cases (15,001 assertions), `midi_file_tests` 15 of 15
  (1,887), `node_tests` 14 of 16 (3,321 of 3,327 assertions). The two failures are WAV recorder
  file-system cases ("duplicate triggers and Stop ties preserve distinct numbered takes across
  blocks", "a partial external write is cleaned and Retry preserves existing output"): the
  directory holds more files than expected under `NODERAWFS`. No assertion about samples
  failed. `kernel_purity` passes; `midi_cli_smoke` fails because its CMake script runs the
  `.js` file directly instead of through Node. The five tests take 0.9 s.
