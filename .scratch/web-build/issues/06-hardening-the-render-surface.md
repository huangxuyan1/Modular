# 06: Hardening the render surface: the length limit, traps, every error path, memory and call order

**What to build:** Whatever a page hands Modular, the surface answers with samples or a clear
`Error`, and it never leaves the page without Modular.

- A file longer than ten minutes is refused before anything is allocated.
- A WebAssembly trap costs only the call that hit it: the next call reloads the module, and the
  page's instruments keep working.
- Every malformed input reaches JavaScript as an `Error` with Modular's message.
- Repeated renders do not grow memory.
- Calls made together run one at a time, in order, even when some fail.

See the [spec](../spec.md) (The surface; The render core) and map ticket 04,
[What does the page hand Modular, and what does it get back?](../../midishare-playback/issues/04-page-to-modular-contract.md)
(decisions 7, 10 and 12).

**Blocked by:** 03: The module and its loader: a page renders through `loadModular`.

**Status:** ready-for-agent

## Checklist

- [ ] The length limit:
  - A file whose duration exceeds ten minutes is refused with an explicit `render: …` error
    naming the limit, before the sink is sized.
  - The limit is one named default in the render core, with no midishare name.
  - A file just under it renders, and its output is at most the file plus the two-second cap.
  - Checked natively (seam S1). Under Node, the check covers only the refusal, to keep the run
    short.
- [ ] After a render the core checks `Engine::errorMessage()`, and a non-empty message fails the
  render with it.
- [ ] Error paths, each checked natively (S1) and through JavaScript (S2), each leaving the
  instrument and module usable:
  - malformed MIDI bytes;
  - malformed soundfont bytes, empty bytes, and a SoundFont 3 file;
  - sample rates of 0, a negative rate, NaN and ±Infinity;
  - running out of memory, which reaches JavaScript as an `Error` with an out-of-memory message.

  A file of zero duration is not an error: it returns two empty channels.
- [ ] The loader checks its arguments before calling the module. A soundfont or MIDI argument
  that is not a `Uint8Array`, or a rate that is not a number, rejects with an `Error`.
- [ ] Traps:
  - An exception escaping an exported call (a trap or an abort) rejects that call with an
    `Error` that says the module stopped and carries the original message.
  - The loader discards the instance, and the next call instantiates a fresh one.
  - Every instrument made before the trap re-creates itself on the fresh instance from its own
    JavaScript copy of the soundfont's bytes, never from the page's array.
  - The smoke test shows that a render after a trap equals the render before it.
  - The trap is induced by a means this ticket chooses that ships nothing extra in the module a
    client takes, for example a module variant built only for the test.
- [ ] Memory: after the first render of a clip, rendering it again many times does not grow the
  module's memory, and a rendering is released as soon as its channels are copied out. This is
  shown by a test-only means that ships nothing in the client's module.
- [ ] Call order:
  - A mix of `createInstrument` and `render` calls made without awaiting, with a failing call
    among them, settle in call order.
  - The failure rejects only its own promise.
  - Two instruments on one module share the one queue.
- [ ] `docs/web.md` lists the errors a page can see, the length limit, and what a trap does to the
  module and its instruments.
- [ ] Nothing changes under `kernel/` or `workspace/`. The full native suite passes
  (`docs/windows.md`), the `web` preset's tests pass, and `/code-review` runs on the branch's
  diff.
