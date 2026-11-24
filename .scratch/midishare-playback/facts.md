# Charting facts

What charting found on 2026-11-24, read-only, so ticket sessions need not repeat the
inspection. Modular facts are at `origin/main` fdb1206. midishare facts come from the
midishare session's facts-only brief, checked against charting's own reading. Recheck a fact
before a decision rests on it.

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

From the midishare session's facts-only brief, read from the midishare repository at its
commit 91721ab on 2026-11-24. Charting's own read-only pass agreed with it on every point it
covered; the brief added the detail below. midishare's source is public on GitHub, so its
files are named here by their path inside that repository.

**One library makes every sound:** abcjs 6.7.2, pinned exactly, through its built-in synth
`abcjs.synth.CreateSynth`. abcjs is also the ABC parser, the sheet-music renderer and the MIDI
writer; those are separate from the synth. The site has no other audio code than two wrappers:

- `src/lib/use-playback.ts`, the transport hook and the only place a clip is played. The editor
  and the clip page both call it, and pass its values to the transport and the piano roll,
  which never touch audio themselves.
- `src/lib/note-preview.ts`, the one-note preview.

`src/lib/abc.ts` makes Download .mid with abcjs's `getMidiFile` in binary form, and makes no
sound. `src/lib/abc-notes.ts` gets the piano roll's notes from abcjs's flattened note events
(`setUpAudio`), so the roll shows what abcjs plays, chord symbols such as "Dm" expanded into
real notes. Card rolls and the landing demo are silent.

**The transport's interface.** `usePlayback(abc, barSteps)` returns `status` (stopped, loading
or playing), `position` (the playhead in fractional 16th-note steps, updated every animation
frame while playing), and `play`, `stop`, `reset`, `seek` and `endSeek`.

**What playback does.**

- **Build once per ABC text.** The synth's `init` fetches the samples the clip needs, then
  `prime` mixes the whole clip into one stereo `AudioBuffer` at the `AudioContext`'s sample
  rate. A replay or a seek reuses the buffer; a changed ABC text rebuilds it. Status is
  "loading" during the build, and seeks are ignored then.
- **Play from a position** converts steps to seconds, seeks the buffer and starts it. A playhead
  at or past the end starts from 0.
- **Seek while playing** goes silent on the first drag move and plays from the drop.
- **End of clip** is found by the hook, comparing `performance.now()` against the buffer's
  length, which includes abcjs's default 200 ms fade tail. There is no loop.
- **The playhead clock** is `performance.now()`, not the audio clock, at one constant tempo
  for the whole clip.
- Editing the ABC, or leaving the page, stops playback.

**Note preview.** Pressing a note, and each pitch change while dragging one, renders a
one-note clip (an eighth note at 120 BPM, 0.25 s) with a new synth and plays it. The latest
request wins: a new one stops the sounding one and cancels one still loading. Failures are
silent. No preview sounds while the clip is playing.

**Not done anywhere:** recording, microphone or MIDI-device input, effects, volume, loop,
metronome, tempo change during playback, audio-file export, instrument choice, velocity (the
site's notes carry pitch, start and length; loudness is what abcjs assigns), sound on cards or
the landing page, and any real-time synthesis: every sound is a pre-rendered buffer.

**Where it runs.** On the main thread, through one shared `AudioContext` that abcjs creates on
the first Play or note press and never closes. `prime` renders with an `OfflineAudioContext`
per unique note, and playback is an `AudioBufferSourceNode` connected straight to the output.
The repository has no AudioWorklet, WebAssembly, Web Worker or server-side audio.

**Samples.** One MP3 per pitch of FluidR3_GM's acoustic grand piano, from abcjs's default host
on GitHub Pages (the `midi-js-soundfonts` project). The site sets no sound-font URL and no
program. A clip's first Play fetches the pitches it uses; a pitch's first preview fetches that
pitch; abcjs caches decoded samples for the life of the page.

**Stack and hosting.** TypeScript, React 19 and Vite 8, built by `vite build`, served by a
Cloudflare Worker whose asset layer serves the built files. `deepspace deploy` runs
`vite build`, which cannot run `emcc`, so a module would have to arrive prebuilt. The live
site's Content-Security-Policy sets only `object-src`, `base-uri` and `frame-ancestors`, with
no `script-src` or `connect-src`, and it sends no cross-origin isolation headers, so
`SharedArrayBuffer` is unavailable.

**Browsers and performance.** The repository names no target browsers or devices. Sound has
been tested in Chromium only; there is no record of a phone, Safari or Firefox. No latency or
performance requirement is written down. The first Play after an edit waits for the sample
downloads and the full render; later plays start at once. ABC is capped at 5000 characters
and the roll covers MIDI notes 36 to 95 (C2 to B6).

**What else names the current sound** and would change with it: two end-to-end tests detect
"a note sounded" by watching for the sample-file requests, and the centred-playhead test plays
a clip with the network samples (`tests/collab.spec.ts`); the landing page credits abcjs for
the sound; the README and submission docs describe the abcjs synth.

**Audio work in progress:** none. Another midishare session is shipping non-audio features
until the submission deadline.

**The candidate route on file.** `docs/MODULAR_AUDIO_ENGINE.md` in the midishare repository,
written by an agent on 2026-11-23 and never approved by the user, proposes rendering offline:
abcjs MIDI bytes into a Modular WebAssembly function, a `Float32Array` back, played as an
`AudioBuffer`; seek and stop stay in Web Audio; the built module is committed in midishare; a
sine voice first, then a soundfont supernode; the abcjs synth kept until the new path is
verified live. It is one candidate route, not a decision. It predates the note preview and
playhead dragging.
