# Ports and nodes have graph-scoped ids, and deletion is RESTRICT

A port's identity stops being its position. `Connection` names ids on both ends, `NodeId`
stops being an index into `nodes_`, and both ids are minted by one counter per graph:

```cpp
struct Connection {
  NodeId srcNode;  PortId srcPort;
  NodeId dstNode;  PortId dstPort;
};
```

Ports become mutable while a node sits in a graph — added and deleted by explicit command.
`Node::frozen_` and its two asserts are deleted. Deleting a port, a node or a boundary node
that still carries a wire **throws**; the caller unwires first.

The question asked was whether ports can change after a node joins a graph. They can. But
"a port can be deleted" is unanswerable while a port *is* its position, so the ruling is
mostly about identity.

## Why positions fail

A wire stores `srcPort` / `dstPort` as offsets into `outputs()` / `inputs()`. Delete port 1
and every later port slides down one, so a wire that said "port 2" now addresses what used
to be port 3. The cutoff automation drives reverb mix and nothing errors. `NodeId` has the
identical defect one level up — `Graph::addNode` returns `nodes_.size() - 1` — so allowing
node deletion while node references are positional reproduces the bug wholesale.

Sprint 2 already recorded the symptom: ADR-0001 notes that a supernode's outer port index is
"stable only because boundary nodes cannot be deleted or reordered." This ruling removes that
precondition, so it had to remove the fragility with it.

## Why graph-scoped, and why construction moves into the graph

**An id must be correct the moment it is minted, because renumbering is impossible.** A node
keeps its own port ids in private members:

```cpp
SubtractNode(Graph& g) : Node(g) {
  minuend_    = addInput(PortType::audio(1), "a");
  subtrahend_ = addInput(PortType::audio(1), "b");
}
```

Nothing outside the node knows `minuend_` exists, so no later pass can rewrite it. That one
fact eliminates every scheme that renumbers after the fact, and it forces the node to hold a
graph before its constructor body runs:

```cpp
graph.addNode<GainNode>(1);                     // was addNode(std::make_unique<GainNode>(1))
```

Graph scope is then what makes a supernode's face work without a special case. Two boundary
nodes in one inner graph draw port ids from the same counter, so they differ, and the
supernode's outer port id **is** the backing boundary node's port id — nothing borrowed,
nothing converted. An inner graph is a closed namespace, so a supernode snapshot drops into
any project unchanged.

The counter is a separately allocated `IdSource` owned by the graph, never a `Graph*` stored
in each node: `Graph` is move-constructible and `Supernode` holds one **by value**, so moving
a graph would leave every node inside pointing at freed memory.

## Considered Options

- **Port-per-parameter, so ports never need to change** — rejected, and it was the first thing
  tested, since it needs no kernel change at all. A plugin publishes its parameter list at
  `initialize`, so a node could declare one value port per parameter and let the view hide the
  unautomated ones; "hundreds of ports" is UI clutter, and clutter is a view problem in a
  project where views are projections. Killed by the fact, not the aesthetics: Serum's reverb,
  compressor and EQ sections switch in and out with their own parameters, so no complete list
  exists at load time. Params are the plugin's knobs, ports are wire endpoints, and the
  mapping between them is the node's private business.
- **Additive-only — append ports, never delete** — rejected. It keeps every existing index
  valid and dodges deletion entirely, and a recycling scheme (all VST3 parameters are the same
  normalized type, so a freed port slot could be handed to the next mapped knob) makes it look
  sufficient. Rejected because it decides for the user: the app would have to arrange its
  affairs around a kernel that cannot delete. It also rested on a conflation — unmapping a
  parameter and deleting a port are unrelated events, and a port with no wire is an ordinary
  state.
- **CASCADE on delete** — rejected: the kernel would silently destroy wiring nobody asked it
  to touch. The cascade a DAW wants ("delete track" unwires it from master, then deletes) is
  one function in `apps/daw/` where a reader can see what it destroys. **SET NULL** cannot be
  spelled — a wire with a null end is not a graph. RESTRICT is the only one of the three where
  the kernel never has to decide what the user meant, which is the argument that already
  rejected fan-in. A `connectionsOf()` query and a force-delete built on it are convenience,
  and force-delete is CASCADE *opted into at the call site*, which is a different thing from
  the kernel choosing it.
- **Ids for authoring, positions for execution**, translated by the compile — rejected, and
  it was this ADR's own first recommendation. It leaves `process()` and every node in the
  library untouched, which looked like the cheap answer. But two coordinate systems need an
  invariant to keep them apart — a node would have to cache its port positions in `prepare()`
  and never resolve them during `process()`, because deleting a port changes the node's list
  immediately while the engine still runs a snapshot compiled against the old one. A rule you
  must remember is worse than one you cannot break. The performance case for positions was
  then checked rather than assumed: `ProcessContext::Wiring` is per-node vectors indexed by
  position, so an id costs one extra array read *once per port per block*, not per sample —
  the same measurement that falsified ADR-0002's hot-path argument.
- **Node-scoped port ids** (a counter per node, constructors untouched) — rejected. Fine for
  wires, since `(NodeId, PortId)` is unique, but a supernode's face collides: two boundary
  nodes each give their single port id 0. Rescuable only by using the boundary node's `NodeId`
  as the outer `PortId`, which conflates two identity kinds at exactly the site ADR-0001
  flagged as thin.
- **Process-global ids** — rejected. No collisions, and the boundary node's own port id would
  serve directly. But a supernode owns its inner graph by value, so a saved supernode placed
  into another project arrives carrying a foreign counter's ids, and the required renumbering
  is impossible (above). Ids would also vary run to run, in a project whose verification method
  is byte-comparing output.
- **Supernode as a pure recipe — flat replay into the outer graph** — rejected as a
  *replacement*, adopted as a coexisting concept. Under it a supernode is a recorded sequence
  of `addNode`/`connect` calls replayed into the graph, which then owns every id; nothing is
  constructed until replay, so no id ever needs renumbering and this ADR's central difficulty
  evaporates. What it costs is structural encapsulation: with one flat node list, nothing
  prevents a wire reaching into the middle of a rack, and an Ableton rack genuinely cannot be
  routed out of. Restoring that would put group membership into the kernel and give `connect`
  a boundary-crossing rule. Unnecessary, because both concepts already exist — **Supernode**
  is the container, **Template** (ticket 02: "templates are functions, not a mechanism") is the
  recipe, a plain function issuing commands from `apps/daw/`. An Ableton rack is a template on
  disk and a supernode once in the project, which is exactly why editing the file leaves the
  placed instance alone.

## Consequences

- **Class: expensive, and this one earns it.** Node constructor signatures change, so the
  library churns for real: `Connection`, `Graph`, `Node`, `Supernode`, `Engine`, plus 121 call
  sites of the form `graph.addNode(std::make_unique<T>(…))`. Every one is a compile error until
  fixed, so nothing half-works silently. Recorded deliberately against the pattern: ADR-0002
  and ADR-0003 were both catastrophic-on-paper and moderate in fact, and three downward
  corrections in a row would be a bad reason to expect a fourth. The class was re-derived here.
- **Node deletion lands with this ruling.** Sprint 2 parked it pending stale-exposure
  semantics; RESTRICT plus ids is that settlement, and ADR-0001's known fragility is retired.
- **`Node::frozen_` is deleted, and so is the "frozen at graph-add" rule** it enforced —
  `assert`-only, so never enforced in a release build regardless. CLAUDE.md's "frozen at
  compile" wins the disagreement with `Node.h:56`, restated: nothing is frozen; the *compiled
  form* is an immutable snapshot and the authored graph stays editable underneath it.
- **`PortSpec` carries its id**, so a view can walk `inputs()` in display order and still name
  a port durably. Position survives as *ordering* — display, and homogeneous port sets like a
  mixer's inputs, where the ordering lives in the node's own `std::vector<PortId>` and index 3
  means input 3 there. Ordering was never identity: a subtract node's operand roles come from
  its member names, not from which port it declared first.
- **The engine compacts sparse ids at compile.** A node mapped and unmapped ten thousand times
  has ids running to 10,000 with twenty alive; the compile chooses the table shape. Invisible
  outside the engine, because no node ever names another node — unlike the authoring/execution
  split rejected above, this translation is unobservable.
- **Requirements handed to map ticket 10 (`prepare` on recompile):** recompile must
  re-`prepare()` every node, and must preserve node state. The node object is now shared
  between an editable graph and a running compiled snapshot, and a plugin node cannot be
  rebuilt on every port edit without churning the DSP state this ruling exists to protect.
- **Nothing here touches the purity grep.** Ids, deletion and RESTRICT name no medium.

**When this reopens.** Serialization must round-trip ids and the `IdSource` high-water mark,
so the deferred `value<T>`/`event<T>` parameterization question now has a companion: ids
minted at authoring time are part of the program text, not an implementation detail. And if a
supernode ever needs to be edited *in place* by many collaborators, graph-scoped ids meet the
multi-user question that is out of scope for this roadmap.

Ruled by `.scratch/roadmap/issues/03-variable-arity-after-graph-add.md`.

## Shared editing clarification (2026-09-13)

While grilling the first graph editor, the user reaffirmed the prior deletion
ruling and clarified that the generic delete-with-connections operation belongs
to Modular, so the GUI invokes shared behavior. The earlier `apps/daw/` example
does not require each frontend to implement its own generic cascade: shared
Modular editing functionality can explicitly disconnect the named node's wires
and then remove it. Media-specific operations such as "delete track" remain app
policy; ordinary kernel deletion remains RESTRICT.

This records the placement of an explicit operation, not a change to the default
deletion rule or a claim that the shared command layer is implemented. Undo/redo
likewise belongs to Modular's shared editing facilities and has lower priority
than the first editor's essential authoring/audio/persistence path.
