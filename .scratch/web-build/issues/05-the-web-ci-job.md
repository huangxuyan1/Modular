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

**Status:** done

## Checklist

- [x] The web build's full scope. The `web` test preset runs, under Node:
  - the kernel, node, MIDI-file, soundfont and authoring tests, persistence included;
  - kernel purity;
  - the render core's tests and the smoke test.

  Test executables that read or write files link Node's raw file system. The MIDI-file tie
  case from ticket 01 passes here. When spec A 08 has landed, its golden test passes here too.
- [x] Exclusions are measured on the Linux runner, not assumed. A test case that fails under
  Node only because of the file system (for example the WAV recorder's two directory-count
  cases, seen failing on a Windows host) is excluded by name, with its reason in
  `docs/web.md`. Also excluded, with their reasons:
  - the graph editor and its tests;
  - `midi_cli_smoke`, whose script runs the CLI directly;
  - the extract tool's test.
- [x] A `web` job in the existing workflow:
  - It `needs: classify`, and is skipped on the documentation-only path as `windows` is.
  - It runs on `ubuntu-24.04`.
  - It clones `emscripten-core/emsdk` at tag 6.0.13, checks that the tag resolves to the
    expected commit, then runs `emsdk install` and `emsdk activate` for 6.0.13.
  - It configures, builds and runs CTest with the `web` presets.
  - It has no cache. The Emscripten version appears in one place in the workflow.
- [x] `CI gate` adds `web` to its needs and checks it as it checks `windows`: success on the build
  path, success or skipped on the documentation-only path. A skipped or cancelled `web` job on
  the build path fails the gate. Permissions stay `contents: read`; no ruleset or setting
  changes.
- [x] Version, baked by the build and exposed by `loadModular()` as `version`:
  - the commit of the tree it was built from;
  - `dirty`, true when that tree had uncommitted changes;
  - the Emscripten version.

  The commit is read when the module is built, not only when CMake configures. The declaration
  file and `docs/web.md` gain `version`. The smoke test checks that it is present and well
  formed.
- [x] Manifest, written by the build itself, so a local build and the CI artifact carry the
  same file. It records:
  - the commit and `dirty`;
  - the Emscripten version;
  - the build flags;
  - each shipped file's name, size and SHA-256.

  It covers the loader, the Emscripten glue, the `.wasm` and the declaration file.
- [x] On every run the job uploads those files and the manifest as one workflow artifact, named
  for its commit, with an upload action pinned by commit SHA. The default retention applies.
- [x] The job's duration on the runner is measured over at least one run and recorded under
  Comments. If installing emsdk dominates, that is noted for a later cache, which is not
  added here.
- [x] Docs:
  - `docs/web.md` describes the job, the artifact, the manifest, the version, the pinned
    Emscripten version, and what the web build covers and does not;
  - `docs/windows.md`'s CI section points to it and no longer lists "builds on other platforms"
    as uncovered without that pointer;
  - `AGENTS.md`'s build line mentions `docs/web.md`.
- [x] Nothing changes under `kernel/` or `workspace/`. The full native suite passes
  (`docs/windows.md`), the `web` job passes on the pull request, and `/code-review` runs on the
  branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/web-build-05-the-web-ci-job`, cut from a
freshly fetched `origin/main` at `00bfcac`. Before its first push it was rebased onto
`f57d9ce`, where soundfont node 08 had landed; no file overlapped. Pull request #36.

- *What was built.*
  - `.github/workflows/ci.yml`: a `web` job, `needs: classify`, skipped on the
    documentation-only path as `windows` is, on `ubuntu-24.04`.
    - It clones emsdk at the tag `6.0.13` into the runner's temporary folder, outside the
      checkout, and stops unless the tag is `35659964bc2762fffec0d257997fb61475f765b7`
      (ticket 01's `35659964`, checked with `git ls-remote`: a lightweight tag on that
      commit). Then `emsdk install` and `emsdk activate`. No cache.
    - Configure, build and test are three steps, each sourcing `emsdk_env.sh`, so
      `EMSDK` and `EMSDK_NODE` are set (ticket 03's note).
    - It writes the manifest into the run's summary, then uploads `build/web/dist/` as
      `modular-web-${{ github.sha }}` with `actions/upload-artifact` v7.0.2, pinned by its
      tag's commit `cf430e030ddbb5b0abf93d22962f4752f3646cd9` (read from the action's
      repository). Default retention.
    - `EMSCRIPTEN_VERSION` and `EMSDK_COMMIT` sit together in the job's `env`, the one
      place the version is named. The job inherits the workflow's `contents: read`; the
      upload uses the runner's artifact token, not `GITHUB_TOKEN`.
    - `CI gate` needs `web` and checks it as it checks `windows`.
  - **Version.** `apps/web/stamp_version.cmake` runs on every build, from the
    `modular_web_version` target. It reads `git rev-parse --verify HEAD` and
    `git status --porcelain --untracked-files=no`, and writes `WebVersion.h` and
    `WebVersion.cmake` into the build tree, each only when it changes. So a build of an
    unchanged tree links nothing, and a new commit or a newly dirty tree relinks the
    module. `apps/web/Version.cpp`, a new file beside `Exports.cpp`, exports
    `modular_version_commit`, `modular_version_dirty` and `modular_version_emscripten`.
    The loader reads them once into `version`; the declarations state it as the spec
    writes it.
  - **Manifest.** `apps/web/write_manifest.cmake`, from the `modular_web_manifest` target
    in `ALL`, writes `dist/modular.manifest.json` after the module links, or when the
    loader, the declarations or the version change. It holds the version, the flags and
    each shipped file's name, size and SHA-256; no date and no path.
  - **Scope.** The `web` test preset has no filter, so it runs every test the web build
    registers: 9 entries. `midi_cli_smoke` is registered only natively, as
    `soundfont_extract` already was. Under Emscripten, the six doctest executables link
    `-sALLOW_MEMORY_GROWTH=1`, and the four that touch files `-sNODERAWFS=1`. A small
    CMake function turns named cases into doctest's `--test-case-exclude`, only under
    Emscripten.
  - `.gitattributes` checks `apps/web/*.mjs` and `apps/web/*.d.mts` out with LF (ticket
    03's note). Their hashes now match between this Windows checkout and the runner.
  - Docs: `docs/web.md` (scope and exclusions, version and manifest, the job, the pin, the
    artifact, five files in the local recipe), `docs/windows.md`'s CI section,
    `AGENTS.md`'s build line and `docs/project-foundations.md`'s pointer.
  - Nothing under `kernel/` or `workspace/` changed.
- *Choices made here.*
  - **The two worker cases are excluded although G7 names only file-system cases.** The
    web build has no threads (spec), so `GraphExecution` throws `thread constructor
    failed: Not supported`; ticket 01's Comments listed both for this ticket to name. The
    rule applied is "Node-only failures, by name, with reasons": file system or no
    threads.
  - **Four file-system cases are excluded on a Windows host only.** On the Linux runner
    every file-system case passes. On Windows, Node's `unlink` of a directory fails with
    `EPERM` (checked directly: `fs.unlinkSync` on a directory), not `EISDIR`, so the C
    library's `remove()` never falls back to `rmdir`. The four cases each remove a
    directory: two in `node_tests` (the WAV recorder's take directory) and two in
    `authoring_tests` (Save's temporary directory, and a recording graph's). Excluding them
    on Windows hosts keeps the documented local recipe green; CI runs them on Linux.
  - **Memory growth for the doctest executables.** At the default 16 MiB,
    `soundfont_tests` aborts with `Aborted(OOM)` under Node. They grow as the module does.
  - **`dirty` counts tracked files only**, as `scripts/soundfont_extract.py` does.
  - **A build outside a Git checkout stops**, since the module could not say what it was
    built from. `find_package(Git REQUIRED)` applies only under Emscripten.
  - **The version lives in a new `Version.cpp`**, not in `Exports.cpp`, so ticket 06's
    work on the exports does not meet it. The loader change is seven lines.
  - **The manifest's flags are composed** from CMake's flags and the module's
    `COMPILE_OPTIONS` and `LINK_OPTIONS`, in CMake's command-line order. They equal the
    `FLAGS` and `LINK_FLAGS` in `build.ninja`, here and on the runner. A later change to how
    CMake adds the standard flag (policy CMP0128) could make them drift; the smoke test
    checks only that both carry `-fwasm-exceptions`.
  - **A pull request's run builds GitHub's merge commit**, as `windows` does, so its
    artifact and `version` name that merge. A client takes a push to `main`'s artifact.
  - **CI uploads `dist/` as it is**, and the smoke test checks that it holds only the
    manifest's four files and the manifest.
- *Tests.* One `node:test` case in `tests/test_web_smoke.mjs`, now 7 cases: `version` is
  well formed (40 hexadecimal digits, a boolean, `x.y.z`), and the manifest beside the
  loader carries the same three facts and `-fwasm-exceptions` in both flag strings, lists
  the four files and matches each one's size and SHA-256, and the folder holds nothing
  else.
  - *Red, then green.* Before the build stamped anything, the case failed with `TypeError:
    Cannot read properties of undefined (reading 'commit')`. Against the built tree, an
    extra file in `dist/` and a changed `modular.d.mts` each fail it.
- *CI runs* (pull request #36; every job green and `CI gate` passed, both times).
  - Run `45459497707`, head `26a7af1`: `web` took 83 s. Set-up and checkout 2 s, emsdk
    install 25 s, configure 2 s, build 49 s, tests 2 s, manifest and upload 1 s. `windows`
    took 66 s (Debug) and 106 s (Release).
  - Run `45461316185`, head `4813836`: `web` took 65 s. Set-up and checkout 8 s, install
    21 s, configure 1 s, build 30 s, tests 2 s, upload 1 s. `windows` took 81 s and 110 s.
  - **The install does not dominate:** about a third of the job, while the build is about
    half, and the job ends before `windows (Release)`. No cache is needed for now.
  - The image has Ninja; emsdk installed SDK `releases-eb132757…` and Node 24.19.0.
  - Under Node on Linux, all 9 CTest entries passed. `node_tests` runs with no exclusion,
    so its 16 cases passed; `authoring_tests` ran 26 of 28, the two worker cases left out.
  - Artifact `modular-web-dc7f8a6de6973110559bb5d882de46f06f93b4e3`: 5 files
    (`modular.d.mts`, `modular.mjs`, `modular_web.mjs`, `modular_web.wasm`,
    `modular.manifest.json`), 47,255 bytes zipped, kept for GitHub's default 90 days.
- *Reproducibility across hosts.* The merge commit of run 2, `dc7f8a6`, was checked out
  and built here on Windows with the same emsdk. Its manifest equals the runner's for the
  commit, `dirty`, the flags, the loader, the declarations and the `.wasm` (98,940 bytes,
  SHA-256 `d2373ae4…`). **The glue differs:** 7,920 bytes here, 7,918 on the runner. Finding
  why needs the runner's glue, which means downloading the artifact, a user approval this
  session does not have. Open below.
- *Sizes.* `modular_web.wasm` is 98,940 bytes, down from ticket 03's 109,013: soundfont
  node 08's portable maths replaced the maths library. The version adds 160 bytes
  (109,013 to 109,173 before the rebase). The loader is 3,793 bytes and the declarations
  2,123.
- *Checks, on the final tree.*
  - `web`, Emscripten 6.0.13 and its Node 24.19.0 on Windows: 9/9 CTest entries. doctest
    cases: kernel 44, node 14 (2 left out on this host), authoring 24 (4 left out), MIDI-file
    18 (the tie case included), soundfont 67 (spec A 08's golden test included), render
    core 14; smoke test 7.
  - Native, through the script: Debug 11/11 and Release 11/11 CTest entries, both `desktop`
    tests, the extract tool's test and kernel purity included.
  - `python scripts/check_docs.py`: 0 problems. `git diff --check`: clean.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at `14f7d37`,
  with a clean working tree. The fixes are re-checked above.
  - Standards, fixed:
    - the pin's procedure now also rechecks the root `CMakeLists.txt` comment that names
      6.0.13;
    - names: the exclusion lists are `node_excluded_cases` and `authoring_excluded_cases`,
      the function `modular_web_exclusion_arguments`, and the loops' variable `target`;
    - the version file's path is one variable, `MODULAR_WEB_VERSION_FILE`;
    - **possible Speculative Generality:** a `CMAKE_CXX_EXTENSIONS` branch the project never
      takes is gone; the comment says the standard is the extension form;
    - **possible Shotgun Surgery** (the shipped files listed in four places): CI now uploads
      `dist/` as it is, and the smoke test pins what it holds. The manifest writer and the
      docs still name the files, deliberately.
  - Standards, kept:
    - **possible Duplicated Code:** three steps source `emsdk_env.sh`. Copying its variables
      into `GITHUB_ENV` and `GITHUB_PATH` would repeat emsdk's own environment logic; the
      comment states the choice.
    - **No `name:` on the job:** the check reads `web`, beside `windows (Debug)` and
      `windows (Release)`, the name the ticket and the planning hub use.
    - the flags' drift risk, above.
  - Spec, fixed: `docs/web.md` said the presets build every target; they build every
    target the web build defines.
  - Spec, recorded: the worker cases outside G7's wording, and the additions it listed (the
    summary step, stopping outside a checkout, the Windows-host exclusions, memory growth,
    tracked-only `dirty`), all under *Choices* above.
  - Spec, limit: the reviewer did not check the two pinned commits against their tags.
    They were checked when written, from `git ls-remote` and the action's tag API, and run
    1's tag check passed.

**Left open:**

- The glue differs by 2 bytes between a Windows host and the runner, at the same commit
  and Emscripten. Finding why needs the runner's glue, so the user's approval to download
  the artifact. Until then, compare a local build with an artifact by the `.wasm`, the
  loader and the declarations.
- The gate's documentation-only path with `web` skipped is not exercised by this pull
  request, which takes the build path. Its check mirrors `windows`' line for line.
