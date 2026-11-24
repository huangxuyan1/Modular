# 07: Golden clips: the glue renders bit for bit natively and under Node

**What to build:** A reviewer sees every change to what Modular renders through its web surface,
and a portability fault fails CI in the pull request that makes it. Three clips are rendered
through the render core at three rates, and their frame counts and per-block hashes are pinned
in a test: a timing tie, a full voice pool and a one-note preview. The same test passes in the
Windows jobs and under Node in the `web` job, so the browser's render equals the native one by
transitivity. The Node smoke test checks the preview's hash through the JavaScript surface. See
the [spec](../spec.md) (Testing Decisions) and map ticket 08,
[How is a browser render checked against a native one?](../../midishare-playback/issues/08-browser-render-checked-against-native.md)
(decisions 1, 5, 6 and 7).

**Blocked by:**

- 05: The `web` CI job under `CI gate`, its artifact, the version and the manifest. The Node
  half runs there.
- Spec A's
  [03: When every voice is busy](../../soundfont-node/issues/03-when-every-voice-is-busy.md).
  The pool clip pins notes taken with the 6 ms fade.
- Spec A's [08: portable maths](../../soundfont-node/issues/08-portable-maths.md). Without it the
  node's output may differ between compilers, so the claim would hold only by luck.

**Status:** ready-for-agent

## Checklist

- [ ] The golden hash is defined once in a test header, and spec A 08's golden test uses the
  same one. If spec A 08 has landed, this ticket reuses its definition; otherwise it defines
  it there for spec A to share. The hash is 64 bits and computable exactly in JavaScript, for
  example FNV-1a over each sample's IEEE bits, frames in order, left before right.
- [ ] Three clips, written by the test, each with its soundfont built in memory by spec A's
  test-side builder:
  1. **Timing:** an event every 40 ticks at `Q:1/4=90` through tick 15,000, the case that split
     under `long double`, then a tempo change.
  2. **Pool:** a voice limit of 4, set through the render core's C++ parameter, under a dense
     passage, so notes are taken with the 6 ms fade.
  3. **Preview:** one note held 0.25 s at velocity 105, then its release.

  The soundfont exercises what the node does when this ticket lands: a stereo pair panned
  ±500, a root key away from the notes played, and a sample whose rate is not the graph's.
- [ ] Each clip renders through the render core (seam S1) at 44.1, 48 and 22.05 kHz.
  - The test checks the frame count and a hash of each 4,096-frame block, as values committed
    in it.
  - On a mismatch it prints the new values and the first differing block.
- [ ] The same test passes natively in Debug and Release and under Node in the `web` job, with
  the same values.
- [ ] The Node smoke test (seam S2) renders the preview clip through the loader at 48 kHz and
  checks the same frame count and block hashes. This covers copy-out, channel order and the
  trim. The fixture writer writes the preview's soundfont and MIDI bytes, so both sides render
  the same input.
- [ ] Spec A 08's golden test passes under Node in the `web` job, which is the Node half that
  spec A 08 left to this spec.
- [ ] Docs:
  - `docs/web.md` says a change that alters a rendered clip updates these values in the same
    commit.
  - A later spec A ticket that changes the node's output (02, 04 or 05, if any lands after this
    one) updates these values the same way. The tests' description in `docs/windows.md` is
    updated.
- [ ] Nothing changes under `kernel/` or `workspace/`. The full native suite and the `web` job
  pass, and `/code-review` runs on the branch's diff.
