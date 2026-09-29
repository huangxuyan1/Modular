# Windows development

Use native Windows with CMake 3.20+ and Visual Studio 2022 Build Tools, including
the C++ workload and a Windows SDK. No WSL or Bash is needed for the build or tests.

From the repository root in PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1
```

This configures the `windows` preset, builds Debug, and runs all eight CTest
entries: kernel, node, authoring and MIDI-file tests, the actual graph-editor app
check and a focused gesture regression, a CLI smoke test, and kernel purity. The app check requires a Windows
desktop and briefly displays a test window for a rendering capture before closing
it. Any failed step stops the script. The execution-policy option applies only to this PowerShell process;
it allows the local script without changing the machine's policy.

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

Prefer the script in agent shells: it normalizes duplicate `Path`/`PATH`
environment entries that otherwise cause MSBuild error MSB6001 before compilation.
It changes only the current process environment. MSVC compiles sources as UTF-8,
so non-ASCII comments work regardless of the Windows display language.

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

`AGENTS.md` and `CLAUDE.md` contain identical, self-contained startup rules for
Codex and Claude Code. Keep them byte-identical when editing instructions. Detailed
layer decisions and coding conventions are in `docs/project-foundations.md`; read
that file when working in those areas. Domain vocabulary, ADRs and local tickets
remain in `CONTEXT.md`, `docs/adr/` and `.scratch/`.

Codex discovers project `AGENTS.md` instructions when a run starts. Its documented
default combined limit is 32 KiB; keep the root file within that budget and account
for any additional instruction files. Editing a file does not prove that an already
running session reloaded it. See the [official instruction-discovery documentation](https://learn.chatgpt.com/docs/agent-configuration/agents-md).
