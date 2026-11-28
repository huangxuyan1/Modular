# What counts as a "recognizable DAW"?

Type: grilling
Status: resolved

## Question

The terminal claim is:

> A recognizable DAW runs on the engine while (a) the kernel-purity grep still returns
> nothing, and (b) the DAW stores nothing that isn't derivable from the authored graph.

Halves (a) and (b) are sharp — (a) is already a ctest test, (b) is testable by reloading a
project from the graph alone and comparing the display. "Recognizable" is the soft word, and
while it stays soft the roadmap has no finish line.

Decide the acceptance test:

- What is the minimum feature set at which a sceptic cannot say "that's a toy"? Candidates,
  each of which needs an in-or-out ruling: multitrack arrangement, a piano roll, a mixer
  with sends and buses, automation lanes, plugin hosting (already in scope), recording,
  loop/punch, freeze/bounce, undo.
- Which of those are load-bearing for the *bet* versus merely conventional? Automation, for
  instance, stresses the value domain and sample-accurate parameter changes; a piano roll
  mostly stresses the view layer. They are not equally informative.
- How is (b) actually tested? Killing the app and reconstructing the full display from the
  graph is one option; another is diffing the app's in-memory state against a projection of
  the graph. Which, and does it become an automated test like the purity grep?
- Does "recognizable" require a *user other than you* to agree? A one-person judgement is
  cheaper but weaker evidence for a claim this load-bearing.

Feeds the terminus statement in `docs/roadmap.md`, and ticket 06 — a constraint mechanism
can't be scoped without knowing what the DAW must constrain.

## Answer

*Grilled 2026-11-27, three rounds.*

**"Recognizable" is a milestone, not the end.** The terminal claim is renamed the
**first-users milestone**: the point at which the user judges it a DAW and hands it to other
people. After it, each thing those people ask for joins the roadmap as an adversary app, the
map's existing method, rather than being designed up front. The feature set below is what
the milestone's task needs, built up one claim at a time and never set up all at once. The
two hard halves are unchanged: (a) the purity grep returns nothing, and (b) the DAW stores
nothing that isn't derivable from the authored graph.

**The test is a task, not a checklist.** A toy can tick a checklist; it cannot do a task.
From an empty project: record and write a 1–2 minute song with drums, bass, keys and one
audio track, mix it with effects and automation, export a WAV, close the app, reopen the
project, and find everything as it was left.

**Features the task needs.**

- **In:** multitrack arrangement of MIDI and audio clips on a timeline; a piano roll; a
  mixer with sends and buses; automation lanes; VST3 hosting, plus Modular's own
  instruments and effects; recording MIDI and audio; loop playback; offline bounce to WAV;
  undo and redo.
- **Out for now:** punch-in, freeze, comping, time-stretching, notation, video. Any of them
  can return after the milestone as a user's request.

Undo and automation stay in although both are expensive: without undo it is a toy, and
automation is what tests sample-accurate parameter changes.

**Load-bearing versus conventional** is answered by the order, not by the list. Every
feature on the list is in; the ones that stress the bet hardest come first (see *How it is
built*). The piano roll and recording mostly stress views and devices, so they come later.

**Live editing is required.** An edit made during playback (moving a note, changing a mix
level, swapping a plugin) is heard within a few blocks and without dropouts. That is the
largest open technical risk, and it lives in tickets 07, 08 and 10. Offline rendering stays
the primary verification for everything structural.

**Scale bar.** At least the task's song: 16 tracks, 8 plugin instances, 5 minutes long, at
48 kHz with a 256-sample buffer, playing without dropouts on the development desktop.
Without a number, two tracks and one sine plugin would pass.

**How half (b) is tested.** Every view is a pure function of the authored graph and its
presentation data (`CONTEXT.md`, **View**); this was already ruled. The test saves a project,
reopens it in a fresh process and compares every view's serialized state byte for byte.
Window size and scroll position don't count. It runs automatically in CTest, beside the
purity check. Comparing screenshots was rejected: it is flaky, and it cannot say which piece
of state escaped the graph.

**Who judges.** The user. Round 1 proposed that one other musician run the task unhelped as
the final acceptance; the user replaced that with the milestone itself. Other people come
after the user judges it a DAW, and their requests drive what follows.

**The DAW system and its GUI are two problems.** Every DAW claim is app-agnostic: proved
through scripts, a command line and text views, and never decided by a GUI. The milestone
needs both, though: a **DAW system** (`CONTEXT.md`) that passes the task, and a GUI good
enough for the user and others to make music in, since nobody makes music on a command line.
The GUI is planned as its own effort with its own quality bar, and which app carries it (the
Windows editor, a JUCE app, the web build) is decided there, not on this map.

**How it is built.**

1. **A walking skeleton first:** a project that is a graph, holding a few tracks of MIDI
   clips on a timeline, with play/stop and bounce to WAV through the soundfont node, driven
   from a command line with text views.
2. **Then one feature per claim, riskiest first.** This is the roadmap's own rule, proof
   dependency first and then blast radius: live editing (the command/edit API, ticket 07,
   and the realtime probe, ticket 08), automation, sends and buses, plugin hosting, undo.
   The user decides the exact order claim by claim.

**Feeds ticket 06.** What the DAW must constrain is now named: tracks, clips on a timeline,
mixer channels with their sends and buses, automation lanes and hosted plugins. Each must be
something a view finds in the graph, with no list held beside it. The walking skeleton
meets that question first, since even its text views must find its tracks.

**Left open.** The authored-versus-derived wording of half (b) is not settled here. A
plugin's opaque state is storable in the graph but not derivable from it (ticket 01), so the
wording stays with plugin-host node design.
