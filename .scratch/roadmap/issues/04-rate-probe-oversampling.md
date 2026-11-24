# Probe: can a 4x-oversampled effect be expressed in today's model?

Type: prototype
Status: resolved

## Question

Sample rate is absent from the project's ledger entirely — not in Settled, not in Open.
Every port implicitly runs at one global rate. That makes it the most likely
catastrophic-class unknown, because rate touching the port type would revisit the
three-domain model itself.

This is a **spike, not a design**. Do not design multi-rate. Attempt the cheapest thing
that would expose the flaw, and report what happened:

- Try to express a 4x-oversampled distortion using only what exists today — upsample node,
  nonlinearity, downsample node, wired in a graph, rendered offline.
- Does it work? If it does, the claim "one global rate is sufficient" survives and rate
  drops down the ordering.
- If it doesn't, *why* — what precisely is missing? Does a port need to carry a rate? Does
  a node need to see a different `numFrames` than its neighbours? Does the block contract
  itself assume one rate for the whole graph?
- Related pressures to note but not solve: analysis at 1/64 rate, varispeed, and a plugin
  host changing rate while loaded (the guest case, which is last on the roadmap and so
  cannot be what retires this question).

Outcome is one of: no claim needed (rate is fine), a moderate claim (rate is expressible
but awkward), or a catastrophic-class claim that jumps to the head of the order. Any code
written here is throwaway; the finding is the deliverable.

## Answer

**Outcome: no claim needed.** One global rate survives the probe. A 4x-oversampled distortion
can be expressed in today's model with no kernel change, in two ways, and both render
sample-for-sample equal to a plain 4x reference at every block size tried. Rate leaves the
catastrophic class and drops down the ordering. Only the literal reading, rate as buffer
length, fails, and §1 says exactly where.

**Probe:** `.scratch/roadmap/prototypes/04-oversampling/`, run with `run.ps1` (Windows, MSVC). It
compiles the shipped `kernel/src/*.cpp` with the probe, and uses `GainNode`, `ConstFloatNode` and
`CaptureSinkNode` from `nodes/` unmodified. Every other node is defined in the probe, in user
space. The yardstick is a plain reference that oversamples the textbook way on whole arrays,
outside any graph: insert three zeros after each sample, apply a 257-tap lowpass at 4x, shape at
4x, apply the lowpass again, keep every 4th sample. A graph "expresses" 4x oversampling if its
render equals that reference sample for sample (`==`). The test tone is 5010 Hz at 48 kHz, and
renders use block sizes 1, 7, 64, 480 and 512, plus a random mix of sizes from 1 to 512.

```
Part 1  rate as buffer length
  block  64: upsampler needs  256 output frames, its view offers  64; next node told numFrames=64
  block 480: upsampler needs 1920 output frames, its view offers 480; next node told numFrames=480
Part 2  polyphase, memoryless tanh          16000/16000 == reference at every block size
  aliasing  1x -20.6 dB   4x graph -111.8 dB   4x reference -111.8 dB
Part 3  polyphase, stateful tanh
  phase-aware node                          16000/16000 == reference at every block size
  stock per-channel node                        2/16000 == reference, max |diff| 0.288,
                                            and its renders are identical at every block size
  polyphase audio<4> -> 4-channel sink: connect accepted; at-4x node prepared with 48000
Part 4  node owning a 4x Engine, stock node inside
                                            16000/16000 == reference at every block size
  inner node prepared with 192000; outer graph has 3 nodes
```

### 1. The literal reading fails, and where

An upsample node that emits 4N frames for an N-frame block has nowhere to put them. Its output
view is N frames long, and the node after it is told `numFrames() == N`. The ticket's three
questions, answered for this reading:

- **Does the block contract assume one rate for the whole graph?** Yes, in three places.
  `Engine::process` gives every node the same `numFrames` (`Engine.cpp:281`). Every audio output's
  storage is `maxBlockFrames` long (`Engine.cpp:214`). `PrepareInfo` carries one `sampleRate` for
  all nodes (`Engine.cpp:254`). A view's length comes from the block, not from the port.
- **Does a node need to see a different `numFrames` from its neighbours?** Only under this
  reading. To make it work, the kernel would need a frame count and a storage size per port,
  which means knowing a rate ratio per port. That is the catastrophic change the ticket feared,
  and §2 and §4 show oversampling does not need it.
- **Does a port need to carry a rate?** Not for oversampling. In §2 the rate lives in the
  channel count and in the two nodes that convert to and from it.

### 2. Polyphase: the 4x signal carried as four channels

`Up4` turns `audio<1>` into `audio<4>`, where channel *p* at frame *m* holds 4x-sample 4*m*+*p*.
`Down4` turns it back. Each keeps its filter history as node state. The chain is Sine → `Up4` →
the library's `GainNode(4)` for drive → `Tanh(4)` → `Down4` → sink. It equals the reference at
every block size, and it cuts aliasing from −20.6 dB to −111.8 dB, the reference's figure.

The block contract never sees the rate: every node still gets N frames at 48 kHz. The library's
`GainNode` runs on the 4x stream unmodified, because a memoryless node does not depend on rate.

### 3. What polyphase costs

- **A stateful node must be written for it.** A feedback saturator, y = tanh(2x + 0.5y), equals the
  reference when it keeps one state and steps through the four phases of each frame in time
  order. Written the way library nodes are, with one state per channel, it computes something
  else (max |diff| 0.288). Its wrong render is identical at every block size, so the repo's
  block-size invariance tests would not catch the error.
- **`prepare()` reports the base rate.** The at-4x node is prepared with 48000, not 192000. A node
  that computes coefficients from the sample rate must be told the factor some other way.
- **The type system cannot tell a polyphase stream from a bus.** The 4x stream connects to a
  4-channel sink, since `audio(4) == audio(4)`. This is ADR-0003's defect again: an 80-band
  spectrogram and an 80-channel bus also type identically. ADR-0003's fix covers this case as
  well. A polyphase stream becomes one more audio subtype, and by ADR-0003's rule the first
  oversampling node that ships defines it. No new mechanism is needed.
- **Events are timed in base frames.** An event's offset cannot address a single 4x sample. A
  distortion needs no events, so this is noted, not tested.

### 4. A node that owns a 4x Engine

`Oversampled4xNode` owns a private `Graph` and an `Engine` built at 4x `sampleRate` and 4x
`maxBlockFrames`. For each outer block it interpolates into the inner graph, runs the inner Engine
for 4N frames, then decimates. The stock per-channel saturator runs inside unmodified. It is
prepared with 192000, and the render equals the reference at every block size. This removes both
costs of the polyphase form.

Its own cost is that the inner graph is not part of the authored graph: the outer graph counts 3
nodes. So the compile does not flatten it, Save/Open would see only the outer node, the workspace
cannot edit it, and delay compensation sees only the node's `latencySamples()`. When oversampling
is a fixed technique inside one effect, that opacity costs nothing; it stays internal to the
effect. It matters only if a user must be able to *author* the subgraph that runs at 4x. No
current workflow or ticket asks for that.

**User ruling (2026-11-24): not now, so no claim.** If an editable oversampled section is ever
wanted, it becomes a moderate claim: a supernode whose inner graph runs at an integer multiple
of the outer rate, visible to compile and Save. Until then it waits in the map's fog, under
"Rate beyond oversampling".

### 5. One finding for delay compensation

The probe had to choose its filter length so that latency came out in whole base frames. With 257
taps, each filter delays 128 samples at 4x, which is 32 base frames. `latencySamples()` is a
`uint32` count of base frames (`Node.h:111`), so a section whose latency includes a fraction of a
base frame cannot report it; 255 taps would give 31.75. Engine-owned compensation can align an
at-rate section only when its total latency is a whole number of base frames. Any fraction has to
be absorbed inside the section. This belongs to the PDC item in the map's fog.

### 6. Class ruling

For oversampling, rate is **cheap**: user-space nodes in `nodes/`, with zero kernel impact. So
the ranking moves from catastrophic to no claim. This is the third catastrophic ranking in a row
that its probe did not bear out; tickets 11 and 12 each landed at moderate. Here the proxy was
that rate would touch the port type and so revisit the three-domain model. Oversampling never
needed rate in the port type.

At the user's request, the rule and the two constraints from §3 and §5 are recorded in the
kernel contract, `docs/project-foundations.md`.

### 7. Related pressures, noted and not probed

- **Analysis at 1/64 rate.** Two domains could carry it. Events are timed by offset, so they are
  block-size invariant by construction. Values are block-latched in v1 (`Buffers.h:105`): a value
  written every 64 frames is observed once per block, so what a reader sees depends on block size.
- **Varispeed.** The integer-ratio polyphase trick does not apply. A source that reads at a
  variable speed handles this itself; the file nodes already convert times to frames in
  `prepare()`. Varispeed of a *live* stream is different. Every node receives exactly `numFrames`
  of input per block, so a node that consumes input at a variable rate must buffer it, and its
  latency then varies with the signal. The kernel contract rules that out: "Latency is structural
  rather than signal-dependent" (`docs/project-foundations.md`). Of the three pressures, this is
  the one most likely to reopen the rate question.
- **A host changing rate while the engine is loaded.** Rebuilding the `Engine` runs `prepare()`
  again (research 01 §1.3), and both forms take their rate from `prepare()`. The nested node
  rebuilds its inner Engine at 4x the new rate. The probe's filter, however, is designed for a
  fixed 48 kHz; a real codec would design it in `prepare()`. The "Called once" comment on
  `Node::prepare` (`Node.h:113`) is still the contract defect research 01 found.

### The probe

The probe is kept in `.scratch/roadmap/prototypes/04-oversampling/`, as ticket 11's was, so the
evidence stays in history with the answer. It is outside the CMake build. `run.ps1` locates MSVC
with `vswhere`, compiles into a temporary directory and runs the probe in about five seconds.
