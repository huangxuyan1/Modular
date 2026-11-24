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

**Status:** done

## Checklist

- [x] The length limit:
  - A file whose duration exceeds ten minutes is refused with an explicit `render: …` error
    naming the limit, before the sink is sized.
  - The limit is one named default in the render core, with no midishare name.
  - A file just under it renders, and its output is at most the file plus the two-second cap.
  - Checked natively (seam S1). Under Node, the check covers only the refusal, to keep the run
    short.
- [x] After a render the core checks `Engine::errorMessage()`, and a non-empty message fails the
  render with it. No node in the instrument's graph reports a message today, so no test reaches
  the check; see Comments.
- [x] Error paths, each checked natively (S1) and through JavaScript (S2), each leaving the
  instrument and module usable:
  - malformed MIDI bytes;
  - malformed soundfont bytes, empty bytes, and a SoundFont 3 file;
  - sample rates of 0, a negative rate, NaN and ±Infinity;
  - running out of memory, which reaches JavaScript as an `Error` with an out-of-memory message.

  A file of zero duration is not an error: it returns two empty channels.
- [x] The loader checks its arguments before calling the module. A soundfont or MIDI argument
  that is not a `Uint8Array`, or a rate that is not a number, rejects with an `Error`.
- [x] Traps:
  - An exception escaping an exported call (a trap or an abort) rejects that call with an
    `Error` that says the module stopped and carries the original message.
  - The loader discards the instance, and the next call instantiates a fresh one.
  - Every instrument made before the trap re-creates itself on the fresh instance from its own
    JavaScript copy of the soundfont's bytes, never from the page's array.
  - The smoke test shows that a render after a trap equals the render before it.
  - The trap is induced by a means this ticket chooses that ships nothing extra in the module a
    client takes, for example a module variant built only for the test.
- [x] Memory: after the first render of a clip, rendering it again many times does not grow the
  module's memory, and a rendering is released as soon as its channels are copied out. This is
  shown by a test-only means that ships nothing in the client's module.
- [x] Call order:
  - A mix of `createInstrument` and `render` calls made without awaiting, with a failing call
    among them, settle in call order.
  - The failure rejects only its own promise.
  - Two instruments on one module share the one queue.
- [x] `docs/web.md` lists the errors a page can see, the length limit, and what a trap does to the
  module and its instruments.
- [x] Nothing changes under `kernel/` or `workspace/`. The full native suite passes
  (`docs/windows.md`), the `web` preset's tests pass, and `/code-review` runs on the branch's
  diff.

## Comments

**2026-11-24: implemented.** Branch `claude/web-build-06-hardening-the-render-surface`, cut
from a freshly fetched `origin/main` at `00bfcac`. Before its first push it was rebased onto
`f57d9ce`, where soundfont node 08 had landed. No file overlapped.

- *What was built.*
  - **The render core** (`apps/web/Instrument.cpp`). After the import, and before any node
    prepares, `checkLength` reads `file().durationSeconds()`. It refuses:
    - a file over the limit, with `render: file is longer than 10 minutes`;
    - a rate at which the file and its two-second tail could not be counted, with
      `render: too many frames`.

    After the last block, a message from `Engine::errorMessage()` fails the render as a
    `std::runtime_error`. The limit is `kLongestFileMinutes = 10`, beside the tail's
    constants.
  - **The loader** (`apps/web/modular.mjs`):
    - **Argument checks.** It refuses a soundfont or MIDI argument that is not a
      `Uint8Array`, or a rate that is not a number. The messages are
      `soundfont: bytes must be a Uint8Array`, `midi file: bytes must be a Uint8Array` and
      `render: sample rate must be a number`. The check runs when the call is made, and the
      call rejects in its own turn.
    - **The instance.** Each instance of the module sits behind one object. Every export is
      called through `call`, which turns an exception into
      `module stopped: <the original message>`, keeps the original as `cause`, and stops the
      instance.
    - **A stopped instance** refuses every later call and lets go of its module at once.
    - **Reloading.** The next call that needs the module instantiates a fresh one inside the
      queue, with the same `locateFile`. An instrument whose handle belongs to a stopped
      instance makes itself again on its next call, from its own copy of the soundfont's
      bytes.
  - **The test module** (`modular_web_test`): `apps/web/Exports.cpp` and the render core
    again, with `tests/web_test_module.cpp` and `tests/web_test_module.js`.
    - Its `operator new` calls `__builtin_trap()` for any allocation of 64 MiB or more, a real
      wasm `unreachable`, which no `catch` can intercept.
    - An export reports `mallinfo().uordblks`.
    - A `--post-js` pushes each instance onto `globalThis.modularTestInstances`.
    - It builds into `build/web/web-test-module/` with a copy of the loader, never into
      `dist/`. The client's `.wasm` and glue contain neither the export nor the post-js.
    - Both modules link with one list, `MODULAR_WEB_LINK_OPTIONS`.
  - **Docs.**
    - `docs/web.md`: every error a page can see, adopting ticket 02's
      `render: too many frames`; the length limit; what a trap does; call order with
      failures; the memory numbers; and the test module.
    - `modular.d.mts` and `Instrument.h`: their comments, with no type changes.
  - Nothing under `kernel/` or `workspace/` changed. No midishare name, path or limit is in
    the code.
- *Choices made here.*
  - **"One named default"** is read as a named constant in one place. It is not an option:
    the surface offers none until a client needs one, as with the voice limit. Map 04, D7
    calls the ten minutes provisional, so moving it is a one-line change. Exactly ten minutes
    renders; a file only past it is refused.
  - **Uncountable rates are refused before any node prepares** (`checkLength`'s second
    clause).
    - On wasm32 a `std::vector<float>` holds at most 1,073,741,823 floats, fewer than
      `size_t` counts. A gigahertz rate therefore reached the page as libc++'s bare
      `length_error`, whose message is `vector`.
    - At 1e300 Hz, a zero-duration file reached the soundfont node's `llround` of its fade
      with a value no integer holds.
    - Both now give `render: too many frames`, so a zero-duration file at such a rate is
      refused rather than rendered empty.
    - `renderedFrames` keeps its exact check, since the node rounds the file's frames itself.
  - **The `Engine::errorMessage()` check has no test.**
    - Only `AudioOutputNode` overrides `errorMessage()`. None of the instrument's four nodes
      reports anything, so no input through the instrument reaches the check.
    - It throws `std::runtime_error`, not `std::invalid_argument`, because a failure while
      processing is not an authoring error. `midi_cli` throws the same way.
  - **Only a trap is induced, not an abort.** Emscripten's `abort()` in this glue calls the
    module's `__trap` once the runtime is up (`if(runtimeInitialized)___trap()`). An abort
    therefore reaches the loader as the same `WebAssembly.RuntimeError`, through the same
    `catch`.
  - **Instruments re-create themselves lazily, on their next call**, not all at once after a
    trap. An instrument never used again costs nothing. A failure while re-creating, a trap
    included, rejects that call, and the next one tries again.
  - **Argument failures settle in their turn.** The ticket asks every call made together to
    settle in call order, failures included. A bad argument is caught when the call is made,
    but it rejects only once the calls before it have settled.
  - **A `Uint8Array` from any realm is taken.** The check reads the typed arrays' own
    `toStringTag` getter, which names a typed array's kind and gives `undefined` for anything
    else. A plain `instanceof` would refuse an iframe's array.
  - **A fresh instance that cannot load** rejects the call that needed it, and the next call
    tries again. `docs/web.md` says so, and the smoke test checks it.
- *Tests.*
  - `web_core_tests` (S1), now 18 cases and 98 assertions, natively and under Node:
    - one tick past ten minutes is refused with the limit's message, at 48 kHz and at 1e19 Hz.
      This happens while no allocation over 1 MiB can succeed, so the refusal comes before the
      rendering is sized;
    - a file of exactly ten minutes, at 997 Hz, renders to the file's frames plus the cap,
      598,200 + 1,994, ending off any block boundary;
    - a render that runs out of memory throws `std::bad_alloc`, and the instrument renders on.
      The test's `operator new` fails anything over 1 MiB while a `MemoryCap` lives;
    - a zero-duration file at 1e300 Hz or `DBL_MAX` is refused as too many frames;
    - in the malformed-bytes case, empty soundfont bytes give `soundfont: truncated chunk`.
  - `web_smoke` (S2), now 12 cases. Six are new or extended:
    - calls across two instruments settle in call order, each failure rejecting only its
      own promise: a module failure, a bad argument and a bad soundfont, among renders and a
      `createInstrument`;
    - each malformed input rejects with its exact message, and the instrument and module
      work on:
      - `soundfont: not a soundfont` (a WAVE header);
      - `soundfont: truncated chunk` (empty bytes);
      - the sf3 message, for the fixture with its `ifil` major version set to 3;
      - `midi file: missing MThd header`;
      - the five bad rates;
      - `out of memory`, for a minute at 10 MHz: 2.5 GB a channel, past the 2 GiB maximum;
      - `render: too many frames`, at 1 GHz and at 1e300 Hz;
      - the ten-minute refusal, which is all that runs under Node;
    - a zero-duration file renders two empty `Float32Array`s;
    - the loader refuses an `ArrayBuffer`, an `Array`, a missing argument, a string rate, a
      `BigInt` rate and a missing rate. It takes a `Uint8Array` from a `vm` context;
    - the trap, on the test module, among seven calls made together:
      - the page's soundfont array is transferred away after the instrument is made;
      - the trap rejects with `module stopped: unreachable`, its `cause` a `RuntimeError`;
      - a bad argument and a bad MIDI file after it reject in their turns;
      - renders on both instruments, and a new instrument, equal the render before the trap;
      - exactly one fresh instance was loaded during the batch, with `locateFile` asked
        twice;
    - a fresh instance that cannot load (`locateFile` points at a missing `.wasm`) rejects
      that call, and the next call loads again;
    - memory, on the test module: see below.
- *Memory*, measured by the smoke test on a one-minute clip at 48 kHz, whose rendering is
  23,165,488 bytes:

  | When | Memory | Heap in use |
  | --- | --- | --- |
  | instrument made | 16,908,288 | 13,400 |
  | after the first render | 24,051,712 | 145,040 |
  | after 20 more renders | 24,051,712 | 145,040 |

  Memory grew once, from 16.1 to 22.9 MiB, then not at all. The 131,640 bytes the first
  render left in use are the kept MIDI file and the nodes' state, not the samples.
- *Red, then green.*
  - Native.
    - Red first: the limit case (it threw `bad allocation` at 48 kHz and
      `midi file: frame count overflow` at 1e19 Hz) and the uncountable-rate case (it did not
      throw).
    - Already passing: the exactly-ten-minutes, out-of-memory and empty-soundfont checks,
      which pin existing behaviour.
  - Smoke, against the old loader: the argument, call-order, trap and memory cases failed.
    The memory case failed only because its first, 30-second clip did not grow the memory. It
    now uses a minute.
  - Mutations of the built loader, each caught:
    - no queue;
    - the instance captured when the call is made;
    - no reload;
    - re-creation from the page's array;
    - no re-creation;
    - no release after copy-out;
    - argument failures rejecting at once;
    - the trap not wrapped, or not recorded;
    - each argument check dropped;
    - `instanceof` in place of the realm-independent check.
  - Removing `max_size()` from the frame limit turns the 1 GHz case's message into `vector`.
  - At first "no queue" survived. Every call had already taken the old instance before the
    trap, and the stopped instance went on working. Two changes catch it now:
    - the loader never calls a stopped instance;
    - the test checks the instance count right after the batch.
  - Not caught by any test: the stopped instance letting go of its module, which only the
    garbage collector would show. The call guard is defensive, so the queued flow never
    reaches it.
- *Checks, on the final tree.*
  - Native, through the script:
    - Debug 11/11 and Release 11/11 CTest entries, including both `desktop` tests, the
      extract tool's test and kernel purity;
    - doctest cases, the same in both: kernel 44, node 17, authoring 29, MIDI-file 18,
      soundfont 67, render core 18;
    - no compiler warnings.
  - `web`, under Emscripten 6.0.13 and emsdk's Node 24.19.0: the build has no warnings, and
    `ctest --preset web` passes 3/3.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
  - TypeScript 6.0.3, bundled with the local VS Code, so nothing was downloaded. A strict page
    gave 0 diagnostics under both `NodeNext` and `Bundler`. Its `@ts-expect-error` lines show
    that an `ArrayBuffer` soundfont and a string rate fail the type check.
  - Sizes: the loader is now 7,326 bytes raw and 2,642 gzipped, from 3,489 and 1,418. The
    `.wasm` on this tree, with soundfont node 08, is 99,561 bytes.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at `684ea3a`,
  before the rebase, with a clean working tree. The fixes were re-checked on the final tree.
  - Both axes: **an instrument kept the stopped instance reachable**, and with it the
    module's whole memory, until that instrument's next call. Fixed: the stopped instance
    lets go of its module at once.
  - Standards. Fixed:
    - the ten-minute render ended on a block boundary at 960 Hz; it now runs at 997 Hz;
    - a node's reported message is now thrown as `std::runtime_error`;
    - lines over 90 columns in the new code are wrapped;
    - the repeated check, queue and run shape in the loader is now one helper, `run`;
    - the smoke test's two failure loops are now one helper, `rejectWith`;
    - `largest` is now `allocationLimit`;
    - `mostFrames()` dropped a `std::min` that did nothing;
    - a `Uint8Array` from another realm is accepted again, as the old loader's copy did.
  - Standards, kept as judgement calls:
    - **Possible Duplicated Code.** `checkLength` and `renderedFrames` both refuse too many
      frames: one from the duration before the nodes prepare, the other exactly, after the
      node's rounding.
    - **Possible Duplicated Code.** Two tests repeat a two-line "renders as a fresh one
      would" check, as ticket 02's tests do.
    - **Possible Speculative Generality.** The `errorMessage()` check is the ticket's own
      requirement.
  - Spec. Fixed:
    - a zero-duration file is now checked through JavaScript too;
    - the failed reload that `docs/web.md` describes is now tested.
  - Spec, kept and recorded above:
    - the untested `errorMessage()` check;
    - the induced trap standing for an abort;
    - the uncountable-rate refusal, a choice made here;
    - the limit as a constant.
  - The planning hub accepted the first three as recorded.

**2026-11-24: `main` merged in.** After the first push, ticket 05 landed as `3c0aad4`. It
added the CI job, the version, the manifest and the LF pinning. It was merged into this
branch rather than rebased, since the branch was already pushed. Both sides were kept.

- *The test module.*
  - It builds from the module's own sources, `MODULAR_WEB_SOURCES`, which include
    `apps/web/Version.cpp`.
  - It waits on the same version stamp, includes its header, and links with
    `MODULAR_WEB_LINK_OPTIONS`.
  - So the loader's `version` works there too. The smoke test checks that the test module
    reports the same `version` as the shipped one.
  - It still builds into `web-test-module/`, so `dist/` holds exactly the manifest's five
    files. The manifest test checks that.
- *The loader* reads `version` once, from its first instance, through the same guarded
  calls. A fresh instance after a trap is the same build.
- *`docs/web.md`*: ticket 05's coverage and exclusions, with the test module under its own
  heading.
- *Checks on the merged tree.*
  - Native, through the script: Debug 11/11 and Release 11/11, both `desktop` tests
    included, with no compiler warnings.
  - `ctest --preset web`: 9/9. This ticket adds no CTest entry. Its cases are in
    `web_core_tests` and `web_smoke`, which now has 13 cases.
  - `check_docs`: 0 problems. `git diff --check`: clean.
