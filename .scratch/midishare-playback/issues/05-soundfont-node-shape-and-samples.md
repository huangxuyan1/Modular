# Is the soundfont node one primitive or a supernode of voices, and where do its samples live?

Type: grilling
Status: resolved

## Question

The user asked for "a soundfont node that reads a piano soundfont". From outside, a supernode
is indistinguishable from a primitive node (`CONTEXT.md`), so either shape satisfies that; the
choice is about what is inside, and it decides where the samples live. The node must be a
reusable part of the node library that a DAW can use later, not midishare-specific code.

**The shape.**

- **A supernode:** MIDI-in boundary → `VoiceAllocatorNode` → N sample-playing voices →
  `MixNode` → audio-out boundary. This follows the stated composition rule: polyphony is "a
  `VoiceAllocatorNode`, several of these, and a `MixNode` — not a bigger synth" (`MidiNodes.h`).
  Every voice needs the same samples, and the allocator cannot see when a voice's release has
  ended (see [How does a released voice keep sounding until its release ends?](06-release-survives-voice-allocation.md)).
- **One polyphonic primitive** that reads the soundfont and allocates its own voices. The
  samples have one owner and release is internal, but it goes against the composition rule,
  and a user cannot rewire its insides.

**Where the samples live**, if voices are separate nodes. Modular's rule is that stored node
content belongs to its node and is saved with it (`CONTEXT.md`, **Stored node content**).

- **Each voice owns a copy.** Keeps the rule, costs N times the memory and the save file.
- **One node owns the samples, and voices read them over a wire.** No port domain carries
  ownership of shared data. A pointer written into a `value` slot would type-check, but nothing
  would own, save or reload what it points at. A reference-carrying subtype is a kernel
  question.
- **Voices share an immutable bank by pointer, outside the graph.** Cheap, but the bank becomes
  storage the graph does not hold, which the terminal claim's derivability clause forbids,
  unless the bank is itself authored content saved once.

**Persistence**, in either shape: is the soundfont saved inside the graph as stored node
content (the whole file, or only the preset in use), or named by reference, which is
out-of-band storage? Check against `docs/graph-persistence.md`: what is saved, once or N
times, and what a reload rebuilds.

Name the trade-off, and whether the answer is durable enough for an ADR.

## Answer

**One polyphonic primitive node that owns the soundfont file's bytes as its stored node
content.** Its voices are internal and read the node's one parsed copy. The file is saved once,
inside the graph, as loaded. Nothing changes under `kernel/` or `workspace/`, and no ADR is
needed. Release and stealing become the node's own policy, which
[How does a released voice keep sounding until its release ends?](06-release-survives-voice-allocation.md)
now decides.

Every decision below was decided under the node-policy delegation in `AGENTS.md`,
2026-11-24. Like every planning record, it is provisional until its planning PR merges.

### D1. Shape: one polyphonic primitive, not a supernode of voices

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **The samples get one owner with no new mechanism.** Each way separate voices could share
  them fails here. Copies cost N times the memory and the save file: a 20 MB piano in 16
  voices is 320 MB in memory and 640 MB of hex in a save file, past its 256 MiB limit. A wire
  carrying a reference is an out-of-line handle: no port domain says who owns what it points
  at, or who saves and reloads it, and the kernel contract defers such handles
  ([ADR-0002](../../../docs/adr/0002-event-payload-is-fixed-inline-32-bytes.md),
  `docs/project-foundations.md`), so it is a kernel question. A store outside the graph is
  out-of-band storage, which the terminal claim forbids. And a voice cannot name the node
  holding the samples, because no node names another (`CONTEXT.md`, **Compile**).
- **A supernode cannot be saved today.** `saveGraph` throws "save: supernodes and boundary
  nodes are unsupported", and the user deferred saving a supernode inside a parent graph
  ([graph-editor planning](../../graph-editor/planning.md), round 2, Q7). A primitive saves
  through a node codec in `nodes/`, with no `workspace/` change. This keeps open the saved-graph
  option of
  [What does the page hand Modular, and what does it get back?](04-page-to-modular-contract.md).
- **A soundfont's voices depend on each other.** A release outlives its note-off, and
  SoundFont 2's exclusive class lets one note cut another. Inside one node the voices see each
  other. Across nodes, the allocator would need a wire back from every voice, and a cycle is a
  compile error.
- **Plugin standards are the requirements oracle** (`docs/roadmap.md`). A VST3 or CLAP
  instrument is one polyphonic unit that takes note events, and CLAP sends a voice's end back to
  the host as an event because only the instrument knows it. A hosted soundfont plugin will
  look exactly like this node from outside.

**The cost:** nobody can rewire its insides, for example to put a filter of their own on each
voice. That is accepted. SoundFont 2 defines its own voice (sample, envelope, filter,
modulators), so the inside belongs to the format, not to the user. Everything after the voices,
such as level, EQ and reverb, stays ordinary wiring on the node's output. A composed form, voice
nodes sharing samples, can be added beside this node later if a sharing mechanism is ever built.
It would be another node, and this one would not change.

**Also rejected:** a primitive that owns a nested `Engine` of allocator, voices and mixer, the
form roadmap ticket 04's rate probe used. Its inner graph is as hidden as a primitive's
insides, and it adds an `Engine` per node for no gain. A generic sampler with pluggable format
readers is an abstraction beyond a concrete need (`AGENTS.md`); another format later is another
reader or another node.

**Not dynamic polyphony** (roadmap tail): the voices are a fixed pool, allocated at `prepare()`,
and no subgraph is ever spawned.

**A conflict, resolved.** `SineSynthNode`'s comment in `MidiNodes.h` says "Polyphony is a
VoiceAllocatorNode, several of these, and a MixNode — not a bigger synth", and the
graph-editor spec and `docs/graph-editor.md` say the same. None of them is a rule for every
instrument. The spec says it of the editor's existing synthesis, which that milestone was told
to use rather than expand, and the docs describe that synthesis. The comment is code, which is
not an accepted decision (`AGENTS.md`), and the allocator's own comment says that a different
policy "is simply a different node". Polyphony stays out of the kernel either way. The
implementing ticket rewords the comment so it stops reading as a rule for the whole library.

### D2. The node's face

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **Ports:** one input, `midi` (`event<midi>`), and one output, `out` (`audio<2>`). The output
  is stereo because SoundFont 2's voice is stereo: it has pan, and linked left and right
  samples. abcjs's buffer is stereo today too. Whether the page gets one channel or two is the
  contract ticket's.
- **No value inputs.** Level is a gain node's job, as `MixNode` already says. It also keeps the
  node clear of roadmap ticket 13 (an unwired `value` input reads zero).
- **Omni:** notes on every MIDI channel play the one preset. Per-channel presets, a
  multitimbral General MIDI player, can be added later without changing this.
- **A program change does not switch the preset.** The preset is an authored setting (D3), as
  today's instrument ignores program changes (`docs/node-io.md`). Following program changes can
  be added later as an option, for example if the stretch goal wants it.
- **Zero latency, block-size invariant:** a note starts at its event's offset, voice state
  advances per sample, allocation depends only on the event stream, and `process()` allocates
  nothing.

### D3. Settings

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **Preset:** the bank and program of one preset in the loaded file. When content is loaded,
  it defaults to the file's lowest bank and program: 0 and 0, the acoustic grand piano, in a
  General MIDI set, and the only preset in a single-piano file. Naming a preset the file lacks
  is an authoring error, and throws.
- **Voice limit:** the size of the fixed voice pool. Its default, what happens when it runs out
  and what it counts (a note with stereo samples plays two) are the release ticket's.
- Changing either is an authoring edit that takes effect at the next `prepare()`.

### D4. Where the samples live

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **In the node, as its stored node content:** the soundfont file's bytes, as loaded. The
  node parses them when it is built or when a file is loaded into it, which is authoring time,
  as `MidiFile` reads MIDI. It never parses in `prepare()` or `process()`.
- **`prepare()` does not read the soundfont again.** It derives what depends on the sample rate
  and resets the voices. So a new `Engine` on the same graph reuses the parsed soundfont,
  whatever roadmap ticket 10 rules about state across a recompile.
- **Each node owns its own copy.** Two soundfont nodes loading the same file hold two copies.
  Sharing between nodes needs a reference on a wire or storage outside the graph (D1). It stays
  out until a real need sends it to the roadmap.
- **Parse from bytes in memory**, as `MidiFile::parse` does. Reading a path is a desktop
  convenience beside it, as `MidiFile::read` is, so the web build needs no filesystem. The node
  is its own library beside `modular_midi`, so a pipeline with no music in it links none of it.
- **The node library ships no soundfont.** "A piano by default" is the client's default:
  midishare's graph loads the piano the planning hub picks from the research in
  [Which soundfonts ship, in what format, under what licence and at what size?](07-which-samples-ship.md).
  So no licensed asset enters Modular's node library. A node with no content is not ready, like
  an empty `MidiFileNode`: `prepare()` throws, asking for a soundfont first.

### D5. Persistence

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

- **Saved inside the graph, once, as the file's bytes**, in the node's `CONTENT`, with the
  preset and voice limit in its `SETTINGS`. It is a new codec kind in
  `modular_primitive_persistence` (a working name: `modular.soundfont/1`). The generic
  `MODULAR_GRAPH` container is unchanged
  ([graph persistence](../../../docs/graph-persistence.md)).
- **Not by reference.** A path or URL is out-of-band storage, which the terminal claim
  forbids. The editor's round 2 record, made from the user's answers, says the same: "A
  filename alone is not a substitute for owned content"
  ([graph-editor planning](../../graph-editor/planning.md)). A loaded file becomes owned content
  at once, and its path is not saved, as with `MidiFileNode`.
- **The whole file, not only the preset in use**, for three reasons. The preset is a setting
  the author can change after loading, which a trimmed file would forbid without the original.
  A later version of the node can honour more of the format without the file, so
  [Which part of the SoundFont format does the node read?](09-soundfont-format-subset.md) can
  start small and grow with no save-format change; this is why the node keeps the file and not
  its parsed meaning, unlike `MidiFileNode`, whose parsed meaning is complete. And cutting one
  preset out of a file is a licence question: the samples research found FluidR3_GM's MIT
  licence allows it with the notice kept, and the site ships such a single-preset extract. An
  author who wants a small graph loads a small file; cutting a file down is a tool's job,
  outside the node. Keeping the file whole also keeps any notice its header carries with the
  samples.
- **What a reload rebuilds:** the codec parses the saved bytes with the same parser as a load,
  and a file that no longer parses fails Open with an explicit error. Parsed zones, tables that
  depend on the sample rate, and voice state are derived and never saved.
- **A limit follows.** `CONTENT` is hex, two characters per byte, and `MODULAR_GRAPH 1` caps a
  file at 256 MiB, so a soundfont over about 128 MiB cannot be saved in a graph today. The node
  sets no limit of its own. A denser encoding would change the format under `workspace/`, a
  roadmap question if a DAW needs it. midishare's file is far below it: its host serves at most
  25 MiB per file.

### D6. No ADR, and no glossary change

*Decided under the node-policy delegation in `AGENTS.md`, 2026-11-24.*

**No ADR.** An ADR needs a decision that is hard to reverse, surprising and a real trade-off.
This one is a real trade-off, but it is one user-space node, reversed by adding another, and it
touches neither the kernel nor the workspace. The lasting question beneath it, whether a wire
may carry a reference to shared data, is a kernel question this map does not open; if a DAW
need reopens it, it goes to the roadmap as a claim. A reader would be surprised by the
`SineSynthNode` comment, so the fix goes there (D1).

**No `CONTEXT.md` change.** Soundfont, preset and voice are node-library words. They belong in
`docs/node-io.md` when the node is built. One collision to avoid: this ticket's question calls
shared sample storage a "bank", but in a SoundFont a bank is part of a preset's number (bank and
program). Say "the node's samples" or "its content" for the storage, and keep "bank" for the
SoundFont meaning.

### What this leaves to other tickets

- **The release ticket**, [How does a released voice keep sounding until its release ends?](06-release-survives-voice-allocation.md),
  is narrowed to the node's own voice policy, and its question is rewritten to match.
- **The subset ticket**, [Which part of the SoundFont format does the node read?](09-soundfont-format-subset.md),
  gains room: since the file is kept whole, a small first subset costs no save-format change
  later.
- **The contract ticket**, [What does the page hand Modular, and what does it get back?](04-page-to-modular-contract.md),
  is not decided here. Inputs from this answer: the node takes the soundfont as bytes, from
  wherever the page gets them; it outputs stereo; and its voices end by themselves, so a
  render-until-silent contract could read silence from the output with no tail API. A soundfont
  inside a saved graph doubles in size as hex before compression.
- **The samples ticket**, [Which soundfonts ship, in what format, under what licence and at what size?](07-which-samples-ship.md),
  resolved while this one was open, and the two agree: a SoundFont 2 file read at authoring
  time, owned by the node. Which piano becomes the default is still the planning hub's, after
  [Measure the FluidR3_GM piano before it becomes the default](10-measure-the-fluidr3-piano.md).
  Nothing here depends on that choice: a single-preset extract loads with its one preset
  selected (D3).
