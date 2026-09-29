# Can a node's ports change after it joins a graph?

Type: grilling
Status: resolved

## Question

This is a concrete, already-found conflict rather than a speculation. `kernel/include/modular/Node.h:56`
states the rule and `addInput`/`addOutput` assert it:

> Ports are reconfigurable while authoring but frozen once the node joins a Graph —
> variable-arity nodes treat arity as construction-time config, and changing it is a graph
> edit (replace the node), never a runtime mutation.

Plugin hosting breaks this. Ableton's Configure gesture — plugin already wired, user now
ticks parameter #47 for automation — requires appending a value port to a node that is
already in the graph. Under today's rule that means *replacing* the node, which loses every
connection around it (they hold the old `NodeId`), churns the plugin's DSP state, and
destroys every automation lane. And `Graph` has no deletion at all, so "replace the node" is
not merely awkward, it is currently unimplementable.

Decide:

- Is **additive-only** reconfiguration after graph-add the ruling? Appending a port keeps
  existing indices valid, which makes it moderate-class. Removing or reordering collides
  head-on with the fragility sprint 2 recorded — "a connection stores an outer port *index*,
  resolved through `boundaryIds()`... stable only because boundary nodes cannot be deleted
  or reordered" — and with deletion not existing.
- Does the freeze flag go away, relax to append-only, or stay with an explicit escape?
- What invalidates the compile, and what does an `Engine` do when its graph's port set has
  grown underneath it?
- Does this change the settled "ports frozen at compile" line, or only the narrower
  "frozen at graph-add" rule the code implemented? CLAUDE.md and `Node.h` currently
  disagree slightly on which of those is the law.
- Does the same ruling apply to a Supernode's derived face, which already stays editable
  after joining a graph? If derived faces can already grow, why can't primitive ones — is
  there a real asymmetry or an accident?

The layer split is already agreed and shouldn't be re-litigated: **kernel** owns that ports
exist and can be declared; the **node** owns which plugin parameters map to ports; the
**app** owns the Configure gesture.

## Answer

Yes — and the ruling reached far past the question, because "a port can be deleted" is
unanswerable while a port's identity *is* its position. Full record in ADR-0004.

**Params are not ports.** The ticket's framing (a Configure gesture appending a port) was
first challenged from the other side: a plugin exposes its parameter list at `initialize`,
so a node could declare one value port per parameter at construction and let the view hide
the ones nobody automates — no kernel change at all, and the project's own test says a
kernel change needs an application that cannot be expressed without one. That is dead, and
Serum kills it: its reverb, compressor and EQ sections switch in and out, each with its own
parameters, so there is no complete parameter list to read at load time. Port creation is a
command; the UI button is one caller of it. Which parameter maps to which port is the node's
private business and never the kernel's.

**Ports are mutable while authoring; nothing is frozen.** `Node::frozen_` and its two
asserts are deleted, and with them the `Node.h`-vs-CLAUDE.md disagreement about whether the
law is "frozen at graph-add" or "frozen at compile". Neither: the *compiled form* is an
immutable snapshot and the authored graph stays editable underneath it. Note the deleted
rule was `assert`-only, so it was never enforced in a release build anyway.

**Deletion is RESTRICT** — the database framing, ruled by the user. Deleting a port that
still carries a wire is an error; the caller unwires first. CASCADE would have the kernel
silently destroy wiring nobody asked it to touch, and SET NULL cannot even be spelled — a
wire with a null end is not a graph. It is also the only one of the three where the kernel
does not have to decide what the user meant, which is the same reason fan-in is rejected.
The cascade a DAW wants ("delete track" unwires from master, then deletes) is written once,
visibly, in `apps/daw/`. A `connectionsOf()` query and a force-delete built on top of it are
convenience, explicitly secondary.

One rule, three cases: port deletion, node deletion, boundary-node deletion. **Node deletion
therefore lands with this ruling** — sprint 2 parked it pending stale-exposure semantics,
and this is that settlement.

**Ids replace positions as identity, everywhere.** A wire becomes `(NodeId, PortId)`.
Position survives only as display order, and `PortSpec` carries the id so a view can walk
positions and still name a port durably. An intermediate proposal — ids for authoring,
positions for execution, with the compile translating — was rejected: it created two
coordinate systems and then needed an invariant ("a node must never resolve its own ports
during `process()`") to stop them colliding. A rule you have to remember is worse than a rule
you cannot break. The performance objection to ids in `process()` was checked against
`Engine.cpp:29-71` rather than assumed: `Wiring` is per-node vectors indexed by position, so
an id adds one array read *once per port per block*, not per sample. Same shape as ADR-0002's
falsified hot-path argument.

`NodeId` stops being an index into `nodes_` for exactly the reason ports did — node deletion
would otherwise slide every later node down.

**Ids are graph-scoped**, minted by one counter per graph for both nodes and ports. This is
the load-bearing choice and it was arrived at by elimination:

- *Node-scoped* (a counter per node) collides on a supernode's face. Two boundary nodes each
  give their single port id 0, so two outer ports claim one id. Rescuable only by borrowing
  the boundary node's `NodeId` as the outer `PortId` — type confusion at precisely the spot
  ADR-0001 already flagged as thin, and rejected by the user on sight.
- *Process-global* has no collisions but breaks the snapshot rule: a supernode owns its inner
  graph by value, so a saved supernode placed into another project arrives with ids from a
  foreign counter, and renumbering is **impossible** — a node keeps its port ids in private
  members (`minuend_`, `subtrahend_`) that nothing outside can rewrite.
- *Graph-scoped* solves both. Two boundary nodes in one inner graph draw from one counter, so
  a supernode's outer port id simply **is** the backing boundary node's port id — nothing
  borrowed, nothing converted. And an inner graph is a closed namespace, so a snapshot drops
  into any project untouched.

That impossibility of renumbering is the whole argument: **an id must be correct the moment
it is minted.** Ports are declared in the constructor, so the node must have its graph before
the constructor body runs, so construction moves into the graph:
`graph.addNode<GainNode>(1)` replacing `graph.addNode(std::make_unique<GainNode>(1))`. The
name stays `addNode` — `emplace` is STL jargon for an implementation detail, while `addNode`
is the domain word and the established `addNode`/`addInput`/`addOutput` family. 121 call
sites, every one a compile error until fixed.

The counter is a separately allocated **`IdSource`** the graph owns, not a `Graph*` held by
each node: `Graph` is move-constructible and a `Supernode` holds one by value, so moving a
graph would leave every node inside pointing at a dead address.

**Container and recipe coexist, and ticket 02 had already named the recipe.** A detour worth
recording: the flat model — a supernode as a recorded command sequence replayed into the
outer graph, which then owns every id — dissolves this entire ticket's difficulty, since
nothing is constructed until replay and there are no pre-minted ids to renumber. It was not
taken as a *replacement*, because it costs structural encapsulation: with one flat node list
nothing prevents a wire reaching into the middle of a rack, and an Ableton rack genuinely
cannot be routed out of. The resolution is that both already exist. **Supernode** is the
container — nested graph, real boundary, erased at compile. **Template** is the recipe — a
plain function issuing commands, ruled in ticket 02, living in `apps/daw/`; its nodes land
flat and the graph mints their ids. An Ableton rack is a template on disk and a supernode
once in the project, which is why editing the file leaves the instance alone. Pick per case;
the kernel learns no notion of group membership, and the enforced-vs-presentational grouping
question never needs an answer.

**Sharpened for ticket 10**, which is where they belong rather than here: recompile must
re-`prepare()` every node, and must preserve node state. Both were "probably" before; the
node object is now shared between an editable graph and a running snapshot, and a plugin node
cannot be rebuilt on every port edit without churning the DSP state this ticket exists to
protect.

**Class: expensive — and unlike tickets 11 and 12, it earns the label.** Node constructor
signatures change, so the library genuinely churns: `Connection`, `Graph`, `Node`,
`Supernode`, `Engine`, plus the call sites. Three rulings in a row corrected *downward* is
not evidence that the next one will be; the class was re-derived here, not inherited.

Ruling only, per wayfinder's plan-don't-do default. Implementation is a claim on
`docs/roadmap.md`.
