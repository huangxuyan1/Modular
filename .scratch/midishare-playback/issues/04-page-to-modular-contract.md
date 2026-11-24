# What does the page hand Modular, and what does it get back?

Type: grilling
Status: open

## Question

The contract the site codes against. It must serve Play (a whole clip) and note preview (one
short note), and it must not quietly decide roadmap questions that are still open.

- **What goes in.** abcjs already makes Standard MIDI File bytes for Download .mid, and
  `MidiFile::parse` reads bytes from memory. Feeding Modular those bytes makes "what you hear is
  what you download" true by construction. The alternative is a note list built from abcjs's
  audio data, which the piano roll already uses. Check whether abcjs's MIDI file carries
  everything its synth plays (chord symbols played as accompaniment, `%%MIDI program`, grace
  notes, swing), because whatever it omits goes silent.
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
  one channel or two, as a `Float32Array` per channel. How the page learns the clip's length,
  including the release after the last note: a fixed tail, or render until the output falls
  silent, which is the "tail length" idea the roadmap ranks for plugins.
- **Errors.** Authoring and compile errors throw in Modular. Does one reach JavaScript as an
  exception with Modular's message, or as a result value?

The instrument tickets decide whether sample data also crosses this boundary; this ticket
leaves room for it rather than deciding it.
