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

**Status:** ready-for-agent

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

- [ ] The header's named functions have portable bodies: `exp2`, `sin` and `cos`, built only
  from operations IEEE 754 makes exact or correctly rounded:
  - `+ - * /`, `sqrt`, an explicit `fma` and conversions;
  - `floor`, `ceil`, `trunc`, `round` and `fmod`;
  - `ldexp`, `frexp`, `abs` and `copysign`.
- [ ] The other conversions go through them:
  - centibels to gain as `2^(−cB × log₂10 / 200)`, with the constant a literal;
  - the concave and convex curves from `exp2`, or from a table.
- [ ] Nothing that reaches the output comes from the standard library's `exp`, `exp2`, `pow`,
  `log`, `sin`, `cos`, `tan`, `tanh` or `hypot`, in any precision. That holds whether the value
  is computed in `process()`, at a note-on, in `prepare()` or at load.
- [ ] A table is allowed only if it is filled from the portable routines or written as
  literals. The node uses no `long double`.
- [ ] Every existing tolerance test passes unchanged: ±1 cent, one frame, 0.1 dB and 1 dB, all
  far wider than the portable routines' 1 or 2 ulp.
- [ ] A golden test renders a clip through every maths path the node has when this ticket
  lands, at 44.1, 48 and 22.05 kHz:
  - pitch away from the root key, from a sample whose rate is not the graph's;
  - the volume envelope;
  - pan at 0 and ±500;
  - whatever of 02, 04 and 05 has landed.

  Per rate, it checks the frame count and a 64-bit hash of each block of 4,096 frames, as
  values committed in the test. On a mismatch it prints the new values and the first differing
  block. Its soundfont comes from the spec's shared test-side builder.
- [ ] The golden test is the spec's one deliberate exception to "A change to the voice's maths
  that keeps the output within tolerance must not break a test". Such a change updates the
  golden values in the same commit, so review sees every change to the node's rendered bits.
  When a later ticket adds a maths path, it extends the clip and updates the values.
- [ ] The golden test passes natively in Debug and Release. Running it under Node joins with
  spec B, as do the timing, pool and preview clips (map ticket 08, decision 6).
- [ ] Output stays bit-identical across block sizes, and `process()` allocates nothing.
- [ ] `docs/node-io.md` says the node computes its output without the platform's maths library,
  and why.
- [ ] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.
