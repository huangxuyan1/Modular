# 08: Acceptance: the default piano in the module, the preview's browser timing, Firefox and Safari

**What to build:** Evidence that the shipped module plays the default piano bit for bit as the
native build does, and that a note preview is fast enough to judge where midishare renders. It
also brings the two browsers the development machine lacks into the check once, by hand. This
ticket changes no product code. A failure is a finding to fix in its own ticket, not here. See
the [spec](../spec.md) (Further Notes, Acceptance), map ticket 04's decision 8 and map ticket
08's decisions 6 and 8.

**Blocked by:**

- 04: The soundfont extract tool, and the default piano's extract;
- 07: Golden clips: the glue renders bit for bit natively and under Node;
- spec A's [02: The piano's tone](../../soundfont-node/issues/02-the-pianos-tone.md) and
  [03: When every voice is busy](../../soundfont-node/issues/03-when-every-voice-is-busy.md),
  so the piano sounds right: loops, filter, velocity and the full pool;
- the user's steps below.

**Status:** ready-for-agent

## Checklist

- [ ] **The default piano, module against native.** Use the extract from ticket 04, kept
  outside the repository. A throwaway harness under `.scratch/web-build/prototypes/`, outside
  the build, renders a clip covering the piano's key ranges and velocities at 44.1 and 48 kHz,
  twice:
  - natively, through the render core;
  - under Node, through the loader.

  Frame counts and per-block hashes are equal. Rendered audio and licensed files are never
  committed.
- [ ] **The preview's timing in a browser** (map ticket 04, decision 8). With the extract
  loaded, a local page times one-note renders in Chrome or Edge, headless, served by a local
  static server:
  - the first render after `createInstrument`;
  - the median of later renders.

  Also time `loadModular` and `createInstrument`, and record everything under Comments for
  midishare's choice between the main thread and a Web Worker.
- [ ] **The check page** (ruling G10):
  - It is committed with the tests and built into the build tree with its fixtures: the
    preview clip's soundfont and MIDI bytes.
  - It renders the clip through the loader and shows the frame count, each block's hash and
    whether they equal the golden values.
  - The `web` job uploads it, with its fixtures and the module, as a separate check artifact,
    not inside the module artifact midishare commits.
  - It needs any static server, and nothing else installed.
- [ ] **Firefox and Safari, by hand (the user).** The user opens the check page in current
  desktop Firefox and in Safari on an Apple device, and records the result here: browser,
  version, operating system, and whether every hash matched.
  - Installing Firefox on the development machine, if the user checks it there, is the user's
    own step.
  - How the user reaches the files (the check artifact or a local build) and serves them is
    agreed with the user through the planning hub before this item starts.
  - The agent checks the same page headless in Chrome or Edge first.
- [ ] Any mismatch or failure is recorded as a finding with its own follow-up ticket proposed to
  the planning hub, not fixed here.
- [ ] Nothing changes under `kernel/` or `workspace/`, and no product code changes. The full
  native suite and the `web` job pass, and `/code-review` runs on the branch's diff.
