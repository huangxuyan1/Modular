# 02 — Port identity, port mutation, and the claim's demo

**What to build:** a graph author can add and delete ports on a node that is already wired
into the graph, and no wire they did not touch changes what it addresses. This is the claim
of the whole effort, and its demo lives here: a node with three ports, the third one wired,
delete the first — under positional identity the surviving wire re-addresses what used to be
a different port and the render changes; under port ids it does not.

Delivering that means identity moves into `process()`. A port id is what a connection names,
what `addInput`/`addOutput` return, what a node keeps in its private members, and what the
process context's accessors take. A port's position in `inputs()`/`outputs()` survives only
as display order.

This also deletes the rule that ports freeze when a node joins a graph. Nothing is frozen:
the compiled form is the immutable snapshot, and the authored graph stays editable underneath
it. The motivation is recorded in ADR-0004 — no plugin can enumerate its params up front, so
a node wrapping one must be able to create and delete ports while wired.

**Blocked by:** 01 — Node identity and node deletion.

**Status:** done

**This is a big-bang, deliberately.** Expand–contract was considered and rejected: ADR-0004
settles that a node takes its graph as a constructor parameter, and running old and new
constructor forms side by side would double every node in the library. So node constructor
signatures change, node construction moves into the graph, and roughly 121 construction sites
across the kernel tests, the node tests and the render CLI change with them. Every one is a
compile error until fixed — nothing half-works silently, which is the property that makes a
big-bang acceptable at this size. Budget the context window for the mechanical migration as
well as the design change.

- [x] Port ids come from the same per-graph `IdSource` as node ids, are never reused, and are
      unique across both kinds within one graph.
- [x] A port spec carries its id. `inputs()`/`outputs()` keep display order; nothing treats
      that order as identity.
- [x] `addInput`/`addOutput` return port ids, which a node stores in private members that
      nothing outside the node can rewrite.
- [x] Node construction moves into the graph, so a node holds its graph before its
      constructor body runs and its port ids are correct the moment they are minted.
- [x] `Connection`'s port ends name ids.
- [x] The process context's six domain accessors take port ids. There is no
      authoring/execution coordinate split — ids go all the way in, so there is no invariant
      a node author has to remember.
- [x] The frozen-on-graph-add rule and its asserts are gone, and the class comment that
      documented it is rewritten to say the compiled form is what is immutable.
- [x] A node adds a port after joining the graph; the new port is connectable and the render
      reflects it.
- [x] Deleting a port leaves every other port's id unchanged, so the members a node holds
      stay valid.
- [x] Deleting a port that still carries a wire throws; unwiring first, then deleting,
      succeeds.
- [x] `connectionsOf()` answers for ports as well as nodes.
- [x] **Falsifiable demo, must fail before the change:** a node declares three ports of one
      domain; wire the third; delete the first; render. The surviving wire still delivers to
      the same logical port and the output is byte-for-byte what it was before the deletion.
- [x] The engine compacts sparse ids at compile — a node whose ports have churned must not
      cost a table sized by its high-water mark. No test asserts the compaction directly; it
      is engine-internal and the bit-identical renders cover it.
- [x] Every node in the library and every construction site is migrated; the full suite and
      the render CLI build and pass.
- [x] The milestone render, the grouped and nested supernode renders, and block-size
      invariance all still produce byte-identical output.
- [x] `scripts/check_kernel_purity.sh` still returns nothing.

## Comments

**2026-08-07 — implemented.** `PortId` joins `NodeId` in `Ids.h`, minted from the same
per-graph `IdSource`; `PortSpec` carries its id; `Connection` names ids on all four ends;
`ProcessContext`'s six accessors take `PortId`. `Node::frozen_` and its asserts are gone and
the class comment now says the compiled form is what is immutable.

Node construction moved into the graph: `graph.addNode<T>(args…)` builds `T(*this, args…)`
and returns the `NodeId`. `Node(Graph&)` keeps only the graph's *id source* — never a
`Graph*`, which would dangle after a move — so `kernel/src/Node.cpp` is new (the one line
needing `Graph` complete) and is in CMakeLists.

**Where deletion ended up, and why it is not next to `addInput`.** Minting a port needs
nothing but the id source, so it stays the node's own business; removing one has to consult
the wires, and only the graph holds those. So `Graph::removePort(NodeId, PortId)` enforces
RESTRICT and then calls back through a new protected `Node::portRemoved(PortId)` hook, which
is where a node drops the members that named the dead port. Without that hook a node's
private port ids would silently outlive the ports (the first version of the demo crashed on
exactly this). A node initiating its own deletion needs a graph to say it through — the
command layer's job, ticket 07, out of scope here.

`Node::erasePort` is virtual so the two structural nodes can refuse: a boundary node's port
*is* its exposure, and a supernode's ports are its boundary nodes. Both throw with a message
naming the right gesture.

**Some of ticket 03 landed here, unavoidably.** Once a wire names a `PortId`, `Flatten`
could no longer index a face by position, so the face is addressed by id now:
`Supernode::boundaryFor(PortId)` finds the backing boundary node, and because a boundary
node's port id *is* the outer port id, both boundary crossings translate nothing at all —
`Flatten` got shorter. Ticket 03 keeps its demo, the boundary-node RESTRICT across the face,
ADR-0001's retirement, and the docs.

Ticket 01's stopgap test *"deleting a boundary node cannot silently repoint an outer wire"*
is rewritten as 01 said it should be, into two: deleting an *earlier* boundary node now
leaves a later outer wire delivering byte-identically (which is 03's claim, arriving early),
and deleting the boundary node a wire actually names still throws at compile. Ticket 03
moves that second throw forward to deletion time.

**Falsification, run rather than reasoned.** `Graph::removePort` was temporarily patched to
let a wire keep its *position* across a deletion — positional identity, reproduced — and the
demo failed on the sample comparison (`renderWith(2, true) != kept`) while the other 39 tests
stayed green. The patch was reverted. The demo also carries its own non-vacuity check
in-tree: `renderWith(3, false) != kept`, so the weighted inputs are provably told apart in
the output.

**Migration.** 157 construction sites and 134 wiring sites across the two test files and the
render CLI; every library node publishes accessors for its ports (`gain->gain()`,
`alloc->voice(v)`), so no call site anywhere names a port by position. Tests keep the node
beside its id with a local `place<T>()` helper.

**Verification beyond the checklist:** `render_cli` output is byte-identical to the
pre-change commit in both Debug and Release (`6df323c5…`); Release/NDEBUG suite green, so
nothing rests on the new asserts; all three direct-`clang++` fallback builds compile clean
under `-Wall -Wextra`. 40 kernel tests + 7 node tests + the purity grep, all passing.

**Left for ticket 03:** CLAUDE.md's status line and authoring-API prose, `docs/roadmap.md`
(line 111 still says the code freezes ports on graph-add), and `CONTEXT.md`'s executed-graph
entry, which says "ports frozen" a few lines above an *Avoid: freeze* note.
