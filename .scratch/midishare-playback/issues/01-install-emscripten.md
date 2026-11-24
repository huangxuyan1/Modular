# Install an Emscripten toolchain the probe can use

Type: task
Status: open

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
