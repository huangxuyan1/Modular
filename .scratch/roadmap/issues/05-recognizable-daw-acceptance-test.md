# What counts as a "recognizable DAW"?

Type: grilling
Status: open

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
