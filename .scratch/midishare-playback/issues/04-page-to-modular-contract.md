# What does the page hand Modular, and what does it get back?

Type: grilling
Status: resolved

## Question

The contract the site codes against. It must serve Play (a whole clip) and note preview (one
short note), and it must not quietly decide roadmap questions that are still open.

- **What goes in.** abcjs already makes Standard MIDI File bytes for Download .mid, and
  `MidiFile::parse` reads bytes from memory. Feeding Modular those bytes makes "what you hear is
  what you download" true by construction. The alternative is abcjs's flattened note events
  (`setUpAudio`), which the piano roll already draws, chord symbols expanded; feeding those
  makes "what you hear is what you see" true instead. Check whether the two agree: whether the
  MIDI file carries everything the roll shows and the synth plays (chord symbols, grace notes,
  swing, `%%MIDI program`, the velocities abcjs assigns), because whatever it omits goes
  silent. The site's own notes carry no velocity.
- **What the page names: a function, a graph or commands.**
  1. One fixed function, for example `render(midiBytes, sampleRate)`. Smallest, but the graph
     (instrument, voice count, level) is compiled in, so every sound change is a rebuild.
  2. A saved graph: the site ships the instrument as `MODULAR_GRAPH` text, and the call opens
     it with the existing codecs (`openGraph` reads a stream), sets one MIDI block's content
     from the clip's bytes, and renders. The instrument becomes authored data the Windows editor
     can make, which is the bet the terminal claim makes. It needs a way to address "the clip's
     MIDI block" in a saved graph.
  3. A JavaScript authoring API (add node, connect, set content, render). This is roadmap
     ticket 07's question, [How is the command/edit API cut?](../../roadmap/issues/07-command-api-cut.md),
     still open; choosing it here would decide 07 by the back door. Every unwired `value` input
     would also read zero, which is roadmap ticket 13's.
- **What comes out.** Samples at the `AudioContext`'s rate (one global rate, roadmap ticket 04),
  one channel or two (abcjs's buffer is stereo today), as a `Float32Array` per channel. How the
  page learns the clip's length, including the release after the last note: a fixed tail (abcjs
  adds a 200 ms fade), or render until the output falls silent, which is the "tail length" idea
  the roadmap ranks for plugins. The site's end-of-clip check reads the buffer's length, so the
  tail is part of the contract.
- **Errors.** Authoring and compile errors throw in Modular. Does one reach JavaScript as an
  exception with Modular's message, or as a result value?

- **The soundfont.** The node reads a soundfont, so the soundfont's bytes reach Modular
  somehow: the page fetches the site's soundfont file and hands Modular the bytes, as with the
  MIDI, or the bytes arrive inside a saved graph, or they are built into the module. When the
  page reads it (on load, on first Play or on first note press), and whether it is read once
  per page or once per render (see the map's fog on repeated renders). Leave room for the
  stretch goal, a choice among a few soundfonts, without designing it.

## Answer

**Two layers.** midishare keeps its own `AudioEngine` (ABC text and an `AudioContext` in, an
`AudioBuffer` out). Its Modular engine file calls a Modular web surface that takes Standard MIDI
File bytes and a sample rate and returns two channels of samples. Modular sees no ABC, abcjs or
Web Audio. The bytes are abcjs's Download .mid bytes as they are, so what you hear is what you
download by construction. The soundfont is handed in once as bytes and parsed once into an
instrument handle that renders any number of clips and previews. Nothing changes under
`kernel/` or `workspace/`.

Every decision below was **decided by the planning hub under the judgment the user delegated
for this map ("All, on your best judgment"), 2026-11-24**, from the recommendations recorded
under Comments, which hold the evidence. Like every planning record, it is provisional until its
planning PR merges.

Tags: **[page]** is midishare code only. **[glue]** is the exported functions and JavaScript
loader Modular ships for the web; where they live and how they are built is
[ticket 03](03-where-the-web-build-lives.md)'s. **[nodes]** is the node library.

### The surface

A sketch, not the final names; the spec fixes them.

```ts
// [glue] Modular's web surface. Every call returns a promise; calls run one at a time.
const modular = await loadModular()                        // instantiate the module, once per page
const piano = await modular.createInstrument(soundfont)    // soundfont: Uint8Array, parsed once
const { sampleRate, channels } = await piano.render(midi, context.sampleRate)
// midi: Uint8Array, Standard MIDI File bytes
// sampleRate: the rate requested, unchanged
// channels: [Float32Array, Float32Array], left and right, equal length, owned by JavaScript
// Failure: the promise rejects with an Error whose message is Modular's.

// [page] midishare's Modular engine file, behind its AudioEngine seam
const midi = abcjs.synth.getMidiFile(abc, { midiOutputType: 'binary' })[0] // Download .mid's bytes
const buffer = context.createBuffer(2, channels[0].length, sampleRate)
buffer.copyToChannel(channels[0], 0)
buffer.copyToChannel(channels[1], 1)
```

### Decisions

1. **Two layers.** [page] [glue] midishare owns its `AudioEngine`. Its Modular engine file calls
   the surface above. abcjs and Web Audio stay out of Modular, and the same surface runs under
   Node.js for [the browser-versus-native check](08-browser-render-checked-against-native.md).
2. **What goes in: abcjs's Download .mid bytes, as they are**, from
   `getMidiFile(abc, {midiOutputType: 'binary'})`. [page] A MIDI writer that midishare owns or
   patches, used for both Download .mid and Play, stays open to midishare; Modular's input does
   not change with it. Feeding the flattened note events through a second input format is
   rejected: it breaks "what you hear is what you download" wherever the writer and the synth
   disagree.
3. **The writer's defects are accepted in the contract.** [page] Modular plays the bytes it is
   given faithfully; fixing the bytes is page policy. **A gate on midishare's side:** before the
   abcjs fallback is removed, either midishare's clip census shows no affected clips or midishare
   patches the writer. The fast-staccato defect matters most: its note-off comes before its
   note-on, so a piano would ring to the end of the file. Until then the fallback stays, as the
   map already requires.
4. **An instrument handle.** [glue] [nodes] `createInstrument(soundfontBytes)` parses the
   soundfont once. `render(midiBytes, sampleRate)` builds a fresh `Engine` per render on the
   kept `Graph`, so every node's `prepare()` starts the render from scratch and roadmap
   ticket 10 is not reached. The graph's shape is fixed in the glue. The one [nodes] addition is
   a `MidiFileNode` import from bytes. A saved `MODULAR_GRAPH` instrument and a JavaScript
   authoring API are rejected for now. The first needs a way to find the clip's MIDI node in a
   saved graph and ships the soundfont as hex text at twice its size; it can arrive later behind
   the same handle with no page change. The second would decide roadmap ticket 07 by the back
   door, and its unwired `value` inputs would raise roadmap ticket 13.
5. **What comes out: `{ sampleRate, channels: [Float32Array, Float32Array] }`, always two
   channels.** [glue] [page] The samples are copied out of WebAssembly memory, at the requested
   rate, with no resampling. The page builds the `AudioBuffer`. Stereo, because the soundfont
   node outputs `audio<2>`
   ([ticket 05](05-soundfont-node-shape-and-samples.md)).
6. **The release tail: render to a cap past the file's end, then trim** sample-accurately below
   a threshold. [glue] The contract promises only that the buffer holds the whole release and
   ends at most a cap past the file's end. Start values, which the spec may tune: −80 dBFS and a
   2 s cap. The length depends on the samples, never on block size, and a node-reported tail
   length (a kernel `Node` interface change, the roadmap's "tail length") is not needed.
   - **Trailing rests** are page policy, since the file ends at its last note-off. A
     recommendation to midishare, not a contract term: pad the buffer with silence to the
     musical end abcjs computes plus 200 ms, so a clip ending in rests ends where it does today.
7. **A maximum length, past which `render` fails with an explicit error** that the page answers
   with the fallback. [glue] It starts at 10 minutes of audio, provisional until midishare's
   census gives the longest clip. Output goes to a preallocated or chunked sink, not a vector
   growing in `process()`; the spec decides which.
8. **Note preview is a one-note clip through the same `render`**, with bytes from
   `getMidiFile(previewAbc(pitch))`. [page] It plays the roll's MIDI pitch for 0.25 s at
   velocity 105, then the release. **An acceptance item for the spec:** time a one-note render
   with the soundfont loaded, in the browser.
9. **Loading.** [page] [glue] The page fetches the soundfont from the site's own static assets
   and hands the bytes in.
   - It loads lazily inside the first `render`, behind one shared promise, so the transport's
     "loading" covers it.
   - Warming up at the first click or key press is allowed.
   - The module and the instrument live for the page's life.
   - The data format is tickets 07, 09 and 10's.
10. **Errors: no C++ exception crosses into JavaScript.** [glue] The glue catches every
    `std::exception` at the module boundary, and the loader rejects with an `Error` carrying
    Modular's message. After a render it checks `Engine::errorMessage()`. After a WebAssembly
    trap the instance is discarded and the next call reloads it. Which exception mode builds the
    module is [ticket 02](02-probe-offline-render-in-a-page.md)'s.
11. **The fallback lives inside midishare's Modular engine file.** [page] A load failure means
    abcjs for the rest of the page's life; a render failure means abcjs for that render. The
    `engine` line is the off switch. Note for midishare: `playFrom` in `use-playback.ts` does
    not catch a rejected `render`, which would leave the status at "loading".
12. **Asynchronous, one call at a time.** [glue] Every call returns a promise, and the loader
    serializes them, so moving the render into a Web Worker later changes no page code.
13. **What must stay true for "what you hear is what Download .mid contains":**
    1. Play and Download .mid get their bytes from one function, with the same options and the
       same ABC text. [page]
    2. Whatever writes the bytes (abcjs 6.7.2 pinned, or a writer midishare owns), both paths
       use it. [page]
    3. Modular plays every note in those bytes, on every channel, at the file's timing and the
       velocity abcjs gave it. Velocity is honoured
       ([ticket 09](09-soundfont-format-subset.md)). [nodes]
    4. Two ABC voices sounding the same pitch both sound
       ([ticket 06](06-release-survives-voice-allocation.md)). [nodes]
    5. The claim covers notes, not General MIDI: program changes, controllers and pitch bend
       are ignored. What the piano does with channel 10's drums is ticket 09's.
    6. A browser render matches a native one on ticket 08's terms.

### The page's open items

For the planning hub to send to the midishare session. None blocks this ticket.

1. A census of stored clips: staccato (`.` or `!staccato!`) and at what `Q:`; tuplets of 7 or
   more; pitches below 16 or above 127; `%%MIDI program`, `drum`, `channel 10`, `chordprog` or
   `bassprog`; quarter-tone accidentals; trailing rests; chord symbols; and the longest clip's
   duration in seconds. It feeds decision 3's gate and decision 7's cap.
2. Whether midishare will own or patch the MIDI writer (decisions 2 and 3).
3. Whether a clip ending in rests should keep ending where it does today (decision 6's
   recommendation).
4. How long a first Play or first note press may wait, how large a download is acceptable, and
   whether to warm up at the first gesture (decision 9).
5. Closing `playFrom`'s missing catch, or relying on the fallback inside the engine file
   (decision 11).
6. How the end-to-end tests will detect that a note sounded once no sample files are fetched.
7. Whether the asset layer serves `.wasm` as `application/wasm` and caches hashed assets long;
   this may belong to ticket 03.

### The midishare session's answers (2026-11-24)

Received by the planning hub after the decisions above. Items 1 and 7 are facts the midishare
session measured, and item 6 is its proposal. Items 2–5 are the user's to decide in that
session; the midishare session has put each to the user with its own recommendation, and none
is decided here.

1. **Census.** The site stores 5 clips, all published examples. None has staccato, a tuplet, a
   pitch below 16 or above 127, a `%%MIDI` line, a quarter tone or trailing rests. One has
   chord symbols, and its accompaniment reaches pitch 33, below the roll's lowest row. The
   longest clip lasts 12.3 s. So decision 3's gate is met for today's clips, and decision 7's
   cap never binds them. Five examples say little about future clips: the editor accepts
   staccato, septuplets, `%%MIDI program` and `drum`, quarter tones and pitches 0 to 132. abcjs's
   `totalDuration` includes trailing rests.
2. **The writer.** The session's recommendation, awaiting the user: don't patch abcjs's writer
   now. The Modular engine file sends an affected clip (fast staccato, a tuplet of 7 or more, a
   pitch below 16) to abcjs for that render, and Download .mid stays as it is. This is page
   policy and fits decision 3; Modular's input does not change.
3. **Clips ending in rests.** The session's recommendation, awaiting the user: keep today's end,
   by padding to abcjs's musical end plus 200 ms.
4. **Waits and download size.** Awaiting the user. The session recommends warming up at the
   first gesture on a page with a transport, not at page load. Today a clip's first Play fetches
   about 0.5 MB of mp3s. The default soundfont chosen in
   [ticket 10](10-measure-the-fluidr3-piano.md) is about 7.45 MiB, and 16-bit PCM barely
   compresses, so the first sound will cost roughly fifteen times that.
5. **The fallback inside the engine file.** The session's recommendation, awaiting the user: yes.
   The session has since made `playFrom` catch a failed render on its branch.
6. **End-to-end tests (proposal).** A test init script wraps
   `AudioBufferSourceNode.prototype.start` and records each start. Which pitch played moves to
   unit tests of the engine file, with a fake instrument.
7. **Asset layer (measured on the live site).** Hashed assets are served with
   `Cache-Control: max-age=0, must-revalidate` and an ETag, so every load revalidates. No
   `.wasm` is served yet, so its content type is unknown until a deploy. This feeds
   [ticket 03](03-where-the-web-build-lives.md).

## Comments

### Investigation pass, 2026-11-24

A ticket session investigated and recommended; the planning hub then decided every call (see
**Answer**). This section is the evidence. Modular facts are at `origin/main` 5d5d61d. midishare facts were read
from the midishare repository and from its engine-seam branch (uncommitted, unpushed and
undeployed when read). abcjs facts come from abcjs 6.7.2's source and from the probe
[`PROTOTYPE_compare.js`](../prototypes/04-abcjs-midi-vs-synth/PROTOTYPE_compare.js), which runs
abcjs under Node.js and compares, clip by clip, what the synth plays today, what Download .mid
contains and what the roll shows. Its numbers are quoted below.

Each call is tagged by what it touches: **[page]** is midishare code only; **[glue]** is the
exported functions and JavaScript loader Modular would ship for the web, wherever
[ticket 03](03-where-the-web-build-lives.md) puts them (an `apps/` target by `AGENTS.md`);
**[nodes]** is the node library. No recommendation below needs a change under `kernel/` or
`workspace/`; the options that would are named where they arise.

#### Facts

**One flattened sequence feeds all three views.** The synth (`CreateSynth.init` calls
`tune.setUpAudio(params)`), the MIDI writer (`abc_midi_create.js` calls
`abcTune.setUpAudio(options)`) and the roll (`abcToNotes` calls `setUpAudio({})`) all flatten
the same way, because the site passes no synth options and `getMidiFile`'s two options
(`midiOutputType`, `generateInline`) are not read by the sequencer or the flattener. So the
three agree on which notes exist, and they differ only where each turns events into time.

**Where the file agrees with what the synth plays** (pitch, onset within 2 ms, length,
velocity; probe): chord symbols (a separate track on its own channel, bass at velocity 64,
chord notes at 48), grace notes (velocity two thirds of the main note's, truncated identically:
63 and 70), triplets and quintuplets, ties, repeats (unrolled), decorations expanded into notes
(trill, mordent, roll), accents (127), dynamics marks (the probe's `!p!` bar played at 60 and
50), beat accents (105, 95, 85), pickup bars, compound meters, and tempo changes. A mid-tune `[Q:]` is
folded into note times at the starting tempo, so the file has one tempo event and its timing
still matches. `%%MIDI swing` changes none of the three: the flattener ignores the directive,
and the synth swings only through a `swing` option the site does not pass. abcjs puts the
loudness it assigns into note-on velocity, so the file carries it even though the site's own
notes do not.

**Where the file differs from what the synth plays** (probe and source):

1. **Staccato.** The writer subtracts the staccato gap scaled by beats per second a second time
   (`abc_midi_create.js`), so a staccato note's length in the file is `d·(1 − 0.4·(Q/60)²)`,
   where the synth plays `d·max(1/3, 1 − 0.4·Q/60)` (Q in quarter notes per minute, simple
   meters). Above Q ≈ 95 the note-off is written *before* its note-on. At Q=120 all five notes of `.C2 .D2 .E2 .F2 |
   .G8` are never turned off and the file ends at 2.0 s instead of 4.0 s. At Q=80 the file's
   notes are shorter than the synth's (0.217 s against 0.35 s).
2. **Tick rounding.** The writer uses 1920 ticks per whole note and rounds every delta without
   carrying the remainder. Septuplets drift: 40 bars of `(7` eighths at Q=120 end 250 ms late.
   Triplets, quintuplets and grace notes measured exact.
3. **Pitches below 16** are written as one hex digit, which corrupts the rest of the file.
   Modular's parser rejects it with `midi file: truncated chunk or event`; the synth plays the
   note. (Checked with a native `midi_cli` build; `MidiFile.cpp` is unchanged on `origin/main`.)
4. **Trailing rests are not in the file.** Each track ends at its last note-off. `C2 z6 | z8`
   lasts 0.5 s in the file and 4.2 s in the synth's buffer. The preview clip lasts 0.25 s in
   the file and 2.2 s in the synth's buffer, since `previewAbc` writes a whole bar.
5. **`%%MIDI program`**: the file has a program change (always on channel 0, once per track,
   at its start); the synth plays that General MIDI instrument's samples; the roll ignores it.
6. **`%%MIDI drum`**: the file puts drum hits on channel 10 with drum note numbers; the synth
   plays percussion samples; the roll draws them as piano notes.
7. **Quarter-tone accidentals**: the file uses pitch-bend events; the synth changes playback
   rate; the roll shows the rounded pitch.
8. **Controllers**: every track starts with reset-all (121), damper off (64), reverb depth 48
   (91), pan centre (10) and channel volume 100 (7). The synth ignores them.
9. **Slurred notes** overlap the next note by 4 ms in the file and 2 ms in the synth.

**The roll is a lossy view.** It rounds every note to the 16th grid and merges equal pitches at
the same start (a unison across two voices: 5 notes heard, 3 drawn), and it carries no
velocity. Feeding Modular the roll's notes would lose tuplet and grace timing and every level
abcjs assigns.

**Download .mid.** It exists on the clip page only, as
`abcjs.synth.getMidiFile(abc, {midiOutputType: 'binary'})[0]` with the same `abc` the clip
page passes to its player (`src/lib/abc.ts`, `src/pages/(app)/clips/[id].tsx`). The probe found
the bytes identical across calls, and identical whether abcjs is given the text or an already
parsed tune.

**What Modular reads today.** `MidiFile::parse` reads bytes from memory: formats 0 and 1, PPQ
timing, tempo in the first track, all channel messages, meta events skipped. abcjs writes
format 1, 480 ticks per quarter note, its tempo in track 0 at tick 0, explicit status bytes and
an end-of-track event closing every track, all of which it accepts (`nodes/src/MidiFile.cpp`).
`MidiFileNode` releases held notes at the file's end, so a note-on that is never turned off
sounds until then. `VoiceAllocatorNode` names a note by pitch only, ignores channels, and drops
anything that is not a note (`MidiNodes.h`). So program changes, controllers and pitch bend are
dropped before any voice, and a unison across two voices on two channels collapses into one
voice. `midi_cli` renders the file's duration plus one frame. `CaptureSinkNode` grows a vector
inside `process()`. `MidiFileNode` can replace its content only from a filesystem path
(`import(path)`).

**midishare's engine seam** (engine-seam branch, `src/lib/audio-engine.ts`): `type AudioEngine
= { render(abc: string, context: AudioContext): Promise<AudioBuffer | null> }`, chosen by one
line, `export const engine = abcjsEngine`. The page owns the one `AudioContext`, made on the
first click or key press, and plays buffers itself (`playBuffer`). The transport renders once
per ABC text and takes the buffer's duration as the end of the clip. The preview renders
`previewAbc(pitch)` through the same `render` and keeps "latest wins". `playFrom` in
`use-playback.ts` does not catch a rejected `render`: a rejection would leave the status at
"loading". The preview swallows failures.

**Clip lengths.** The 5000-character cap does not bound duration. Plain eighths at Q=120 fill
5000 characters with 8.3 minutes; with every bar repeated, 15 minutes; at Q=40, 25 minutes
(probe). Stereo float samples at 48 kHz take 23 MB a minute.

#### Judgment calls

**J1. Where the contract boundary sits.** [page] [glue]

- *Options.* (a) Modular implements midishare's `AudioEngine` itself: it takes ABC text and an
  `AudioContext` and returns an `AudioBuffer`. (b) Two layers: midishare's `AudioEngine` stays
  page-owned, and a Modular engine file in midishare calls a Modular surface that takes SMF
  bytes and a sample rate and returns samples.
- *Recommendation: (b).* abcjs, ABC and Web Audio stay out of Modular, so the same surface runs
  under Node.js for [the browser-versus-native check](08-browser-render-checked-against-native.md),
  and a DAW client can use it later. The Modular engine file is about ten lines: `getMidiFile`,
  render, `createBuffer` and `copyToChannel`, null for a clip with no notes, and the fallback.
  This ticket then decides only the lower surface; the seam above it is midishare's.

**J2. What goes in: Download .mid's bytes, or the note events.** [page] (Modular side: none)

- *Options.* (a) The bytes `getMidiFile(abc, {midiOutputType: 'binary'})` returns, as they are.
  (b) The flattened events, through a second input format. (c) SMF bytes from a MIDI writer
  midishare owns or patches, used for both Download .mid and Play.
- *Recommendation: (a) now, with (c) open to midishare later.* (a) makes "what you hear is what
  you download" true by construction, and Modular reads the bytes today. (b) gives Modular a
  second input format and breaks that clause wherever the writer and the synth disagree. (c)
  keeps the clause and removes the writer's defects (J3), but changes Download .mid. Under (a)
  and (c) Modular's input is the same SMF bytes, so the choice between them is midishare's and
  can change without touching Modular.

**J3. The writer's defects become audible.** [page]

- *Facts.* Differences 1 to 3 above. The fast-staccato one is the worst: a staccato scale
  becomes a held cluster until the file ends. Download .mid has always had these defects; only
  the synth hid them.
- *Options.* (a) Accept them, with the abcjs fallback catching the unreadable files.
  (b) midishare fixes the bytes (J2 (c)), for both Download .mid and Play. (c) Report them to
  abcjs, then upgrade.
- *Recommendation:* (a) while the abcjs synth is still the fallback. Decide between (b) and
  living with them before the fallback goes, using midishare's count of stored clips with
  staccato, septuplets or very low pitches (facts needed, below).

**J4. What the page names: a function, a graph or commands.** [glue] [nodes]

- *Options.* (1) A fixed function with the instrument compiled in. (1′) A fixed graph shape in
  the glue, with the instrument's data handed in once as an *instrument handle*:
  `createInstrument(data)`, then `instrument.render(midiBytes, sampleRate)` as many times as
  needed. (2) A saved `MODULAR_GRAPH` instrument opened with `openGraph`. (3) A JavaScript
  authoring API.
- *Recommendation: (1′).* The handle is the smallest shape that loads a soundfont once and
  renders many clips and previews, and a different soundfont, the stretch goal, is another
  handle. (2) can arrive later behind the same handle without changing the page. It needs a
  soundfont codec ([ticket 05](05-soundfont-node-shape-and-samples.md)), a way to find the
  clip's MIDI node, and stored content as hexadecimal text (twice the soundfont's size, under a
  256 MiB file limit). Finding the node by its kind, in the glue, avoids naming a node in
  `workspace/`. Reject (3): it decides roadmap ticket 07 by the back door, and its unwired
  `value` inputs raise ticket 13.
- *Under (1′)* the handle keeps a `Graph` alive and builds a fresh `Engine` per render, which is
  the existing rule that editing means a new `Engine`. Every node's `prepare()` then restarts
  from scratch, as an offline render wants, so roadmap ticket 10 (state kept across a
  recompile) is not reached. Swapping the clip in needs a small [nodes] addition, a
  `MidiFileNode` import from bytes, or removing and re-adding the node per render. Voice count
  and level stay in the glue until midishare needs to change them.

**J5. What comes out.** [glue] [page]

- *Recommendation:* `{ sampleRate, channels: Float32Array[] }`, one array per channel, all the
  same length, copied out of WebAssembly memory so that memory growth cannot invalidate them.
  `sampleRate` is the rate passed in (the `AudioContext`'s rate; one global rate, no resampling
  in Modular). One channel or two, as the instrument produces; the page builds an `AudioBuffer`
  with that many channels, and Web Audio copies mono to both speakers. Today's buffer is stereo
  with identical channels, since the site sets no pan. Whether the piano is stereo is for
  tickets 05 and 09. Return no `AudioBuffer`: building one needs Web Audio, which a Node.js
  check lacks.

**J6. The clip's length and the release tail.** [glue] [page]

- *Facts.* Today every note fades out linearly over 200 ms after its end, and the buffer is the
  musical end plus 200 ms. The transport ends the clip at the buffer's end and stops the source
  there, so the tail has to fit inside the buffer. The file ends at its last note-off (fact 4).
- *Options for the tail.* (a) A fixed tail. (b) Render past the file's end up to a cap, then
  trim trailing samples below a threshold, sample-accurately. The length then depends only on
  the samples, never on block size. (c) A tail length each node reports, the roadmap's "tail
  length" item: a change to the kernel's `Node` interface, so a roadmap claim, not this map's.
- *Recommendation: (b) in the glue,* with a contract that promises only that the buffer
  includes the whole release and ends at most a stated cap after the file's end. Proposed
  numbers for the hub: −80 dBFS, a 2 s cap. (c) can replace (b) later without changing the
  page.
- *Trailing rests* are page policy: the Modular engine file pads the buffer with silence up to
  the musical end abcjs computes plus 200 ms, so a clip ending in rests ends where it does
  today. [page]

**J7. A maximum clip length.** [glue]

- *Options.* No cap; a cap with an explicit error; a cap plus chunked output.
- *Recommendation:* a cap on the file's duration with an explicit error that the page answers
  with the fallback. Proposed: 10 minutes, `midi_cli`'s limit, about 230 MB of stereo float at
  48 kHz. The glue writes the output in chunks or into a preallocated sink, not through
  `CaptureSinkNode`'s growing vector, so that WebAssembly memory stays near the output's size.
  The number waits on midishare's longest stored clip.

**J8. Note preview.** [page] (a constraint on [nodes])

- *Options.* (a) A one-note clip through the same `render`, with bytes from
  `getMidiFile(previewAbc(pitch))`. (b) A dedicated `renderNote(pitch, seconds, velocity, rate)`.
- *Recommendation: (a).* The soundfont loads once per page into the handle whichever is chosen,
  so (b) saves only an abcjs parse and a tiny MIDI parse. (a) keeps one entry point and gives
  the note what it gets today: the roll's MIDI pitch, 0.25 s, velocity 105 (beat one), then the
  release. The cost to watch is per-render set-up (building the `Engine`, the soundfont node's
  `prepare()` at the context's rate). It must be small enough for a preview on every drag step,
  so the probe or the node ticket should time a one-note render with the soundfont loaded.
- What the soundfont load costs falls on whichever comes first, Play or a note press.
  Warming up (J9) hides it, not a separate method.

**J9. How and when the page loads the module and the soundfont.** [page] [glue]

- *Options for the soundfont's bytes.* (a) The page fetches the site's file and hands the bytes
  to `createInstrument`. (b) Inside a saved graph (J4 (2)). (c) Built into the module.
- *Recommendation: (a).* Modular stays free of networking; the site controls the URL and
  caching; a choice among soundfonts is a choice among URLs. (c) makes every soundfont change a
  module rebuild. What the bytes are (an SF2 file, SF3, or per-note audio the browser decodes)
  belongs to tickets 07 and 09; `createInstrument`'s argument follows their answer.
- *When.* Lazily, inside the first `render` behind one shared promise, so the transport's
  "loading" covers it. The engine may also start fetching at the first click or key press,
  where `audioContext()` is made. Both are page policy. The module and each instrument stay
  for the page's life, and hashed file names let the browser cache them across pages.

**J10. Errors.** [glue]

- *Options.* (a) A C++ exception crosses into JavaScript. (b) The glue catches every
  `std::exception` at the module boundary, and the loader turns it into a rejected promise with
  an `Error` carrying Modular's message.
- *Recommendation: (b).* What arrives in JavaScript then does not depend on the exception mode
  [the probe](02-probe-offline-render-in-a-page.md) picks. Exceptions must still be enabled,
  because `MidiFile::parse` throws inside the module. After a render the glue also checks
  `Engine::errorMessage()`. A WebAssembly trap (out of memory) rejects too, and the page then
  discards the module instance.

**J11. The abcjs fallback.** [page]

- *Recommendation:* inside the Modular engine file. A failure to load the module or the
  soundfont switches the page to `abcjsEngine` for its remaining life; a failure on one render
  falls back for that render only. The `engine` line is the switch that turns Modular off. The
  fallback has to sit inside the engine because `playFrom` does not catch a rejection (facts).
  The preview keeps swallowing failures. Once the fallback goes, a render failure surfaces as
  the page decides.

**J12. Calls are asynchronous and one at a time.** [glue]

- *Recommendation:* every call returns a promise, even while the render runs synchronously on
  the main thread, and the loader runs one at a time. Moving the render into a Web Worker
  later (the map's fog) then changes no page code.

**J13. What must stay true for "what you hear is what Download .mid contains".**

1. Play and Download .mid get their bytes from one function with the same options and the same
   ABC text. [page]
2. Whatever writes the bytes (abcjs 6.7.2 pinned, or J2 (c)), both paths use it. [page]
3. Modular plays every note in those bytes, from every channel, at the file's timing and the
   velocity abcjs gave it. That needs the soundfont node to honour velocity (ticket 09), or the
   beat accents and the chord track's lower level vanish. [nodes]
4. Two voices sounding the same pitch both sound. Today's allocator merges them; per-channel
   note identity is a policy for tickets 05 and 06, not this one. [nodes]
5. The claim is about the notes, not about General MIDI. The piano ignores program changes,
   controllers and pitch bend, so a clip with `%%MIDI program 40` sounds as a piano, where the
   synth plays a violin today. What a piano does with channel 10's drums is ticket 09's.
6. A browser render equals a native render of the same bytes, sample for sample or within
   ticket 08's tolerance.

#### Facts still needed from midishare

1. A count over stored clips: staccato (`.` or `!staccato!`) and at what `Q:`; tuplets of 7 or
   more; pitches below 16 or above 127; `%%MIDI program`, `drum`, `channel 10`, `chordprog` or
   `bassprog`; quarter-tone accidentals; trailing rests; chord symbols; and the longest clip's
   duration in seconds (J3, J7).
2. Whether midishare would change Download .mid's bytes by owning or patching the writer
   (J2 (c), J3).
3. Whether a clip ending in rests should keep ending where it does today (J6's padding).
4. How long a first Play or first note press may wait, how large a download is acceptable, and
   whether warming up at the first gesture or at page load is acceptable (J9).
5. Whether the fallback may live in the engine file, given `playFrom`'s missing catch (J11).
6. How the end-to-end tests will detect that a note sounded once no sample files are fetched.
7. Whether the asset layer serves `.wasm` as `application/wasm` (for streaming compilation)
   and caches hashed assets long; this may belong to ticket 03.
