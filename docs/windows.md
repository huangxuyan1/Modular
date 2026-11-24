# Windows development

Use native Windows with CMake 3.20+ and Visual Studio 2022 Build Tools, including
the C++ workload and a Windows SDK. No WSL or Bash is needed for the build or tests.

From the repository root in PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1
```

This configures the `windows` preset, builds Debug, and runs all twelve CTest
entries: kernel, node, authoring, MIDI-file and soundfont tests, the web render
core's tests with its golden clips (`web_core_tests`), a CLI smoke test, the
soundfont extract tool's test (`soundfont_extract`), kernel purity, the soundfont
node's portable-maths check (`soundfont_portable_maths`), and the two
`desktop`-labelled entries, the
actual graph-editor app check (`graph_editor_app`) and a focused gesture regression
(`graph_editor_gesture`). The extract tool's test runs `scripts/soundfont_extract.py`
with the Python 3 (3.8 or later) that CMake finds when it configures; with none
found, that test fails and says so. The portable-maths check, like kernel purity, reads
the source: it fails when a soundfont node source other than
`nodes/src/SoundFontMaths.h` calls a maths-library transcendental such as `std::sin`
or `exp2` (see [node-io.md](node-io.md#soundfontnode)).
Any failed step stops the script. The execution-policy option applies only to
this PowerShell process; it allows the local script without changing the
machine's policy.

The `desktop` tests launch the real editor on a private Win32 desktop that the
test creates. The editor's window, file choosers and dialogs open there, so
nothing appears on the screen or takes focus, and the editor is killed with the
test, even when a CTest timeout ends it. They add about 20 seconds per
configuration. Append `-ExcludeDesktop` to run only the ten headless entries.

For an optimized build, append `-Configuration Release`. Executables are under
`build/windows/Debug/` or `build/windows/Release/`.

For a MIDI-file-to-speakers/WAV example, see [node-io.md](node-io.md).
For the flat graph editor, launch `build/windows/Debug/graph_editor.exe` and see
[graph-editor.md](graph-editor.md) for gestures, settings and verification.

For a focused build/check, the script accepts `-Target` and `-TestRegex`; for
example, `-Target authoring_tests -TestRegex '^authoring_tests$'`. With neither
option, it builds all targets and runs the full suite.

The equivalent individual commands are:

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
```

`ctest --preset windows -LE desktop` (`--label-exclude`) runs only the headless
tests, the same selection hosted CI runs; `-L desktop` runs only the two editor
tests. Append `--build-config Release` for Release.

Prefer the script in agent shells: it normalizes duplicate `Path`/`PATH`
environment entries that otherwise cause MSBuild error MSB6001 before compilation.
It changes only the current process environment. MSVC compiles sources as UTF-8,
so non-ASCII comments work regardless of the Windows display language.

## Continuous integration

GitHub Actions runs `.github/workflows/ci.yml` on every pull request and every
push to `main`, and again when a pull request's base branch changes. Its **CI gate**
job is the one check for branch protection to require. It always reports, and it
passes only if every job the change needs has passed; a skipped or cancelled job
makes it fail rather than pass.

- **Changed-file scope:** a change is documentation-only when every changed path
  ends in `.md` and none is under `.github/`. Anything else, including CI changes,
  build files, an empty diff or a base commit CI cannot find, takes the build path.
- **Every change:** `python scripts/check_docs.py` (Python 3, also runnable
  locally) checks that each repository-internal Markdown link reaches a tracked
  file and, for Markdown targets, an existing heading, and that `CLAUDE.md` imports
  `AGENTS.md` (a line reading `@AGENTS.md`). `git diff --check` rejects whitespace
  errors and conflict markers in the changed lines. No Markdown style linter runs.
- **Build path:** on the `windows-2022` image (Visual Studio 2022, the preset's
  generator), Debug and Release each configure the `windows` preset, build every
  target and run CTest with `--label-exclude desktop`: the kernel, node, authoring,
  MIDI-file, soundfont and web render core tests, the golden clips among them, the
  CLI smoke test, the soundfont extract tool's test, kernel purity and the soundfont
  portable-maths check. The golden clips pass with the same values here and in the
  `web` job ([web.md](web.md#golden-clips)).
- **Build path, web:** on `ubuntu-24.04`, a `web` job installs the pinned Emscripten,
  builds with the `web` presets, runs the web build's tests under Node and
  uploads the built module as an artifact. [web.md](web.md#continuous-integration)
  describes it, and [what the web build covers](web.md#what-the-web-build-covers).

CI does not cover:

- the two `desktop`-labelled tests, `graph_editor_app` and `graph_editor_gesture`,
  which drive the real graph-editor window. The local script runs them by
  default (or use `ctest --preset windows -L desktop`); when a change affects the
  graph editor, record that local result as the ticket's GUI evidence;
- whether external links resolve, audible playback through an audio device, or native
  builds on other platforms, such as macOS or Linux; the web build is the `web` job's;
- releases or deployment; nothing merges automatically.

## Moving between machines

Keep a separate build directory for each platform. The copied root `build/`
contains the Mac cache; the Windows preset creates its own cache underneath it.
Source changes are shared; generated caches and executables are machine-specific.

If moving a checkout from a filesystem without Unix executable bits, a local
`core.filemode=false` setting can avoid spurious mode changes. `.gitattributes`
keeps shell scripts on LF line endings; tracked executable bits remain in Git's
index.

If compiler detection fails and leaves a partial Windows cache, fix the reported
cause, then reconfigure with `cmake --fresh --preset windows` (CMake 3.24+).
Run this in an environment with a single `Path` entry, as established by the script.

## VS Code

Use Microsoft's C/C++ and CMake Tools extensions. Select the `windows` configure
preset in CMake Tools, and use `ms-vscode.cmake-tools` as the C/C++ configuration
provider. A local, ignored `.vscode/c_cpp_properties.json` may contain machine-specific
Windows and Mac configurations; it is not part of the source tree.

The Visual Studio generator does not produce `compile_commands.json`; use the
CMake Tools provider for include paths and compiler flags. A copied root
`compile_commands.json` may be a placeholder rather than a usable compilation
database; the Windows configuration does not reference it.

## Agent context

`AGENTS.md` holds the self-contained startup rules every agent shares. `CLAUDE.md`
imports it with `@AGENTS.md`, which Claude Code inlines at startup, and adds only
Claude Code context. Put shared rules in `AGENTS.md`. Detailed
layer decisions and coding conventions are in `docs/project-foundations.md`; read
that file when working in those areas. Domain vocabulary, ADRs and local tickets
remain in `CONTEXT.md`, `docs/adr/` and `.scratch/`.

Codex discovers project `AGENTS.md` instructions when a run starts. Its documented
default combined limit is 32 KiB; keep the root file within that budget and account
for any additional instruction files. Editing a file does not prove that an already
running session reloaded it. See the [official instruction-discovery documentation](https://learn.chatgpt.com/docs/agent-configuration/agents-md).
