# Charting facts

What charting found on 2026-11-24, read-only, so ticket sessions need not repeat the
inspection. Modular facts are at `origin/main` fdb1206. midishare facts come from reading the
midishare repository's working tree; the midishare session's facts-only brief replaces them
where the two differ. Recheck a fact before a decision rests on it.

## Modular in a web page

**The kernel is portable as written.** `kernel/` uses only the C++20 standard library: no
platform header, thread, atomic or filesystem include. Authoring and compile errors throw
`std::invalid_argument`; `process()` asserts and never throws. Emscripten's default build
cannot catch an exception, so the module needs exception support switched on (JavaScript-based
or native WebAssembly exceptions); which one, and its size cost, is the probe's to measure.

**`Engine` already has the shape a client drives.** `Engine(graph, sampleRate,
maxBlockFrames)` compiles in its constructor, and `process(numFrames)` accepts a different
block size on every call (`kernel/include/modular/Engine.h`). The graph must not be edited
while an `Engine` on it exists. The block loop allocates nothing except `EventBuffer::push`,
the documented v1 exception.

**One global sample rate** (`docs/project-foundations.md`, roadmap ticket 04): a render runs
at one rate, so a page renders at its `AudioContext`'s rate rather than resampling afterwards.

**Platform-specific code sits outside the kernel and is already fenced.**

| File | What it needs | Web build |
|---|---|---|
| `nodes/src/AudioOutputNode.cpp` | WinMM, `windows.h` | Not built: CMake builds it only on `WIN32`. |
| `nodes/src/WavRecorderNode.cpp` | a `_WIN32` branch, `<filesystem>`, `<mutex>` | Not needed to play a clip. |
| `workspace/src/GraphFiles.cpp` | a `_WIN32` branch, `<filesystem>` | Not needed unless a graph is opened from a file. |
| `workspace/include/modular/GraphExecution.h` | `std::jthread` pacing `process()` with a sleep | Not needed: offline render needs no pacing, and a realtime page is paced by its audio callback. Threads in WebAssembly need `SharedArrayBuffer`, which needs cross-origin isolation headers. |

**What exists to play a MIDI clip.** `apps/midi_cli/main.cpp` builds trigger → MIDI file →
`VoiceAllocatorNode(8)` → eight `SineSynthNode` → `MixNode` → `GainNode` (level from a
`ConstFloatNode`) → `CaptureSinkNode`, then calls `Engine::process` block by block.
`MidiFile::parse` reads a Standard MIDI File from bytes in memory (`MidiFile.h`); only `read`
needs a path. `CaptureSinkNode` collects interleaved samples in memory (`BasicNodes.h`).

**What is missing for a good sound.**

- `SineSynthNode` is a bare sine with a hard on and off: no envelope, velocity ignored, 0.5
  amplitude per voice (`MidiNodes.h`).
- No sample-playback node and no soundfont parser exist.
- `VoiceAllocatorNode` frees a voice at its note-off, so the next note-on can take a voice whose
  release is still sounding (`MidiNodes.h`). A voice cannot tell the allocator it is still busy
  without a wire back, and a cycle is a compile error in v1.
- `MidiBlockNode` always starts from the top; seek and loop are deferred.
- An unconnected `value` input reads zero (`Node.h`, `Buffers.h`), so an unwired gain silences
  its output. Roadmap ticket 13 holds that question.

**Persistence is already a text format for handing over a graph.** Save/Open writes and reads
`MODULAR_GRAPH` text, and `nodes/src/PrimitivePersistence.cpp` rebuilds each primitive from its
saved kind and content (`docs/graph-persistence.md`). Opening needs a stream, not a file.

**No web build exists.** Nothing in the repository mentions Emscripten or WebAssembly. The
set-aside plan in the midishare repository records that `emcc` was not installed on the
development machine on 2026-11-23.

## midishare today

**Its sound is abcjs's built-in synth** (abcjs 6.7.2). One playback hook serves the editor and
the clip page. On Play it:

1. parses the ABC;
2. creates the synth, whose `init` downloads one recorded piano note per pitch from a public
   soundfont host (FluidR3_GM, hosted on GitHub Pages) at play time;
3. `prime` renders the whole clip into one buffer;
4. `seek` and `start` play that buffer from the playhead.

Editing the music stops playback, and the next Play rebuilds the buffer. The playhead is
computed from the clock and the tune's milliseconds per bar, not read from the audio, and the
end of the music is detected from the buffer's duration.

**Note preview.** Pressing a note in the piano roll, or dragging it across pitches, renders a
one-note clip (an eighth note at 120 BPM) with the same synth. Only one preview sounds at a
time. It does not sound while the clip is playing.

**Download .mid** comes from abcjs's `getMidiFile` in binary form.

**Hosting.** A Cloudflare Worker serves the built site as static assets; deploy runs
`vite build`, which cannot run `emcc`, so a built module would have to arrive prebuilt. The
site's headers set a Content-Security-Policy with no `script-src` directive and send no
cross-origin isolation headers, so `SharedArrayBuffer` is unavailable.

**Size of a clip.** ABC is capped at 5000 characters; the AI assistant writes at most 16 bars.

**The site's own record.** Its README lists the abcjs synth and its third-party sample download
under known limits and says a change of audio engine is planned. A set-aside plan for this
change, written 2026-11-23, proposes rendering offline in WebAssembly and playing the buffer
with Web Audio, with a soundfont supernode in Modular as the instrument.
