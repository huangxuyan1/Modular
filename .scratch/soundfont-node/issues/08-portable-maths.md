# 08: The soundfont node renders bit-identically across compilers: portable maths

**What to build:** The soundfont node's output no longer depends on the compiler or the maths
library that built it. Every value that reaches its output is computed with operations IEEE 754
makes exact or correctly rounded, so a native build and a WebAssembly build render the same
bits. A golden test pins those bits natively; spec B runs the same test under Node. See the
[spec](../spec.md) and map ticket 08's answer,
[How is a browser render checked against a native one?](../../midishare-playback/issues/08-browser-render-checked-against-native.md)
(decisions 1 and 2).

**Blocked by:** 01: Tracer: a loaded soundfont plays notes at their pitch through the volume
envelope.

**Status:** done

**Why.** Map ticket 08's probe compared MSVC's runtime with Emscripten's musl:

- They disagree on `exp2` in 38% of doubles, by up to 2 ulp. On `pow`, `exp`, `log10`, `sin`,
  `cos` and `tan` they disagree in 0.2% to 4%, by 1 ulp.
- MSVC's own results change with the CPU's FMA3 support.
- A portable `exp2` and `sin` built from `+ - * /`, `floor` and `ldexp` agreed bit for bit
  across all three, within 1 ulp of musl's. They are in the probe's
  [`PROTOTYPE_libm_sweep.cpp`](../../midishare-playback/prototypes/08-libm-and-timing/PROTOTYPE_libm_sweep.cpp).

**Where the calls are.** Ticket 01 routes every maths-library call the voice makes through one
internal header of named functions, implemented with `std::` (the planning hub's instruction to
ticket 01's session, 2026-11-24). This ticket swaps their bodies in that one place. Tickets 02,
04 and 05 add calls through the same header:

- 02: the velocity curve and the filter;
- 04: the LFOs, filter Q, tuning and pitch modulation;
- 05: file modulators' curves.

A ticket that lands after this one uses the portable bodies at once; one that lands before is
converted by the swap. If ticket 01 landed without the header, this ticket first gathers the
calls into one.

## Checklist

- [x] The header's named functions have portable bodies: `exp2`, `sin` and `cos`, built only
  from operations IEEE 754 makes exact or correctly rounded:
  - `+ - * /`, `sqrt`, an explicit `fma` and conversions;
  - `floor`, `ceil`, `trunc`, `round` and `fmod`;
  - `ldexp`, `frexp`, `abs` and `copysign`.
- [x] The other conversions go through them:
  - centibels to gain as `2^(−cB × log₂10 / 200)`, with the constant a literal;
  - the concave and convex curves from `exp2`, or from a table.

  *Done another way, as recorded under Comments:* gains come from a portable `exp10`, which
  shares `exp2`'s series but reduces its argument by log₁₀2 in two parts, and the concave
  curve and a release's starting level come from a portable `log10`.
- [x] Nothing that reaches the output comes from the standard library's `exp`, `exp2`, `pow`,
  `log`, `sin`, `cos`, `tan`, `tanh` or `hypot`, in any precision. That holds whether the value
  is computed in `process()`, at a note-on, in `prepare()` or at load.
- [x] A table is allowed only if it is filled from the portable routines or written as
  literals. The node uses no `long double`.
- [x] Every existing tolerance test passes unchanged: ±1 cent, one frame, 0.1 dB and 1 dB, all
  far wider than the portable routines' 1 or 2 ulp.
- [x] A golden test renders a clip through every maths path the node has when this ticket
  lands, at 44.1, 48 and 22.05 kHz:
  - pitch away from the root key, from a sample whose rate is not the graph's;
  - the volume envelope;
  - pan at 0 and ±500;
  - whatever of 02, 04 and 05 has landed.

  Per rate, it checks the frame count and a 64-bit hash of each block of 4,096 frames, as
  values committed in the test. On a mismatch it prints the new values and the first differing
  block. Its soundfont comes from the spec's shared test-side builder.
- [x] The golden test is the spec's one deliberate exception to "A change to the voice's maths
  that keeps the output within tolerance must not break a test". Such a change updates the
  golden values in the same commit, so review sees every change to the node's rendered bits.
  When a later ticket adds a maths path, it extends the clip and updates the values.
- [x] The golden test passes natively in Debug and Release. Running it under Node joins with
  spec B, as do the timing, pool and preview clips (map ticket 08, decision 6).
- [x] Output stays bit-identical across block sizes, and `process()` allocates nothing.
- [x] `docs/node-io.md` says the node computes its output without the platform's maths library,
  and why.
- [x] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/soundfont-node-08-portable-maths`, cut from a
freshly fetched `origin/main` at `df4d78d`. Before the push it was rebased onto `00bfcac`
(web build 03, the module and its loader), which touches none of this ticket's files; the
rebase was clean.

- *What was built.*
  - `nodes/src/SoundFontMaths.h`: all five seam functions are portable, built from
    `+ - * /`, `floor`, `ldexp`, `frexp` and conversions only.
    - `exp2`: `x = n + f`, `e^(f ln 2)` by its Taylor series to degree 13, then `ldexp`.
    - `exp10`: `x = n log₁₀2 + r`, with log₁₀2 split into 32 bits and the rest, then
      `2^n e^(r ln 10)` through the same series.
    - `log10`: `frexp`, then `2 atanh s` by its series to s²³. `log10(1)` is exactly 0.
    - `sin` and `cos`: reduced by π/2 split as fdlibm's `pio2_1` and `pio2_1t`, then Taylor
      series to r¹⁷ and r¹⁸. `cos x` is `sin` one quarter turn on.
    - The node's call sites do not change. Its sources call no other transcendental: the
      only `std::` maths left are `floor`, `ceil`, `fmod`, `llround`, `isfinite`, `ldexp`
      and `frexp`. There is no `long double` and no table.
  - `tests/GoldenRender.h` defines the golden hash once, for web build 07 to reuse. It is
    FNV-1a 64-bit over each sample's IEEE bits, bytes least significant first, frames in
    order, left before right, started afresh for every 4,096 frames. `measure` computes a
    render's values. `difference` names the frame count and the first differing block.
    `source` prints the new values as C++ to paste.
  - The golden test, "a clip through every maths path renders its golden bits at three
    rates", in `tests/test_soundfont.cpp`:
    - Its soundfont comes from the shared builder. Its samples are made from integers only
      (a triangle plus noise from a linear congruential generator), so its bytes do not
      depend on a maths library. It has five zones and 30 note events, with a voice limit
      of 6.
    - It renders at 44.1, 48 and 22.05 kHz in blocks of 97 frames, and trims after the last
      frame either channel sounds on: 37,395, 40,702 and 18,699 frames, which give 10, 10
      and 5 block hashes.
    - Its coverage was confirmed with temporary counters in the node, since removed. Per
      rate, the clip has:
      - 8 notes taken, and 2 tails replaced within 6 ms;
      - releases starting in the delay, the attack, the hold, the decay (3) and the
        sustain (7), and 3 mode-3 loops released;
      - 2 voices ending at the floor while held, and 1 at its sample's end;
      - the 1440 cB ceiling once, and the filter's 0.45·rate clamp;
      - 4 note-offs spent on notes already gone.
  - `docs/node-io.md`: a **Portable maths** paragraph saying what the node does and why.
  - [`prototypes/08-portable-maths/`](../prototypes/08-portable-maths/PROTOTYPE_maths_probe.cpp):
    the throwaway probe behind the numbers below.
- *Red and green.*
  - The golden test first ran with no values. It failed, naming each rate's frame count and
    printing the render's values.
  - A golden hash corrupted by hand made it report "block 3 of 10 (frames 12288 on)
    differs", and was restored.
  - *Sensitivity:* biasing every `exp2` result by one ulp (`nextafter`, temporarily)
    changed 11 of the 25 block hashes.
  - The swap from `std::` to the portable routines changed none of this clip's bits: every
    1 or 2 ulp difference vanished in the float output. So the values committed are also
    `origin/main`'s.
  - The tolerance tests pass unchanged: 66 cases before, 67 with the golden test.
- *Debug against Release.*
  - **The cause.** MSVC's Debug and Release builds return different `std::exp2`, in 37 to
    38 % of values, by up to 2 ulp. They also return different `std::pow(10, x)`, in 0.17
    to 0.19 % of values, by 1 ulp. They agree on `sin`, `cos` and `log10`.
  - **Before.** Web build 04's helper renders the default piano's extract (outside the
    repository) at 44.1 kHz as `c303097e64329788` in Debug and `b8d8c8d538bdad31` in
    Release. At 48 kHz both give `89ee1204924aa20d`. This reproduces web build 04's finding
    on `df4d78d`.
  - **After.** Debug and Release both give `b8d8c8d538bdad31` at 44.1 kHz and
    `89ee1204924aa20d` at 48 kHz.
  - The seam's outputs agree bit for bit between Debug and Release on every probe set whose
    inputs agree. Three sets compute their inputs with `std::exp2`, so their inputs differ.
  - The golden test passes with the same values in both builds.
  - The golden clip did not split Debug from Release under `std::`. Nor did a 4.6 s
    variant, or one with the piano's slow decay. The split needs the piano's particulars, so
    the piano is the before-and-after evidence, and the golden test pins the after.
- *Native against Emscripten.* Emscripten 6.0.13 and its Node 24.19.0, configured by hand
  outside the repository: `emcmake cmake -G Ninja -DCMAKE_BUILD_TYPE=Release`, with
  `-fwasm-exceptions` and, at link, `-sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1`. On the rebased
  tree:
  - `soundfont_tests` runs under Node through CTest: 67 of 67 cases and 9,665 assertions.
    The golden values are the native ones.
  - The helper, built by hand with `em++ -O2 -ffp-contract=off`, renders the piano as
    `b8d8c8d538bdad31` and `89ee1204924aa20d`, equal to native.
  - With `origin/main`'s `std::` wrappers, WebAssembly matched MSVC Release on the piano,
    while MSVC Debug did not.
  - The seam's outputs equal MSVC's on every probe set whose inputs agree. Emscripten's own
    `std::` differs from MSVC Release in 0.03 to 1.7 % of values. Map ticket 08's probe
    measured 38 % for `exp2`. Its `cl` line has no `/MD`, so it linked the static runtime
    where CMake's builds link the DLL one. That may explain the gap; it was not checked.
- *Accuracy against `std::`* (MSVC Release), on the node's inputs and a million random
  values per function:

  | Routine | Values that differ | At most |
  | --- | --- | --- |
  | `exp2` | 10 % | 1 ulp |
  | `exp10` | 12 to 14 % (2.6 % of decay factors) | 1 ulp |
  | `log10` | 21 to 27 % | 3 ulp (1 on the velocity curve) |
  | `sin`, `cos` | 11 to 27 % | 1 ulp |

  Against MSVC Debug, `exp2` differs by up to 2 ulp, which is Debug's own error. The code
  review simulated the routines against 50-digit references: `exp2` within 1.2 ulp, `log10`
  within 2.6 ulp, and `exp10` within 1.4 ulp over |x| < 300.
- *Checks, on the final tree.*
  - Script focused checks: `-Target soundfont_tests -TestRegex '^soundfont_tests$'`, in
    Debug and Release.
  - Full suite through the script: Debug 11/11 and Release 11/11 CTest entries, with both
    `desktop` tests and kernel purity. `soundfont_tests` has 67 cases and 9,665 assertions.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
  - Nothing under `kernel/` or `workspace/` changed.
- *Judgement calls.*
  - **A portable `log10`, not a table.** The release's starting level is no table's
    argument, and J2 of map ticket 08 allows "a `log`". The concave curve uses it too.
  - **`exp10` by its own reduction.** The ticket's literal form, `exp2(x log₂10)`, loses up
    to `|x log₂10|` ulp to that product's rounding: 18 ulp on the node's gains, and 650 near
    |x| = 300. That contradicts the 1 or 2 ulp this checklist expects. The reduction uses
    only the listed operations and literal constants, and the golden values did not change.
  - **"Frame count"** is the trimmed length. The clip renders against a 2 s cap, and the
    test requires silence before the cap.
  - **No source check for banned calls.** J2 left it optional, and it would add a CTest
    entry. See the open notes.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at
  `30f3b83`, before the rebase, with a clean working tree. The fixes were checked again on
  the final tree.
  - **Standards.** There were no hard violations, and every coefficient and constant checked
    out.
    - Fixed:
      - *The accuracy claim was false for `exp10`:* `exp2(x log₂10)` reached 18 ulp, and
        650 at the edge of its domain. It now reduces by log₁₀2 in two parts: within 1 ulp
        of `std::` on the node's inputs.
      - `sin` and `cos` repeated one quadrant switch. `cos` now calls `sin`'s one quarter
        turn on.
      - The out-parameter `quadrant(x, k)` became `quarterTurns(x)`, which returns `{r, k}`.
      - `cosNear` gained its remainder bound. `exp10`'s comment names its uses.
      - `everyPath()` became `everyPathFont()`.
      - Two test lines over 90 columns were wrapped. The `node-io.md` Status paragraph was
        reflowed.
    - Kept: the golden test trims with its own loop over both channels, because
      `lastSounding` reads one channel and returns the size when it is silent.
  - **Spec.** It found nothing missing against the ticket, the spec's Testing Decisions and
    "Browser against native", map ticket 08's decisions, or web build 07's hash item.
    - Fixed:
      - Evidence missing from the ticket: it is recorded above.
      - On a frame-count mismatch, `difference` did not name a block. It now names the
        first differing block too.
      - The `exp10` claim, as under Standards.
      - The `log10` route was not recorded as a judgement call. It is now, above, with a
        note under the checklist.
    - Kept: the probe has no results file. Its numbers are here, and its header points
      here.

**Open notes for the planning hub.**

- *Guarding later tickets.* Tickets 04 and 05 add maths through this header. Nothing but
  review stops them from calling `std::` directly. A source check like `kernel_purity`
  could fail on a banned call in the node's files, if wanted.
- *Golden values for spec B.* Web build 07 reuses `tests/GoldenRender.h`, and runs this
  golden test under Node in its `web` job. `soundfont_tests` is not in the `web` preset's
  test filter yet.
