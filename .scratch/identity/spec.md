# Spec: Identity — graph-scoped ids, and deletion is RESTRICT

Status: ready-for-agent

Claim: **a port's identity is an id, not a position — so a node, a port or a boundary node
can be deleted from a wired graph and no surviving wire changes what it addresses.**

Falsified by: any delete-and-rewire sequence after which a connection delivers to a
different logical port than it did before, or after which the milestone render differs.

Ruled in ADR-0004 (`docs/adr/0004-ports-and-nodes-have-graph-scoped-ids.md`), from map
ticket `.scratch/roadmap/issues/03-variable-arity-after-graph-add.md`. This spec is the
implementation cut; it decides nothing the ADR left open.

## Problem Statement

A graph author cannot delete anything. `Graph` has no deletion at all — not a node, not a
connection, not a port — so every authoring mistake is permanent and every experiment
accumulates. That alone would be a missing feature. What makes it a defect is *why* it is
missing: a wire stores its endpoints as offsets into `inputs()` / `outputs()`, and
`Graph::addNode` returns `nodes_.size() - 1`, so identity and position are the same thing at
both levels. Adding deletion on top of that would not be incomplete, it would be **wrong** —
delete port 1 of a three-port node and every later port slides down one, so the wire that
said "port 2" now addresses what used to be port 3. Nothing errors. The cutoff automation
drives reverb mix.

The consequence reaches past convenience. Ticket 01's oracle work established that no plugin
can enumerate its params up front — Serum's reverb, compressor and EQ sections switch in and
out with their own params — so a node hosting a plugin must be able to create and delete
ports while it sits wired in the graph. Today `Node` forbids exactly that: `frozen_` asserts
that ports stop changing once the node joins a `Graph`. And sprint 2 shipped a known
fragility on the same fault line — ADR-0001 recorded that a supernode's outer port index is
"stable only because boundary nodes cannot be deleted or reordered."

So three separate things are blocked behind one unresolved question, and the question is not
"should deletion exist" but "what is a port".

## Solution

Identity moves to ids. `NodeId` and `PortId` are both minted by one `IdSource` per graph,
never reused, and used all the way into `process()`. A `Connection` names ids on both ends.
`PortSpec` carries its id, so position survives as *ordering* — display order, and
homogeneous port sets like a mixer's inputs — but never as identity.

Two things follow immediately, and both are the point rather than side effects. Ports become
mutable while a node sits in a graph: `Node::frozen_` and its two asserts are deleted, and
`addInput`/`addOutput` are joined by deletion. And deletion arrives for nodes, ports and
boundary nodes under one rule — **RESTRICT**: deleting anything that still carries a wire
throws, and the caller unwires first. The kernel never decides what the user meant, which is
the same argument that already rejected fan-in. A `connectionsOf()` query lets a caller see
what it would have to unwire; a cascade is one function in an app, where a reader can see
what it destroys.

Graph scope is what makes a supernode's face work with no special case: two boundary nodes
in one inner graph draw port ids from the same counter, so they differ, and a supernode's
outer port id **is** the port id of the boundary node backing it — nothing borrowed, nothing
converted. ADR-0001's fragility retires with the index it depended on.

An id must be correct the moment it is minted, because renumbering is impossible. That forces
node construction into the graph — `graph.addNode<GainNode>(1)` — so a node holds its graph
before its constructor body runs and keeps its own port ids in private members nothing
outside can rewrite.

## User Stories

1. As a graph author, I want to delete a node, so that a wiring experiment is not permanent.
2. As a graph author, I want to delete a connection, so that I can rewire without rebuilding
   the graph.
3. As a graph author, I want to delete a port from a node that is already in the graph, so
   that a node's port set can follow what the node is currently doing.
4. As a graph author, I want to add a port to a node that is already in the graph, so that I
   do not have to know a node's final shape at the moment I create it.
5. As a graph author, I want every wire I did not touch to keep addressing exactly the port
   it addressed before any deletion, so that deleting one thing cannot silently change what
   another thing does.
6. As a graph author, I want deleting a port that still carries a wire to fail loudly, so
   that the kernel never destroys wiring I did not ask it to touch.
7. As a graph author, I want deleting a node that still carries a wire to fail the same way,
   so that there is one deletion rule to remember rather than one per kind of thing.
8. As a graph author, I want deleting a boundary node that still backs a wire to fail the
   same way, so that a supernode's face cannot be hollowed out from inside.
9. As a graph author, I want to ask what connections touch a node or a port, so that I can
   unwire deliberately before deleting.
10. As a graph author, I want an id never to be reused after the thing it named is deleted,
    so that a stale reference fails instead of resolving to something new.
11. As a graph author, I want a port's display position to be free to change without any
    wire noticing, so that reordering a node's ports is a presentation decision.
12. As a node author, I want to declare ports in my constructor and keep their ids in my own
    members, so that nothing outside the node can rewrite what my ports mean.
13. As a node author, I want to name my ports by those members inside `process()`, so that
    reading the wrong port is a compile error rather than an off-by-one.
14. As a node author, I want to add and delete my own ports in response to whatever I am
    hosting, so that a plugin whose sections switch in and out can be wrapped in a node.
15. As a node author, I want deleting my port to leave my other ports' ids untouched, so that
    I do not have to re-derive my own members after every edit.
16. As a supernode author, I want a supernode's outer port ids to be stable across edits to
    its inner graph, so that a wire into a supernode survives work done inside it.
17. As a supernode author, I want to delete a boundary node and have the rest of the face
    keep its ids, so that removing one exposed port does not repoint the others.
18. As a supernode author, I want an inner graph's ids to mean nothing outside it, so that a
    saved supernode drops into any project unchanged.
19. As an application developer, I want a node id that stays valid for as long as the node
    exists, so that a view can hold a reference to a node across edits.
20. As an application developer, I want a port id I can put in my own tables, so that a view
    can annotate a port without inventing out-of-band identity for it.
21. As an application developer, I want to write my own delete-and-unwire cascade, so that
    what my "delete track" gesture destroys is visible in my code rather than the kernel's.
22. As an application developer, I want authoring failures to throw with a message naming
    what went wrong, so that a bad edit is diagnosable at the call site.
23. As the kernel maintainer, I want the compiled form to be an immutable snapshot while the
    authored graph stays editable underneath, so that "frozen" describes the compile rather
    than the authoring API.
24. As the kernel maintainer, I want the engine to compact sparse ids at compile, so that a
    node whose ports have churned ten thousand times does not cost a ten-thousand-entry
    table at runtime.
25. As the kernel maintainer, I want that compaction to be invisible outside the engine, so
    that ids are the only coordinate system anyone has to reason about.
26. As the kernel maintainer, I want the block loop to cost no more than it does today, so
    that identity is an authoring-time concern that the runtime does not pay for.
27. As the kernel maintainer, I want ids and deletion to name no medium, so that the purity
    grep still returns nothing.

## Implementation Decisions

All ruled in ADR-0004; this section states the cut, not new decisions.

- **One `IdSource` per graph, minting node and port ids from one counter.** Separately
  allocated and owned by the graph, never a `Graph*` stored in each node: `Graph` is
  move-constructible and `Supernode` holds one by value, so moving a graph would leave every
  node inside pointing at freed memory. Ids are never reused within a graph.
- **`Connection` names ids on both ends** — `{NodeId srcNode, PortId srcPort, NodeId
  dstNode, PortId dstPort}`. `NodeId` stops being an index into `nodes_`.
- **`PortSpec` carries its id.** `inputs()`/`outputs()` stay in display order; a view walks
  them in order and still names each port durably.
- **Construction moves into the graph:** `addNode<T>(args…)` constructs the node with the
  graph in hand and returns its `NodeId`. `addInput`/`addOutput` return `PortId`, which the
  node stores in private members. The old `addNode(std::unique_ptr<Node>)` goes away — this
  is the 121-call-site churn, and every one of them is a compile error until fixed, so
  nothing half-works silently.
- **`ProcessContext`'s six accessors take `PortId`.** Ids go all the way into `process()`;
  there is no authoring/execution coordinate split, because two coordinate systems would
  need an invariant a node author must remember, and a rule you must remember is worse than
  one you cannot break. Cost was checked rather than assumed: `Wiring` is per-node vectors,
  so an id costs one extra array read once per port per block, not per sample.
- **`Node::frozen_` and its two asserts are deleted**, along with the "frozen at graph-add"
  rule. Nothing is frozen; the *compiled form* is the immutable snapshot and the authored
  graph stays editable underneath it. `Node.h`'s class comment is rewritten to say so.
- **Deletion is RESTRICT for all three kinds** — port, node, boundary node. Deleting one
  that still carries a wire throws `std::invalid_argument` per the existing authoring-path
  convention. CASCADE and SET NULL were both rejected in the ADR.
- **Connection deletion is unconditional** — a wire is what RESTRICT protects, so removing
  one has nothing to protect.
- **`connectionsOf()` is the query that makes RESTRICT usable.** Convenience, not mechanism;
  a force-delete built on it is CASCADE opted into at the call site, which is a different
  thing from the kernel choosing it.
- **A supernode's outer port id is the backing boundary node's port id.** No conversion, no
  side table. `boundaryIds()` and the derived face keep working, but the face is addressed by
  id rather than by position, which is what retires ADR-0001's recorded fragility.
- **The engine compacts sparse ids at compile.** The compile chooses the table shape;
  `SourcePort` and the resolved-node tables stay positional inside `Flatten`, since the
  executed graph has no ids of its own. Unobservable outside the engine, because no node ever
  names another node.
- **Params are not ports.** Nothing in this cut hosts a plugin; the ruling is recorded here
  only because it is why port mutation must exist.

## Testing Decisions

A good test here observes external behaviour at one of the two seams the codebase already
has, and nothing else. Both are existing:

1. **The render seam** — build a `Graph`, construct an `Engine`, `process()` blocks, compare
   captured samples. Prior art: `"the milestone graph renders bit-identically with its clip
   section grouped"` and `"output is independent of block size"` in `tests/test_kernel.cpp`,
   both of which already compare full sample vectors across two builds.
2. **The authoring seam** — `Graph`'s public API and what it throws. Prior art:
   `"connect enforces exact type equality"` and `"input accepts only one connection; merging
   is a node's job"`.

**Explicitly not tested: the engine's id→position compaction.** ADR-0004 calls it
unobservable outside the engine, and asserting the table directly would make an
engine-internal into a tested contract — testing the mechanism instead of the claim. The
bit-identical render covers it: if compaction were wrong the samples would differ.

The falsifiable demo, at seam 1 — **it must fail on today's code**:

- A node declares three ports; wire the third; delete the first; render. The surviving wire
  still delivers to the same logical port, and the output is byte-for-byte what it was before
  the deletion. Under positional identity the wire re-addresses and the samples change.

Supporting tests, trivial cases first so a failure localizes rather than reporting "bytes
differ":

- Ids are unique across nodes *and* ports within one graph, and are not reused after deletion.
- Two boundary nodes in one inner graph have different port ids (the collision that killed
  node-scoped ids).
- Deleting a wired port throws; deleting a wired node throws; deleting a wired boundary node
  throws. Unwiring first, then deleting, succeeds.
- Deleting a connection then reconnecting elsewhere works, and the graph renders accordingly.
- `connectionsOf()` reports exactly the wires that RESTRICT would refuse over.
- A node adds a port after joining a graph, it is connectable, and the render reflects it.
- Deleting a port leaves the other ports' ids unchanged (the members a node holds stay valid).
- A supernode's outer port ids survive edits to its inner graph; deleting one boundary node
  leaves the remaining face's ids untouched, and a wire into a surviving outer port still
  delivers.
- Regression: the milestone graph, the grouped/nested supernode builds, and block-size
  invariance all still render bit-identically after the conversion.
- Kernel purity: `scripts/check_kernel_purity.sh` still returns nothing.

Style follows existing convention: doctest, one file per layer, test names as invariant
sentences, frame numbers that cannot align with block boundaries by accident (primes, odd
offsets), graphs built fresh per render, helper nodes local to the test file.

## Out of Scope

The cut list. Each of these is deliberately excluded, with the reason:

- **Serialization of ids and the `IdSource` high-water mark.** ADR-0004 names it as where
  this reopens, and it drags in the deferred `value<T>`/`event<T>` parameterization ruling.
  Nothing here saves anything.
- **The command layer.** Ticket 02 named `modular_workspace` as the owner of atomic edits,
  undo and stable-across-session ids; ticket 07 (command API cut) is open and unblocked. This
  cut puts ids and deletion in the *kernel*, below whatever 07 decides wraps them. No
  transactions, no undo, no atomicity.
- **`prepare()` on recompile, and state preservation across recompile.** ADR-0004 hands
  requirements to map ticket 10; that ticket is open. This cut does not make recompile
  mid-stream work.
- **Live editing while an `Engine` exists.** `Engine`'s existing contract stands: the graph
  must not be edited while the engine lives, and editing means building a new `Engine`. The
  editable-graph/immutable-snapshot framing describes the compile, and does not yet license
  concurrent edits.
- **Any plugin-hosting node.** Params-are-not-ports is the *motivation* for port mutation;
  no plugin node is built here.
- **Outer-port renaming and reordering** for supernodes (boundary-node configuration).
- **Port groups / the annotation layer** — ticket 06, still open.
- **Unconnected value defaults** — ticket 13, independent of this and unaffected by it.
- **PDC, cycles, threading.** Untouched.
- **Any UI or view work.**

## Further Notes

- **Ranking: expensive, and ADR-0004 records that this is the first of the four rulings that
  earns the label.** ADR-0002 and ADR-0003 were both corrected catastrophic → moderate, and
  the ADR notes deliberately that three downward corrections in a row would be a bad reason to
  expect a fourth. The 121 call sites are compile errors, not silent breakage.
- The claim closes per `map.md`'s claim anatomy: spec (this file), tickets, ADR (already
  written), demo + tests that fail if the claim is false, **close-out review**, CLAUDE.md
  updated, then a `Close: …` commit. The close-out review is what found sprint 1's fixed-size
  values bug and must not be skipped.
- CLAUDE.md's "Pre-coding decisions" entry for port declaration and the Supernode section's
  struck-through fragility note are already updated for ADR-0004; what still needs updating at
  close is the status line and anything describing `Graph`'s API.
- Vocabulary per `CONTEXT.md`: **port id**, **id source**, **primitive node**, **supernode**,
  **boundary node**, **authored graph**, **executed graph**, **compile**, **variable arity**,
  **param**. Avoid *port index*, *port number*, *slot*, *allocator*, *registry*, *id pool*,
  and *parameter* as a kernel term.
- `docs/roadmap.md` is a running artifact — this claim appends to it rather than waiting for a
  final write-up.
