# Where does the web build live, and how does a built module reach the site?

Type: grilling
Status: claimed
Blocked by: 02

## Question

midishare deploys by running `vite build`, which cannot run `emcc`, so the site can only ship
a module built somewhere else. Decide where, and what Modular takes on by it.

- **Is the web a supported Modular platform?** Supported means an Emscripten configure in
  CMake, a CI job that builds it on every PR, and a `docs/` page beside `docs/windows.md`.
  The alternative is a recipe a client follows at its own risk. Supported catches a change that
  breaks the web build when it is made; it also makes every later kernel change pay for a
  second toolchain.
- **Where does the glue live?** The exported functions and their JavaScript loader are client
  code. In this repository they would be an `apps/` target, and `AGENTS.md` puts client policy
  in `apps/`. But which instrument and how many voices is midishare's policy, so a glue layer
  that fixes them in C++ puts midishare's policy in Modular. How far that matters depends on
  [What does the page hand Modular, and what does it get back?](04-page-to-modular-contract.md).
- **How does a module reach the site?** Candidates: built here and committed into the midishare
  repository with the Modular commit recorded beside it; or built by Modular's CI and published
  as a versioned artifact (a release asset or a package) that midishare pins. Either way the
  site states which Modular commit it plays.
- Does any of this need cross-origin isolation, or change midishare's headers? With offline
  render and no threads, it should not; confirm from the probe.

The probe supplies the facts this rests on: which libraries build, what had to change, and
how large the module is.

## Comments

### Investigation pass, 2026-11-24

A ticket session investigated and recommends; the planning hub decides. Modular facts are at
`origin/main` 0d0db07. midishare facts were read from the midishare repository (its Vite,
TypeScript, ESLint and Wrangler configuration, `public/_headers`, and the deploy command in
its installed SDK); nothing there was built or run. The measurements below used Emscripten
6.0.13 ([ticket 01](01-install-emscripten.md)) on the development machine, building into a
folder outside the repository. No file outside this ticket changed.

Tags: **[Modular]** is decided here and built by this repository's tickets; **[midishare]** is
the midishare session's, and the user's there; **[user approval needed]** needs the user's
explicit request (a release, tag, setting, account, download, or a push beyond a PR branch).

#### Facts

**What builds with Emscripten today** (measured: `emcmake cmake`, Release, Ninja,
`-fwasm-exceptions`):

- The whole CMake project configures unchanged. `if(WIN32)` already keeps the Windows audio
  node, the graph editor and its UI tests out.
- `modular_kernel`, `modular_workspace`, `modular_midi`, `modular_recorder`, `render_cli`,
  `midi_cli`, `kernel_tests`, `node_tests` and `midi_file_tests` build with no source change.
- **`modular_primitive_persistence` does not.** `nodes/src/PrimitivePersistence.cpp` reads the
  MIDI node's `long double` duration with `std::from_chars`, which libc++ declares deleted for
  `long double`. `authoring_tests` links it, so it cannot build either. MSVC accepts the call;
  any libc++ build refuses it (Emscripten here, presumably macOS too, untested). The probe did
  not build persistence, so this break has been on `main` unseen: CI builds only with MSVC.
- Emscripten's toolchain file sets Node.js as CMake's cross-compiling emulator, so `ctest` runs
  each test executable under Node with no change to `add_test`. With `-sNODERAWFS=1` linked
  into the tests (they read fixtures from disk), `kernel_tests`, `midi_file_tests` and
  `kernel_purity` pass. `node_tests` fails 2 of its 16 cases, both the WAV recorder's
  file-system checks (directory counts after Stop ties and after a partial write), on a Windows
  host; a Linux runner is untested. `midi_cli_smoke` fails because its CMake script runs
  `midi_cli.js` directly rather than through the emulator.
- Times on the development machine (16 threads): configure 2.2 s; a clean build of the targets
  above 9.7 s, and 11.9 s at 4 jobs; those tests run in about 1 s. The probe's module, built
  with one `em++` command, takes 10.8 s.
- **The output is reproducible.** The probe's ES6 module built from a copy of the sources in
  another folder is byte for byte equal, `.wasm` and `.mjs`, to one built from the checkout
  (same machine and toolchain). Windows against Linux hosts is unmeasured.
- The `-sEXPORT_ES6` glue finds its `.wasm` with `new URL("<name>.wasm", import.meta.url)`
  unless the caller passes `locateFile`. When streaming compilation fails, for example on a
  wrong content type, the 6.0.13 glue logs "wasm streaming compile failed" and falls back to
  instantiating from an `ArrayBuffer`.

**Modular's repository and CI.**

- Public on GitHub, with no releases, no tags and no `package.json`; nothing is published.
  The repository has no `LICENSE` file.
- One ruleset on `main`: squash merges through a PR, no force push or deletion, and `CI gate`
  as the only required status check (strict). A new job under the gate changes no setting.
- [`ci.yml`](../../../.github/workflows/ci.yml) runs with `permissions: contents: read` and
  pins actions by commit SHA. `windows` (Debug and Release on `windows-2022`) runs off the
  docs-only path, and `CI gate` fails unless every needed job succeeded. Recent `windows` jobs
  took 47 to 115 s each, in parallel.
- `CMakePresets.json` is version 2 with one `windows` preset (Visual Studio generator).
  Version 2 has no `toolchainFile` field; `CMAKE_TOOLCHAIN_FILE` as a cache variable works.
- `apps/` holds three clients: `render_cli`, `midi_cli` and `graph_editor`.
  [`docs/windows.md`](../../../docs/windows.md#continuous-integration) lists "builds on other
  platforms" and "releases or deployment" as outside CI.

**midishare's build and site** (read, not run):

- Its deploy command runs `npx vite build` on the developer's machine and uploads the result.
  With GitHub as the app's source it ships the checkout as it is, uncommitted bytes included,
  and records no commit for the release. So a module must be in midishare's working tree when
  the deploy runs; nothing in midishare's build fetches or builds it. The repository has no CI.
- `public/_headers` applies to every path: CSP `object-src 'none'; base-uri 'self';
  frame-ancestors 'none'`, `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, and
  referrer and permissions policies. No `Cache-Control` and no cross-origin isolation headers.
  The deploy passes `_headers` to the asset layer, so midishare can set caching there.
- Asset requests are answered by the asset layer before the worker, which sees only misses
  (`run_worker_first` lists only API and platform paths).
- TypeScript is `strict` over `src`. ESLint lints only `.ts` and `.tsx`, with one rule.
  `vite build` type-checks nothing, but `npm run validate` runs `tsc`, which wants a
  declaration for any imported `.mjs`.
- On the live site, hashed assets are served with `Cache-Control: max-age=0, must-revalidate`
  and an ETag, and no `.wasm` is served yet
  ([ticket 04](04-page-to-modular-contract.md), the midishare session's item 7). The host
  serves at most 25 MiB per file ([research](../research/07-which-soundfonts-ship.md)).

#### Judgment calls

**J1. Is the web a supported Modular platform?** [Modular]

- *Options.* (a) Supported: a `web` configure preset, a `web` CI job on every PR that takes the
  build path, under `CI gate`, and `docs/web.md` beside `docs/windows.md`. (b) A recipe:
  `docs/web.md` alone, which a client follows at its own risk. (c) Between: a web job only on
  pushes to `main`, or nightly, outside the gate.
- *What (a) costs.* Every later code change must also compile with Clang and libc++. A change
  that breaks only the web build needs the 2.0 GB toolchain to reproduce locally, or iterates
  through CI. About 1.5 to 2.5 minutes of CI per PR, in parallel with `windows` (J10), so the
  wait grows by a minute at most. No setting changes.
- *What (b) and (c) miss.* The `from_chars` break above is the example: it compiles under
  MSVC, it is on `main` today, and only a second compiler found it. Clang also warns where
  MSVC does not (the probe's two warnings). Under (b) the next such break surfaces when
  midishare next needs a module; under (c), after it has merged.
- *Recommendation: (a), scoped.* The `web` preset builds what the web surface needs and the
  tests that pass under Node, as `if(WIN32)` scopes the Windows build today. Targets that do
  not belong on the web stay out with a stated reason: persistence until J2's condition,
  `midi_cli_smoke`, and the WAV recorder's file-system cases if they also fail on Linux.
  `docs/web.md` says, as `docs/windows.md` does, what CI covers and what it does not. The probe
  needed no source change for the render path, so the standing cost is small, and the platform
  this map ships on is then checked on every PR instead of at the next midishare update.

**J2. The persistence break under libc++.** [Modular]

- *Options.* (a) Keep `modular_primitive_persistence` and `authoring_tests` out of the web
  configure: the contract opens no saved graph. (b) Fix the parse in
  `nodes/src/PrimitivePersistence.cpp` so it builds on any standard library. (c) Store `double`
  instead, extending the probe's optional `long double` patch.
- *Recommendation: (a) now,* recorded in `docs/web.md` and the spec as a known gap. The planning
  hub declined the `long double` change ([ticket 02](02-probe-offline-render-in-a-page.md),
  decision 2), and nothing at this destination saves or opens a graph in a browser. Take (b)
  when a libc++ build needs persistence: the contract's deferred saved-graph instrument, or a
  macOS build. It is a small `nodes/` change, never a kernel or workspace one; ticket 02's note
  on 36-digit output belongs with it.

**J3. Where the glue lives, and what it fixes.** [Modular]

- *Facts.* The contract's [glue] is the exported C functions; the graph built inside them (the
  MIDI file node from bytes, the soundfont node, a preallocated stereo sink); the tail trim
  (−80 dBFS, a 2 s cap); the 10-minute limit; exceptions caught at each export; and the
  JavaScript loader, which runs calls one at a time, rejects with Modular's message, copies the
  channels out and discards the instance after a trap
  ([ticket 04](04-page-to-modular-contract.md), decisions 4–7, 10 and 12).
- *How much of that is midishare's policy? Almost none.* The soundfont, when it loads, the
  fallback, padding to the musical end, and the preview's pitch, length and velocity are page
  code. The voice pool (256) and the preset (a single-preset file's only one) are the soundfont
  node's defaults ([ticket 06](06-release-survives-voice-allocation.md),
  [ticket 05](05-soundfont-node-shape-and-samples.md)). The tail values and the length limit
  are surface defaults; only the 10-minute limit waits on midishare, whose longest clip is
  12.3 s today. What remains is a general surface, "render Standard MIDI File bytes through a
  SoundFont instrument to stereo samples", which any page can use.
- *Options.* (a) `apps/web/`: one CMake target that links the kernel, `modular_midi` and the
  soundfont node and emits the module, plus a hand-written ES module loader and a TypeScript
  declaration file. (b) The exports in `apps/web/`, the loader in midishare. (c) All of it in
  midishare.
- *Recommendation: (a).* It is a client of the kernel as `midi_cli` is, so it belongs in
  `apps/` by `AGENTS.md`; midishare is its client in turn. Name it for what it does: no
  midishare name, path or limit in Modular's code. Keep the tail and length values as named
  defaults in one place, and add options (preset, voice limit) only when a client needs one;
  the stretch goal may need `preset`. Ship the declaration file, since midishare's `tsc` is
  strict. (b) splits one contract across two repositories and two reviews; (c) puts C++ that
  Modular's CI never sees in a repository that cannot build it. Nothing under `kernel/` or
  `workspace/` changes.

**J4. How a built module reaches the site.** [Modular] to produce it, [midishare] to take it
in; option 2 [user approval needed]

- *Option 1: committed into the midishare repository, with the Modular commit recorded beside
  it.* The files come either (1a) from a build on the development machine by a Modular
  session, or (1b) from the `web` CI job, which uploads them as a workflow artifact on every
  run: no release, no tag, and `contents: read` suffices. GitHub keeps an artifact 90 days by
  default, enough to fetch it once. A midishare session downloads the artifact of a merged
  `main` commit and commits it on a midishare branch.
  - *Needs from the user:* in Modular, only the usual authorization of the implementing ticket
    (the `ci.yml` change rides its PR). In midishare, the go-ahead for that branch after the
    deadline, and the artifact download in that session (downloads need the user's yes).
  - *Cost:* roughly 100 to 200 KB of generated files in midishare's history per update (the
    probe's 98 KB module and glue, plus the soundfont node; an estimate), copied by hand.
- *Option 2: Modular's CI publishes a versioned artifact that midishare pins.*
  - A GitHub release asset, a tarball. midishare can depend on the tarball's URL through npm,
    and its lockfile pins it by integrity hash, so no npm account is needed. *Needs from the
    user:* a tag and a release per version, a release job holding `contents: write` (a wider
    token in `ci.yml`), and a versioning rule. On midishare's side, a module loaded from
    `node_modules` may need `optimizeDeps.exclude` so Vite's dev server keeps the glue's
    `new URL(..., import.meta.url)` working (a common need with Emscripten packages; unconfirmed
    here).
  - An npm package: the same, plus an npm account and a publish token kept as a repository
    secret, an account and a setting that are both the user's.
  - Publishing for other clients would also raise the missing `LICENSE`. A module for the
    author's own site does not.
- *Recommendation: option 1 with (1b).* It needs no Modular release, tag, setting or wider
  token. The module is built from a reviewed, clean commit by the same job that tests it. And
  midishare's deploy, which ships its checkout and records no commit, then ships bytes that
  midishare itself committed. The build is reproducible, so anyone can rebuild a commit and
  compare hashes. (1a) is the stopgap until the CI job exists. Option 2 changes only the
  transport, so it can replace option 1 when a second client appears or the user wants
  versioned releases; the files and the manifest stay the same.
- Neither option touches midishare's deployed site. Each midishare change is on a branch after
  2026-11-24, 11:59 PM ET, deployed only when the user says.

**J5. How the site states which Modular commit it plays.** [Modular] [midishare]

- *Recommendation.* The build bakes the Modular commit and the Emscripten version into the
  module, and `loadModular()` exposes them (for example `modular.version`), so the live page can
  report what it plays. The CI artifact carries a manifest: the Modular commit, the Emscripten
  version, the build flags and each file's SHA-256. midishare commits the manifest beside the
  files. [Modular] for the baked version and the manifest; [midishare] for where the version
  is shown.

**J6. The tool that makes the soundfont extract.** [Modular]

- *Facts.* [Ticket 10](10-measure-the-fluidr3-piano.md) measured with a throwaway
  standard-library Python reader and wrote no extract. Ticket 05 (D5) puts cutting a file
  outside the node. The research left open whether an editor such as Polyphone keeps every
  generator and modulator. The full `FluidR3_GM.sf2` sits outside both repositories, matched
  to Debian's SHA-256 (`74594e8f…`, ticket 10).
- *Options.* (a) A small Python script, standard library only, in Modular (for example under
  `tools/`, outside the CMake build). It copies one preset, its instrument and the samples
  they use into a new SF2, writes the full MIT notice and a provenance line into the INFO
  list, and gives the same bytes for the same input. (b) A C++ tool in `apps/` sharing the
  node's parser; it still needs a writer the node does not have. (c) Polyphone by hand: an
  install (the user's approval), steps that cannot be rerun exactly, and unknown fidelity.
  (d) A script in midishare.
- *Recommendation: (a), delivered with the soundfont node's spec.* The extract is correct when
  the node reads it as it reads that preset in the full file, so test it there: the node renders
  preset 0:0 from the full file and from the extract identically, and the FluidSynth comparison
  ([ticket 09](09-soundfont-format-subset.md)) runs on the extract. The script serves the
  stretch goal's further presets, and its fixed output makes the extract's SHA-256 a checkable
  record. (d) puts a SoundFont writer in a TypeScript app that cannot test it against the node.

**J7. Where the extract lives, its licence and its provenance.** [midishare] for the files,
[Modular] for the record; one download [user approval needed]

- *Where.* Committed only in midishare. Modular's node library ships no soundfont (ticket 05,
  D4), and the full file is committed nowhere. The 7.45 MiB extract is 30 % of the host's
  25 MiB limit and is served from the site's own static assets
  ([ticket 04](04-page-to-modular-contract.md), decision 9). *Recommendation:* midishare
  imports it through Vite (`?url`), so its name carries a content hash and it can be cached
  for good (J9). A fixed name in `public/` also works, but revalidates.
- *Licence.* MIT requires the copyright and permission notice in copies, and the file's own
  INFO only names the licence (ticket 10). *Recommendation:* the full notice both inside the
  extract's INFO list, so it travels with any copy, and as a text file beside it on the site,
  with a credit in midishare's README. The text is `COPYING` from Debian's `fluid-soundfont`
  3.1-6 source: a small new download, which needs the user's approval (ticket 10's approval
  covered the `.sf2` only).
- *Provenance.* The source URL and Debian version, the source's SHA-256, preset 0:0, the tool's
  Modular commit and the extract's SHA-256: inside the extract's INFO, in a file beside it in
  midishare, and in Modular's soundfont spec, so the FluidSynth comparison names exact bytes.

**J8. Cross-origin isolation.** [midishare], no change

- *Confirmed: none is needed.* The module uses no threads and no `SharedArrayBuffer`: it is
  built without `-pthread`, its only imports are `emscripten_resize_heap` and `_abort_js`, and
  memory growth without threads is a plain `ArrayBuffer`. The probe ran it from a static
  server that sends no isolation headers. midishare's headers stay as they are. Moving the
  render into a Web Worker later (the map's fog) needs no isolation either, since buffers move
  by `postMessage` transfer; only the out-of-scope AudioWorklet route with shared memory would.
- *One condition for midishare:* its CSP has no `script-src` or `default-src`, so WebAssembly
  compiles. If it ever adds either, it must allow `'wasm-unsafe-eval'`.

**J9. The `.wasm` content type and caching.** [Modular] [midishare]

- *Content type.* [midishare] The asset layer infers a type from the extension;
  `application/wasm` is expected but unconfirmed until a `.wasm` is deployed, so check it with a
  header request on the first branch preview. If it is wrong, the glue falls back to
  `ArrayBuffer` instantiation (facts): it still loads, compiles more slowly and warns in the
  console, and a `_headers` rule corrects it.
- *Caching.* [midishare] Today every page load revalidates hashed assets: one conditional
  request per file, answered with a bodiless 304. Long caching is an optimization, not a
  requirement. A `/assets/*` rule in `public/_headers` with
  `Cache-Control: public, max-age=31536000, immutable` is safe because those names carry
  content hashes; it must not cover fixed names.
- *Modular's part.* [Modular] Produce files that are safe to cache forever under a hashed name,
  and never assume a fixed URL. With the files under midishare's `src/`, the glue's
  `new URL(..., import.meta.url)` lets Vite emit the `.wasm` with a hash; the loader also takes
  a URL option (`locateFile`) so a page can place it elsewhere.
- *Compression.* Whether the host compresses `application/wasm` is unconfirmed. Uncompressed,
  the probe's module and glue are 98 KB, small beside the 7.45 MiB soundfont, which barely
  compresses anyway.

**J10. The CI job, its runner and its toolchain.** [Modular]

- *Shape.* A `web` job with `needs: classify`, skipped on the docs-only path as `windows` is.
  `CI gate` adds it to `needs` and checks its result as it checks `windows`: success on the
  build path, success or skipped on the docs-only path. The gate stays the one required check
  and still fails on a skipped or cancelled job. No ruleset change.
- *Runner.* Ubuntu, pinned (`ubuntu-24.04`) as `windows-2022` is. Emscripten brings its own
  Clang, Node.js and Python, and the image carries CMake, Ninja and Git (if Ninja is missing,
  one package install). It is the quickest image, and hosted minutes cost nothing on a public
  repository.
- *Toolchain options.* (a) Clone `emscripten-core/emsdk` at tag 6.0.13, check the tag's commit,
  then `emsdk install 6.0.13` and `emsdk activate 6.0.13`: ticket 01's own steps, first-party
  code only, 35 s on the development machine. (b) The same plus `actions/cache` of the emsdk
  folder: about 2 GB an entry against the repository's 10 GB, and restoring it may save little
  over downloading. (c) The official `emscripten/emsdk:6.0.13` container image, pinned by
  digest; its pull time is unmeasured. (d) A third-party setup action pinned by SHA: more code
  from outside running with the job's token.
- *Recommendation: (a) without a cache to start;* add (b) only if the measured install
  dominates. The pinned version lives in one place in `ci.yml` and in `docs/web.md`, and moves
  in one PR.
- *Estimate, unmeasured on a runner:* set-up and checkout 10 s, emsdk 30 to 90 s, configure 5 s,
  build 20 to 40 s (12 s here at 4 jobs; a hosted runner has 4 vCPUs), tests and artifact upload
  a few seconds each: about 1.5 to 2.5 minutes, beside `windows`' 1 to 2.

#### For ticket 08

[How is a browser render checked against a native one?](08-browser-render-checked-against-native.md)
depends on J1. Under (a), its check can run in the `web` job, since CTest already runs
Emscripten builds under Node. One configure cannot build both the web module and an MSVC
reference, so a native reference reaches the `web` job as committed expected output (hashes or
files), as an artifact of the `windows` job (which makes the two jobs run in turn), or from a
third, native compiler on the Linux runner. Which one is ticket 08's call.

#### What the recommendations would change

- Under `kernel/` and `workspace/`: nothing.
- Elsewhere in Modular, through tickets: `CMakeLists.txt` (an `apps/web` target and the web
  scope), `CMakePresets.json` (a `web` preset), `ci.yml` (the `web` job, its artifact and the
  gate), `docs/web.md` and the CI section of `docs/windows.md`, `apps/web/`, a `tools/` script
  for the extract, and the `nodes/` work already decided (the MIDI file node from bytes and the
  soundfont node). Perhaps one mention of `docs/web.md` in `AGENTS.md`'s build line.
- No ADR: a supported platform is undone by removing a job, and the trade-off is recorded here.
  No new `CONTEXT.md` term.

#### What each option needs from the user

- J1 (a), J2 (a), J3 (a), J4 option 1 (1b), J5, J6 (a) and J10 (a): nothing beyond authorizing
  the implementing tickets as usual.
- J4 option 1, midishare's side: the go-ahead for a midishare branch after the deadline, and
  the artifact download in that session.
- J4 option 2: a tag and a release per version and a `contents: write` job; for npm, also an
  account and a repository secret.
- J7: the download of Debian's `COPYING` text.
- J6 (c), if chosen: installing Polyphone.
