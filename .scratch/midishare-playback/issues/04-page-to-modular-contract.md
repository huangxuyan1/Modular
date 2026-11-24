# What does the page hand Modular, and what does it get back?

Type: grilling
Status: open

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
