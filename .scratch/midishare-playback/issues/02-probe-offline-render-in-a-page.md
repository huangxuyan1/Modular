# Probe: does the offline MIDI render run in a page as WebAssembly?

Type: prototype
Status: open
Blocked by: 01

## Question

Every Modular build so far is native. This is a **spike**: try the cheapest thing that would
expose a web build's problems, and report numbers, not opinions.

- Build `modular_kernel`, the header-only nodes and `modular_midi` with Emscripten. Wrap
  `apps/midi_cli`'s graph (trigger → MIDI file → voice allocator → sine voices → mix → gain →
  capture) in one exported function that takes Standard MIDI File bytes and a sample rate and
  returns samples. The sine voice stands in for the soundfont node here and is never shipped.
- Call it from a plain local page, play the result through an `AudioBufferSourceNode`, and
  render a clip as long as midishare allows (5000 characters of ABC).
- Throwaway code under `.scratch/midishare-playback/prototypes/`, outside the build, as roadmap
  ticket 04's probe was. Nothing in the midishare repository is touched.

Report:

- which source files needed a change to compile, and why;
- the exception mode chosen (JavaScript-based or native WebAssembly exceptions), and whether an
  authoring error thrown inside the module arrives in JavaScript as a catchable error;
- module size, raw and compressed, with and without exceptions;
- time to instantiate the module, and time to render the longest clip, at 44.1 and 48 kHz;
- whether the browser's render equals the native `midi_cli` render of the same file, sample
  for sample, and if not, by how much it differs (feeds
  [How is a browser render checked against a native one?](08-browser-render-checked-against-native.md));
- whether `<filesystem>` or other unused library code ends up in the module;
- which browsers it ran in. midishare names no target browsers and has tested sound in
  Chromium only; the exception mode decides the oldest browser that can load the module.

Outcome: turns "the first WebAssembly build of a C++20 project usually needs fixes" from a risk
into a list, and gives the build-location and surface tickets their numbers.
