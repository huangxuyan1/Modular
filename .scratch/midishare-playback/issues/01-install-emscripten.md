# Install an Emscripten toolchain the probe can use

Type: task
Status: resolved

## Question

The probe ([Probe: does the offline MIDI render run in a page as WebAssembly?](02-probe-offline-render-in-a-page.md))
needs a C++-to-WebAssembly compiler, and none is installed. Emscripten is the toolchain to
use: it is the one that ships the browser glue, C++ exception support and a CMake toolchain
file. The user approved installing it from its official repository at a pinned release
(round 1, 2026-11-24).

**Pinned release: 6.0.13** (tagged 2026-10-19), the last release of the 6.0 line. The newer
7.0.0 (2026-11-06) opened a new major line and was followed by 7.0.1 on 2026-11-21, so the
probe starts on the settled one. Moving up is a later, separate step.

Checklist:

1. Clone `https://github.com/emscripten-core/emsdk` into a folder of its own, outside this
   repository and outside the midishare repository, and check out the tag `6.0.13`.
2. From that folder, `emsdk install 6.0.13`, then `emsdk activate 6.0.13`, **without**
   `--permanent`. The install downloads the toolchain (Clang/LLVM, Binaryen, Node.js and
   Emscripten) into that folder only. Activation writes its configuration file in that folder
   and changes no user or system environment variable.
3. In a fresh PowerShell, run that folder's `emsdk_env.ps1`, which sets `PATH` and the
   `EMSDK` variables for that shell only. Then confirm `emcc --version` reports 6.0.13 and
   `emcmake cmake --version` runs. Emscripten's CMake builds need a Ninja or Makefile
   generator, not Visual Studio's.
4. Record in the answer: the version, the disk space the folder takes, how a session activates
   the toolchain (`emsdk_env.ps1`), and anything the install asked for that this list did not
   say. Record no machine paths.

Undoing it is deleting that one folder. Nothing in this repository changes. Whether Modular's
CI later gains an Emscripten job is
[Where does the web build live, and how does a built module reach the site?](03-where-the-web-build-lives.md).

## Answer

Resolved 2026-11-24. **Emscripten 6.0.13 is installed and verified; the probe can build.**

**What was installed.** emsdk was cloned from its official repository into a folder named
`emsdk` in the user's home folder, outside both repositories, and checked out at the tag
`6.0.13`: commit `35659964`, "Release 6.0.13", dated 2026-10-19 and shown as verified by
GitHub. `emsdk install 6.0.13` resolved to the SDK build
`releases-eb132757325583b787fe9414b09afea9442b72ad` and took 35 seconds. `emsdk activate 6.0.13`
ran without `--permanent`. No administrator rights were used. The user and machine environment
variables were compared before and after activation, and neither changed.

| Part | Version |
| --- | --- |
| Emscripten (`emcc`) | 6.0.13 (`e13ee988`) |
| Clang/LLVM | 24.0.0git (`83ee0779`) |
| Binaryen (`wasm-opt`) | 133 |
| Node.js, bundled | 24.19.0 |
| Python, bundled | 3.13.3 |
| Ninja, already present | 1.12.1 |
| CMake, already present | 4.0.0-rc4 |

**Disk space: 2.0 GB** (2,028 MB in 18,032 files). The toolchain takes 1,852 MB of it, and
597 MB of that is the prebuilt cache of system libraries. The bundled Node.js takes 101 MB, the
bundled Python 71 MB and the emsdk clone 3 MB. emsdk deleted the downloaded archives (about
770 MB) after unpacking them. The hello-world builds below added 0.3 MB to the cache.

**How a session activates it.** In PowerShell, once per shell:

```powershell
. "$env:USERPROFILE\emsdk\emsdk_env.ps1"
```

This sets `PATH`, `EMSDK`, `EMSDK_NODE` and `EMSDK_PYTHON` for that shell only; nothing
persists. It runs under the account's existing `RemoteSigned` execution policy. To configure a
build, run `emcmake cmake -S <source> -B <build>`. `emcmake` adds Emscripten's toolchain file,
Node.js as the cross-compiling emulator and `-G Ninja`. Then run `cmake --build <build>`. To undo
the install, delete the folder.

**Verified** in a fresh `powershell -NoProfile -ExecutionPolicy RemoteSigned`, which had no
`emcc` until `emsdk_env.ps1` ran:

- `emcc --version` reports 6.0.13.
- `emcmake cmake --version` runs.
- Hello-world: a C++20 file that sums a `std::vector`, then throws a `std::invalid_argument`
  and catches it as `std::exception`. It was built twice with `-fexceptions`, in a temporary
  folder outside the repository:
  - with `emcmake cmake` and Ninja (`hello.wasm` 22,466 bytes);
  - with `em++ -std=c++20 -O2` (`hello.wasm` 22,612 bytes).

  Both builds ran under the bundled Node.js, printed the sum, caught the exception and exited
  with 0. Each `.wasm` file begins with the WebAssembly magic bytes.

**Not in the checklist, or unexpected.**

- **Python.** emsdk installed its own Python 3.13.3 next to Node.js. The checklist named only
  Clang/LLVM, Binaryen, Node.js and Emscripten. The system Python is not used.
- **Download host.** The archives came from emsdk's release storage on Google Cloud Storage
  (`storage.googleapis.com/webassembly/emscripten-releases-builds`), where emsdk's manifest
  points, not from GitHub.
- **Linking C++.** Use `em++`, not `emcc`. `emcc hello.cpp` compiled but failed to link,
  reporting undefined C++ library symbols such as `operator new` and `__cxa_throw`. Emscripten's
  error message suggests `em++` or `-sDEFAULT_TO_CXX`. CMake picks `em++` by itself.
- **Node.js on `PATH`.** After `emsdk_env.ps1`, `node` on `PATH` is still the system Node.js
  (24.14.0). Emscripten uses its bundled copy through `EMSDK_NODE`, so run a built module with
  `& $env:EMSDK_NODE` when the version matters.
- **Ninja.** Nothing was installed for it. `emcmake` uses the Ninja on `PATH`, a Windows Package
  Manager install. Visual Studio 2022 Build Tools also bundles Ninja 1.12.1 with its CMake
  component, but that copy is on `PATH` only in a developer shell.
- **CMake.** The system CMake is a release candidate, 4.0.0-rc4. It configured the hello-world
  without complaint.
- **Exception modes.** The cache ships prebuilt C++ library variants with exceptions disabled,
  for `-fexceptions`, and for native WebAssembly exceptions (`wasmexcept` and `legacyexcept`).
  So the probe can compare exception modes without building libraries first. Only
  `-fexceptions` was tried.
- **`--permanent` prompt.** `emsdk activate` ends by suggesting `--permanent` or `--system`.
  Neither was used, as the checklist says.
