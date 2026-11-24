# 05: The `web` CI job under `CI gate`, its artifact, the version and the manifest

**What to build:** Every pull request that takes the build path also builds Modular for the web
and runs its tests under Node, and `CI gate` waits for the result. A change that breaks the
web build therefore fails in the pull request that makes it. Each run uploads the built
module, its loader, its declarations and a manifest as a workflow artifact, so a midishare
session can commit the files of a merged `main` commit (map ticket 03, decision 4, option 1b).
`loadModular()` reports the commit and toolchain that built the module. See the
[spec](../spec.md) (Version and manifest; The web build's scope and CI) and map ticket 03
(decisions 1, 2, 4, 5 and 10).

**Blocked by:** 03: The module and its loader: a page renders through `loadModular`.

**Status:** ready-for-agent

## Checklist

- [ ] The web build's full scope. The `web` test preset runs, under Node:
  - the kernel, node, MIDI-file, soundfont and authoring tests, persistence included;
  - kernel purity;
  - the render core's tests and the smoke test.

  Test executables that read or write files link Node's raw file system. The MIDI-file tie
  case from ticket 01 passes here. When spec A 08 has landed, its golden test passes here too.
- [ ] Exclusions are measured on the Linux runner, not assumed. A test case that fails under
  Node only because of the file system (for example the WAV recorder's two directory-count
  cases, seen failing on a Windows host) is excluded by name, with its reason in
  `docs/web.md`. Also excluded, with their reasons:
  - the graph editor and its tests;
  - `midi_cli_smoke`, whose script runs the CLI directly;
  - the extract tool's test.
- [ ] A `web` job in the existing workflow:
  - It `needs: classify`, and is skipped on the documentation-only path as `windows` is.
  - It runs on `ubuntu-24.04`.
  - It clones `emscripten-core/emsdk` at tag 6.0.13, checks that the tag resolves to the
    expected commit, then runs `emsdk install` and `emsdk activate` for 6.0.13.
  - It configures, builds and runs CTest with the `web` presets.
  - It has no cache. The Emscripten version appears in one place in the workflow.
- [ ] `CI gate` adds `web` to its needs and checks it as it checks `windows`: success on the build
  path, success or skipped on the documentation-only path. A skipped or cancelled `web` job on
  the build path fails the gate. Permissions stay `contents: read`; no ruleset or setting
  changes.
- [ ] Version, baked by the build and exposed by `loadModular()` as `version`:
  - the commit of the tree it was built from;
  - `dirty`, true when that tree had uncommitted changes;
  - the Emscripten version.

  The commit is read when the module is built, not only when CMake configures. The declaration
  file and `docs/web.md` gain `version`. The smoke test checks that it is present and well
  formed.
- [ ] Manifest, written by the build itself, so a local build and the CI artifact carry the
  same file. It records:
  - the commit and `dirty`;
  - the Emscripten version;
  - the build flags;
  - each shipped file's name, size and SHA-256.

  It covers the loader, the Emscripten glue, the `.wasm` and the declaration file.
- [ ] On every run the job uploads those files and the manifest as one workflow artifact, named
  for its commit, with an upload action pinned by commit SHA. The default retention applies.
- [ ] The job's duration on the runner is measured over at least one run and recorded under
  Comments. If installing emsdk dominates, that is noted for a later cache, which is not
  added here.
- [ ] Docs:
  - `docs/web.md` describes the job, the artifact, the manifest, the version, the pinned
    Emscripten version, and what the web build covers and does not;
  - `docs/windows.md`'s CI section points to it and no longer lists "builds on other platforms"
    as uncovered without that pointer;
  - `AGENTS.md`'s build line mentions `docs/web.md`.
- [ ] Nothing changes under `kernel/` or `workspace/`. The full native suite passes
  (`docs/windows.md`), the `web` job passes on the pull request, and `/code-review` runs on the
  branch's diff.
