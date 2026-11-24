# How is a browser render checked against a native one?

Type: grilling
Status: claimed
Blocked by: 02

## Question

Offline renders are Modular's primary verification path because they are comparable sample
for sample. A web build adds a second compiler and a second maths library, so the same graph
may not render the same bits in the browser as on Windows: `std::sin` and `std::pow` differ
between libraries, and floating-point contraction can differ between compilers.

Decide, using the probe's measured difference:

- Is the claim "a browser render equals the native render, sample for sample", or "equal
  within a stated tolerance"? The first is a stronger guarantee and may cost a portable maths
  routine in the nodes that need one.
- Where does the check run? An Emscripten build also runs under Node.js, so CTest could run the
  web build's render against a native reference without a browser.
- Is that check part of `CI gate`, and therefore part of the decision in
  [Where does the web build live, and how does a built module reach the site?](03-where-the-web-build-lives.md)?

## Comments

### Investigation pass, 2026-11-24

A ticket session investigated and recommended; nothing here is decided. The planning hub
decides each call under the judgment the user delegated for this map, except where a call is
marked **[user approval needed]**. Calls marked **[Modular]** are this repository's to decide.
Modular facts are at `origin/main` 0d0db07; the [soundfont node spec](../../soundfont-node/spec.md),
merged while this ran (28c95d6), and ticket 03's open planning PR are taken into account where
they bear on a call. The numbers come from a throwaway probe,
[`prototypes/08-libm-and-timing/`](../prototypes/08-libm-and-timing/results.md), which also
says how to rerun it: MSVC 19.43 with CMake's Release flags against Emscripten 6.0.13 in its
Node 24.19.0, on the development machine. No recommendation needs a change under `kernel/` or
`workspace/`; the changes it does need are listed at the end.

#### Facts

**Every maths-library call a SoundFont 2 voice could make differs between MSVC and
Emscripten's musl.** One million inputs per function over its working range, plus the format's
own integer inputs:

| Call | What a voice would use it for | Values that differ | By |
| --- | --- | --- | --- |
| `std::exp2` | cents to pitch ratio, timecents to seconds, LFO rate, cutoff in Hz | 38.2% (37.4% of integer cents) | up to 2 ulp |
| `std::pow(2, x)` | the same | 0.19% | 1 ulp |
| `std::pow(10, x)` | centibels to gain | 0.19% (1 of 1,441 integer centibels) | 1 ulp |
| `std::exp` | gain, if written as `exp` | 0.48% | 1 ulp |
| `std::log10` | the concave and convex velocity curves | 1.30% (4 of 127 velocities) | 1 ulp |
| `std::sin`, `std::cos` | equal-power pan, filter coefficients | 1.69%, 2.09% (23 and 30 of the 1,001 pan values) | 1 ulp |
| `std::tan` | a bilinear filter's prewarp | 3.99% | 1 ulp |
| `float` versions of `exp2`, `pow`, `sin` | the same, in float | 38.3%, 0.07%, 0.15% | 1 to 2 float ulp |

- **The cast to float hides a double's 1 or 2 ulp, almost always.** Not one of the about
  490,000 differing double values above rounds to a different float, which is why ticket 02's
  sine voice matched. It is not a guarantee: a value within 2 ulp of a float rounding boundary
  flips, a few in a billion. A node that keeps a library result in its own state, such as a
  filter's coefficients or an envelope's per-sample factor, carries the difference into every
  later sample. Float-precision library calls differ in the float result itself.
- **The native reference is not fixed either.** MSVC's runtime picks FMA3 implementations at
  startup when the CPU has FMA3. With them turned off on the same machine, its own `pow`,
  `exp`, `sin`, `cos` and `tan` change in 0.12% to 2.85% of values. Microsoft documents that
  results can differ "between computers that do or don't support FMA3"
  ([`_set_FMA3_enable`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/get-fma3-enable-set-fma3-enable)).
  Every current x64 CPU has FMA3, so in practice the development machine and a hosted runner
  take the same path.
- **Two routines written with only `+ - * /`, `floor` and `ldexp` agree bit for bit.** A
  portable `exp2` and `sin` (about 50 lines, a Taylor polynomial after range reduction) gave the
  same bits in all three runs, over 2 million values, and are within 1 ulp of musl's (MSVC's
  own `exp2` is up to 2 ulp from it). IEEE 754 requires those operations to be correctly
  rounded, so they cannot differ between runtimes. Cost per call: portable `exp2` 27 ns on MSVC
  and 6.3 ns in WebAssembly, against 34.5 ns and 4.1 ns for `std::exp2`; portable `sin` 8.9 and
  16.8 ns, against 7.0 and 11.1 ns.
- **Contraction cannot happen in either build today.** Visual Studio 2022 generates no
  floating-point contractions under its default `/fp:precise`
  ([`/fp`](https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior)),
  and x64's default `/arch` has no FMA instruction. Core WebAssembly has no scalar FMA either,
  so Clang cannot fuse there. It can on a native ARM64 build: Clang's default
  `-ffp-contract=on` fuses `a * b + c` within a statement where the target has FMA, as every
  ARM64 CPU does, so a native build on an Apple-silicon Mac may compute different bits. The
  root `CMakeLists.txt` sets no `/fp`, `-ffp-contract` or `/arch` flag.
- **`long double` already splits MIDI event frames.** `MidiFile::parse` sums event times in
  `long double` seconds and `MidiFileNode::prepare` rounds `seconds × rate` in `long double`;
  that type is 64-bit under MSVC and a 128-bit software float on wasm32. Replaying that code
  over every whole-number BPM from 30 to 300 at four rates, with an event every 40 ticks for ten
  minutes, put **9,881 of 21.5 million events on different frames** in the two builds, at 216
  of 271 tempos; 3,219 of them on sixteenth positions at 44.1 or 48 kHz. Every one sits on an
  exact tie (a frame of whole-plus-one-half), which the two precisions round to opposite
  sides. Example: `Q:1/4=90` at 48 kHz, tick 15,000 (bar 8, beat 4, second sixteenth) is
  exactly frame 1,000,000.5; MSVC places it at 1,000,001 and WebAssembly at 1,000,000. The same
  code in `double` agreed on all 107 million events tried. Ticket 02's four clips hit no split
  tie, so its renders matched.
- **`long double` also stops persistence compiling for WebAssembly.** Emscripten's libc++ has
  no `std::from_chars` for `long double`, which `PrimitivePersistence.cpp` uses to read stored
  MIDI times. Ticket 02 did not build persistence.
- **Modular's maths today.** The only transcendental calls in `kernel/`, `nodes/` and
  `workspace/` are `SineSynthNode`'s `std::pow` and `std::sin`, in double, cast to float. The
  kernel and workspace call none. `docs/project-foundations.md` asks for "floats for samples
  and doubles for phase/time math". Nothing sets flush-to-zero or denormals-are-zero, so the
  native offline render keeps subnormals, as WebAssembly always does.
- **WebAssembly's numbers are the same in every engine.** Its core specification fixes IEEE
  754 arithmetic with round-to-nearest and subnormals kept; only a NaN's sign and payload, and
  the relaxed-SIMD instructions, may vary by engine. Ticket 02's module imports no maths
  function from JavaScript (only `emscripten_resize_heap` and `_abort_js`), so musl runs
  inside the module. That is why Node, Chrome and Edge gave identical bits.
- **CTest already runs a WebAssembly build under Node, with no browser.** `emcmake cmake` on
  the unchanged `CMakeLists.txt` configures in 1.8 s and sets `CMAKE_CROSSCOMPILING_EMULATOR`
  to Emscripten's Node, so `add_test(NAME x COMMAND x)` runs as `node x.js`. Built from clean in
  9.7 s on 16 threads: `kernel_tests` passes 44 of 44 cases, `midi_file_tests` 15 of 15 and
  `node_tests` 14 of 16. The two failures are WAV-recorder file-system cases under Node's file
  system, not samples. `midi_cli_smoke` runs the `.js` directly and fails; `authoring_tests`
  cannot build (persistence, above). The run takes 0.9 s.
- **CI today.** `CI gate` waits for Windows Debug and Release jobs on `windows-2022`, which took
  47 s to 1 min 55 s in the last three code-path runs (one Release job: 14 s configure, 56 s
  build, 2 s CTest). Installing Emscripten took 35 s on the development machine, for 770 MB of
  downloads (ticket 01).

#### Judgment calls

**J1. The claim: bit for bit, by construction.** [Modular]

- *Options.*
  - (a) **Bit for bit, by construction.** For the same graph, soundfont bytes, MIDI bytes and
    sample rate, the module's render equals the native render in every float's bits and in
    length. The nodes on the web graph make it true by design (J2 to J4).
  - (b) Bit for bit, asserted, while the nodes keep calling the maths library. It holds only as
    long as the float cast keeps hiding the library's differences; a filter or envelope state
    can break it rarely and without warning.
  - (c) **Within a tolerance.** The strongest form worth having: the same length, allowing one
    frame where the −80 dBFS trim (ticket 04, decision 6) moves, and a largest sample difference
    of 2⁻¹⁵ (−90.3 dBFS, one 16-bit step). It must admit a voice whose end at the 96 dB floor
    (ticket 06, D1) moves by a frame: that sample is up to −96 dBFS for a full-scale zone with
    no attenuation (about −110 dBFS on the default piano, with its 140 cB). It still catches a
    wrong rate, a missing note (velocity 40 and the default piano's attenuation take a note only
    about 34 dB below its sample), a block seam, and a note one frame late (a one-frame shift
    changes a note's samples by about 2πf/rate of its level: 25 dB down at 440 Hz, 49 dB down
    at 27.5 Hz). It misses anything
    below −90 dBFS: a wrong release tail under that level, or quiet noise from uninitialised
    memory. It cannot use a hash as its reference (J5). And a voice that ends a frame later
    while the pool is full can turn a free voice into a taken note (ticket 06, D3), an audible
    difference that no tolerance bounds.
  - (d) An RMS bound averages a local fault, such as one click, away. (e) "Differs only where
    a voice's end frame moves" needs the render to report voice ends to the test, and the pool
    cascade above defeats it too.
- *Recommendation: (a).* What it costs is J2's two or three small routines and J3's type
  change. It turns the reference into a hash (J5) and catches anything that makes the two
  builds compute differently, whatever its level, uninitialised memory included. A failure is
  then always a real portability fault to remove, never a tolerance to widen. Every tolerance
  option still needs J3, since a split tie moves a note by a frame.
- *Scope.* The claim covers the graph the web build ships: `MidiFileNode`, the soundfont node
  and what the glue wires after them. `SineSynthNode` is outside it and stays as it is, since
  no web graph uses it. A node entering a web graph later takes on J2's rule.
- This says nothing about sounding right: that is ticket 09's D10 (the spec's equations, then
  FluidSynth).

**J2. The soundfont node computes no value that reaches its output with the maths library.**
[Modular], node policy under ticket 09's delegation

- *The rule.* On any value that reaches the output, whether computed in `process()`, at a
  note-on, in `prepare()` or in a table built at load, the node uses only operations IEEE 754
  makes exact or correctly rounded: `+ - * /`, `sqrt`, an explicit `fma`, conversions,
  `floor`, `ceil`, `trunc`, `round`, `fmod`, `ldexp`, `frexp`, `abs`, `copysign`. No `exp`,
  `exp2`, `pow`, `log`, `sin`, `cos`, `tan`, `tanh` or `hypot` from the library, in any
  precision.
- *What the node needs instead.* One portable `exp2` covers cents, timecents, LFO rates,
  cutoff, Q and centibels (`10^(−cB/200)` is `2^(−cB × log₂10 / 200)`, with the constant as a
  literal). A portable `sin`/`cos` covers equal-power pan and the filter's coefficients. The
  concave and convex curves need a `log`, or a 128-entry table built from the portable `exp2`
  or written as literals.
- *Tables are a speed choice, not a portability one.* A table filled at `prepare()` from
  `std::exp2` inherits MSVC's 38% of differing values. Filled from the portable routines, or
  written as literals, it is the same everywhere.
- *Against the soundfont node spec.* Its Further Notes say ticket 08's answer may require "a
  portable maths routine, and that change would be its own ticket". Its testing decisions
  already fit: "A change to the voice's maths that keeps the output within tolerance must not
  break a test", and the tolerances (±1 cent, one frame, 0.1 dB, 1 dB) are far wider than 1 or
  2 ulp. So the rule needs one new ticket on that spec, not a rewrite of it. Its ticket 01
  (pitch, interpolation, pan, the volume envelope) is the first to write these conversions,
  and a session already holds its branch. The new ticket is cheapest placed after 01 and
  before 02, which adds the filter, the modulation envelope and velocity; or 01's session can
  take the rule from the start, if the hub tells it in time.
- *Where.* A small header in `nodes/`, under a name the spec chooses. Its tests pin a few
  outputs' bit patterns natively, so a compiler change that alters them fails on Windows too,
  and check accuracy within 2 ulp of `std::` over a sweep. Optionally, a source check like
  `kernel_purity` fails if the node's files name a banned call.
- *Cost.* About 100 lines and a test file. Per call it costs about what the library does
  (Facts), and calls happen per note or per control update. The probe's `exp2` is untuned;
  setting the exponent bits directly instead of calling `ldexp` is the obvious first step.
- *Alternative.* Keep the library and take J1 (b) or (c). Not recommended, for J1's reasons.

**J3. MIDI times move from `long double` to `double`.** [Modular]; revisits ticket 02's
planning hub decision 2

- *Options.* (a) `double` in `MidiFile.h`, `MidiFile.cpp`, `MidiFileNode.h` and
  `PrimitivePersistence.cpp`: ticket 02's optional patch, extended to persistence. (b) Exact
  integer arithmetic from ticks to frames. (c) Leave it.
- *Recommendation: (a).* Ticket 02's decision 2 held the patch back because nothing needed it.
  Now something does. Under (c), any claim tighter than "sounds the same" fails on ordinary
  clips: a sixteenth at `Q:1/4=90` and 48 kHz can land a frame apart. Under MSVC, (a) changes
  nothing, since its `long double` already is a 64-bit double: every Windows render and every
  saved graph stays as it is, and persistence writes the same 17 digits. On the web it makes
  frames equal native, lets persistence compile, and saves 7.3 KB (ticket 02). (b) would round
  every tie correctly, but it moves 47,925 of the 21.5 million probe events by a frame on
  Windows today and changes how stored times are kept. It can come later on its own merits.
- *Against ticket 03's proposal.* Its open planning PR names persistence's `long double`
  (`std::from_chars`) as a pre-existing defect with its own fix ticket, and keeps persistence
  out of the web build until then. (a) is that fix, widened to the timing code that produces
  the stored value: one ticket, four files, not two tickets.

**J4. Compiler flags that keep the bits the same.** [Modular]

- MSVC keeps its default `/fp:precise`; the build never adds `/fp:fast` or `/fp:contract`.
- **The root `CMakeLists.txt` adds `-ffp-contract=off` for GCC and Clang.** It changes nothing
  on WebAssembly or on x86-64 without FMA. It makes a native build on ARM64, such as an
  Apple-silicon Mac (`docs/windows.md` mentions a Mac build), compute the same bits. It
  applies to every target, so it is a root build-file change, though not a `kernel/` source
  change.
- The web build never uses `-ffast-math` or `-mrelaxed-simd`, and the offline render never sets
  flush-to-zero. Emscripten's `-sJS_MATH` would put each browser's own maths library in the
  module's place; once J2 holds it would change nothing, and it is not wanted.

**J5. Where the check runs: CTest, under Node, against committed golden hashes.** [Modular]

- *Options for the reference.* (a) **Golden values in the test**: per clip and rate, the frame
  count and a 64-bit hash of each block of 4,096 frames, so a failure names the first differing
  block. (b) A native render made in the same CI run. (c) Committed golden sample files.
- *Recommendation: (a).* One test runs in both builds against one set of values: natively in
  the Windows CI jobs, Debug and Release, and under Node in the WebAssembly build. Each must
  match, so web equals native by transitivity, and neither job waits for the other. (b) needs
  both toolchains in one job, or a render handed from a Windows job to the web job, which puts
  the jobs in series. (c) costs megabytes and serves only a tolerance (J1 (c)).
- *The golden values change on purpose.* A change that alters a node's output updates them in
  the same commit, and the test prints the new values on a mismatch. A reviewer then sees every
  change to what Modular renders, a native regression guard the repository lacks today:
  `midi_cli_smoke` checks only a WAV's size.
- *What the test calls.* The C++ render function behind the module's export (ticket 04,
  decision 4), so the same code runs natively. A second, smaller Node test loads the built
  module through its JavaScript surface and renders one clip against the same hash, covering
  the copy out of memory, the channel order and the trim.

**J6. Which clips.** [Modular]

- *The soundfont.* The test writes its own, in memory, with the soundfont node spec's shared
  test-side builder: a few kilobytes of synthetic 16-bit samples. No soundfont file enters
  `tests/`, and the licensed piano never does. It sets every group of generators ticket 09's
  D1 honours: samples at two rates (neither the render rate), a root key away from the played
  notes, fine and scale tuning, loop modes 0, 1 and 3, both envelopes with key scaling, both
  LFOs to pitch, filter and volume, a filter with Q, pan at 0 and ±500, two velocity layers and
  an exclusive class. Each feature joins it as the node's ticket that adds it lands.
- *The clips:*
  1. overlapping chords whose releases end at the 96 dB floor, through every path above;
  2. timing: an event every 40 ticks at `Q:1/4=90` through tick 15,000 at 48 kHz, which splits
     under `long double` today, then a tempo change;
  3. the pool: a voice limit of 4 under a dense passage, so notes are taken with ticket 06's
     6 ms fade;
  4. the note preview (ticket 04, J8): one note.
- *Rates:* 44.1 and 48 kHz, the page's, plus 22.05 kHz. Each clip lasts a few seconds and
  renders in milliseconds.
- *The default piano* is checked once, as an acceptance step of the spec: the FluidR3 extract
  rendered by the module in Node and natively, hashes equal, with the file kept outside
  `tests/`.

**J7. Whether the check is part of `CI gate`: it follows ticket 03.**
[user approval needed] only as far as ticket 03's decision needs it

- *If the web is a supported platform with a CI job:* the check runs in that job, and `CI gate`
  waits for it, as it does for the Windows jobs. Estimated, not measured on a runner: a Linux
  job takes 1.5 to 2.5 minutes (Emscripten install about 35 s to a minute,
  cacheable; configure 2 s; build under a minute on a hosted runner; tests a few seconds). It
  runs beside the Windows jobs, so the gate waits about 0 to 1 minute longer, for about
  2 runner-minutes more per code-path run. Documentation-only PRs skip it, as they skip the
  Windows jobs. *Recommendation:* in the gate. It is cheap, and it is the only check that sees
  a portability break in the PR that makes it.
- *If the web build is a recipe:* `CI gate` keeps the native half, since the golden test runs
  in the Windows jobs. The web half runs in the recipe's build script, which refuses to produce
  a module whose Node check fails and records the Modular commit beside it. No CI minutes, but
  a break surfaces at the next module build, for whoever builds it. A manual, non-gating
  workflow could run it on demand. *Recommendation:* the script's check is mandatory, with no
  scheduled job.
- Either way it is the same test, so a recipe can become a CI job later by adding the job.
- *Where ticket 03 stands.* Its open planning PR proposes the first: a `web` job under
  `CI gate` on every PR that takes the build path, on a pinned `ubuntu-24.04` with emsdk
  6.0.13, with no cache at first. If it merges, the check is a test in that job, the gate
  needs no further change, and the uncached install is the job's largest cost.
- *Facts for ticket 03.* A supported build has to deal with what the CTest run above found:
  persistence's `long double` (J3), the two file-system cases in `node_tests`, and
  `midi_cli_smoke` calling the `.js` directly.

**J8. Browsers: Node is enough for the numbers.** [Modular] for the check; the manual checks
are the user's

- *Recommendation.* Node is the only runtime the check needs. WebAssembly's numbers are the
  same in every engine (Facts), the module brings its own maths, and ticket 02 saw Node, Chrome
  and Edge agree bit for bit. No browser needs a periodic numeric check.
- *What a browser check is for is the integration.* Does the module load (legacy WebAssembly
  exceptions need Firefox 100 or Safari 15.2), what rate does the `AudioContext` run at, and
  does the `AudioBuffer` path work? So once, at the spec's acceptance, Firefox and Safari each
  render the check clip in the page, which shows its hash; it must equal the golden value.
- *Who.* The user. The map already has Safari checked by hand once, on an Apple device. Firefox
  is not installed on the development machine, so either the user checks it, or installs it, a
  download that needs the user's approval.
- *When to repeat.* Only when the module's build flags change (exception encoding, SIMD) or
  Emscripten moves to a new major version.

#### What this would change

- **`kernel/`, `workspace/`:** nothing.
- **`nodes/`:** `long double` to `double` in `MidiFile.h`, `MidiFile.cpp`, `MidiFileNode.h` and
  `PrimitivePersistence.cpp` (J3); a portable maths header, used by the soundfont node under
  J2's rule, through a new ticket on its spec.
- **Root `CMakeLists.txt`:** `-ffp-contract=off` for GCC and Clang (J4), and the web build's
  configuration, which is ticket 03's.
- **`tests/`:** the golden render test and its in-memory soundfont writer, shared with the
  soundfont node's own tests (J5, J6).
- **`.github/workflows/ci.yml`:** a web job, only if ticket 03 makes the web a supported
  platform (J7).

#### For the planning hub

1. J3 reverses ticket 02's decision 2 ("not taken now"), on the new evidence of split ties, and
   merges with the persistence fix ticket that ticket 03's proposal names.
2. J7 follows
   [Where does the web build live, and how does a built module reach the site?](03-where-the-web-build-lives.md),
   whose open proposal makes the web a supported platform with a gated job.
3. J2 is time-sensitive. The soundfont node's ticket 01 is the first code to convert cents,
   timecents and pan, and its branch exists. Decided now, the rule costs a new ticket between
   01 and 02, or nothing if 01's session takes it from the start.
4. If the hub takes J1 (a), the two specs each carry their part: J2 and J6 in the soundfont
   node's (J6's soundfont comes from its shared test-side builder), and J3 to J5 and J7 in
   spec B, the web build's.
