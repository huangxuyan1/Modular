# Install an Emscripten toolchain the probe can use

Type: task
Status: open

## Question

The probe ([Probe: does the offline MIDI render run in a page as WebAssembly?](02-probe-offline-render-in-a-page.md))
needs a C++-to-WebAssembly compiler, and none is installed. Emscripten is the toolchain to
use: it is the one that ships the browser glue, C++ exception support and a CMake toolchain
file. Installing it means downloading software, so the user does it or approves an agent doing
it.

Checklist:

- Install the Emscripten SDK from its official repository (`emscripten-core/emsdk` on GitHub),
  outside this repository, and activate one pinned release rather than "latest".
- Confirm `emcc --version` and `emcmake cmake --version` both run from a fresh shell.
- Record in the answer: the pinned Emscripten version, and how a session activates it
  (`emsdk_env`). Record no machine paths.

Nothing in this repository changes. Whether Modular's CI later gains an Emscripten job is
[Where does the web build live, and how does a built module reach the site?](03-where-the-web-build-lives.md).
